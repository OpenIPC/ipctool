/* http_test: http_post() against servers forked on the loopback, the way
 * `ipctool upload` meets them -- one that redirects on the headers, one that
 * takes the body, one that sends an interim 100 first, and one that never
 * reads, as a filter that freezes the connection does (#232). The timeouts
 * are shortened through the variables http.h exposes for this. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "http.h"
#include "report.h"

static int failed;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

enum mode { REDIRECT, ACCEPT, CONTINUE, FREEZE };

/* The server's half, in a child: one connection, then exit. Its exit code
 * is how many body bytes it read, in MB rounded down, or 99 when it read
 * any body at all where it should not have. */
static void serve(int ls, enum mode mode, int peer_port) {
    int c = accept(ls, NULL, NULL);
    if (c < 0)
        _exit(98);
    char buf[65536];
    size_t got = 0;
    char *end = NULL;
    while (!end) {
        ssize_t n = recv(c, buf + got, sizeof(buf) - 1 - got, 0);
        if (n <= 0)
            _exit(97);
        got += (size_t)n;
        buf[got] = '\0';
        end = strstr(buf, "\r\n\r\n");
    }
    size_t body = got - (size_t)(end + 4 - buf);
    if (mode == FREEZE) {
        pause(); // killed by the parent
        _exit(0);
    }
    if (mode == REDIRECT) {
        char a[256];
        int l = snprintf(a, sizeof(a),
                         "HTTP/1.1 307 Temporary Redirect\r\n"
                         "Location: http://127.0.0.1:%d/next\r\n"
                         "Content-Length: 0\r\nConnection: close\r\n\r\n",
                         peer_port);
        send(c, a, l, 0);
        usleep(200000);
        // Whatever arrived after the headers is body this server never
        // asked for.
        ssize_t n = recv(c, buf, sizeof(buf), MSG_DONTWAIT);
        _exit(body || n > 0 ? 99 : 0);
    }
    if (mode == CONTINUE) {
        const char *cont = "HTTP/1.1 100 Continue\r\n\r\n";
        send(c, cont, strlen(cont), 0);
    }
    // http_post() spells it so; strcasestr() is a GNU extension.
    const char *cl = strstr(buf, "Content-Length:");
    size_t want = cl ? strtoul(cl + 15, NULL, 10) : 0;
    while (body < want) {
        ssize_t n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        body += (size_t)n;
    }
    const char *ok =
        "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\n"
        "Connection: close\r\n\r\n{\"id\":\"r-test1234\"}";
    send(c, ok, strlen(ok), 0);
    close(c);
    _exit((int)(body >> 20));
}

static pid_t spawn(enum mode mode, int *port, int peer_port) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (mode == FREEZE) {
        // Small, so the client's sends stop finding room soon.
        int small = 4096;
        setsockopt(ls, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    }
    struct sockaddr_in a = {.sin_family = AF_INET,
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t al = sizeof(a);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) || listen(ls, 1) ||
        getsockname(ls, (struct sockaddr *)&a, &al)) {
        perror("listen");
        exit(2);
    }
    *port = ntohs(a.sin_port);
    pid_t pid = fork();
    if (pid == 0)
        serve(ls, mode, peer_port);
    close(ls);
    return pid;
}

static int reaped(pid_t pid) {
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static double since(const struct timespec *t0) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0->tv_sec) + (t.tv_nsec - t0->tv_nsec) / 1e9;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    http_early_answer_ms = 300;
    http_stall_timeout_s = 1;
    http_late_answer_s = 1;

    size_t total = 16u << 20; // a 16 MB flash
    char *big = malloc(total);
    memset(big, 0x5a, total);
    span_t span = {big, total};
    nservers_t ns = {.len = 0};
    static char resp[4096], loc[256];
    int status, err;

    // A redirect on the headers: no body goes to that server, and the
    // Location names where it should.
    int accept_port, redirect_port;
    pid_t acc = spawn(ACCEPT, &accept_port, 0);
    pid_t red = spawn(REDIRECT, &redirect_port, accept_port);
    err = http_post("127.0.0.1", redirect_port, "/api/v1/reports", &ns,
                    "application/octet-stream", &span, 1, total, resp,
                    sizeof(resp), &status, loc, sizeof(loc));
    CHECK(!err && status == 307, "redirect: err %d, status %d", err, status);
    CHECK(reaped(red) == 0, "the redirecting server was sent the body");
    char host[64], path[64];
    int port;
    CHECK(report_redirect(loc, host, sizeof(host), &port, path, sizeof(path)) &&
              port == accept_port && !strcmp(path, "/next"),
          "Location %s", loc);

    // Followed: the whole body arrives, and the answer is read.
    err = http_post(host, port, path, &ns, "application/octet-stream", &span, 1,
                    total, resp, sizeof(resp), &status, loc, sizeof(loc));
    CHECK(!err && status == 201 && strstr(resp, "r-test1234"),
          "accept: err %d, status %d, %s", err, status, resp);
    CHECK(reaped(acc) == 16, "the body did not arrive whole");

    // An interim 100 is not the answer: the body still goes, and the final
    // 201 is what comes back.
    int cont_port;
    pid_t cont = spawn(CONTINUE, &cont_port, 0);
    err = http_post("127.0.0.1", cont_port, "/", &ns,
                    "application/octet-stream", &span, 1, total, resp,
                    sizeof(resp), &status, loc, sizeof(loc));
    CHECK(!err && status == 201, "100 Continue: err %d, status %d", err,
          status);
    CHECK(reaped(cont) == 16, "after a 100 the body did not arrive whole");

    // A span read from a file as it is sent -- a UBI volume too big for RAM
    // -- arrives whole, between spans held in memory.
    char tmpl[] = "/tmp/http_test.XXXXXX";
    int tf = mkstemp(tmpl);
    CHECK(tf >= 0 && write(tf, big, total) == (ssize_t)total, "temp file");
    close(tf);
    span_t mixed[3] = {
        {big, 1u << 20, NULL}, {NULL, total, tmpl}, {big, 1u << 20, NULL}};
    int file_port;
    pid_t fil = spawn(ACCEPT, &file_port, 0);
    err = http_post("127.0.0.1", file_port, "/", &ns,
                    "application/octet-stream", mixed, 3, total + (2u << 20),
                    resp, sizeof(resp), &status, loc, sizeof(loc));
    CHECK(!err && status == 201, "file span: err %d, status %d", err, status);
    CHECK(reaped(fil) == 18, "the file span did not arrive whole");

    // A file shorter than its span promised stops the upload: the
    // Content-Length is already sent, and a short body is a broken one.
    span_t longer = {NULL, total + 1, tmpl};
    int short_port;
    pid_t sho = spawn(ACCEPT, &short_port, 0);
    err = http_post("127.0.0.1", short_port, "/", &ns,
                    "application/octet-stream", &longer, 1, total + 1, resp,
                    sizeof(resp), &status, loc, sizeof(loc));
    CHECK(err == ERR_SOURCE, "a short file: err %d", err);
    reaped(sho);
    unlink(tmpl);

    // A server that never reads: a stall, in the stall timeout plus the late
    // answer's, not the minutes it used to take.
    int freeze_port;
    pid_t frz = spawn(FREEZE, &freeze_port, 0);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    err = http_post("127.0.0.1", freeze_port, "/", &ns,
                    "application/octet-stream", &span, 1, total, resp,
                    sizeof(resp), &status, loc, sizeof(loc));
    double took = since(&t0);
    kill(frz, SIGKILL);
    reaped(frz);
    CHECK(err == ERR_STALLED, "freeze: err %d", err);
    CHECK(took < 10, "freeze took %.1f s", took);

    free(big);
    if (!failed)
        printf("http_test: ok\n");
    return failed;
}
