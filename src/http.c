#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dns.h"
#include "http.h"

static int get_http_respcode(const char *inpbuf) {
    char proto[32], descr[32];
    int code;

    if (sscanf(inpbuf, "%31s %d %31s", proto, &code, descr) < 2)
        return -1;
    return code;
}

/* Connect within timeout_ms: 0 when connected, -1 otherwise (errno says
 * why). A non-blocking connect nearly always completes through poll(); the
 * old version returned poll()'s 1 for that case, which the caller read as a
 * failure, so an upload reached the server only when connect() happened to
 * finish at once. */
int connect_with_timeout(int sockfd, const struct sockaddr *addr,
                         socklen_t addrlen, unsigned int timeout_ms) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    int rc = 0;
    if (connect(sockfd, addr, addrlen) < 0) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            rc = -1;
        } else {
            struct timespec start, now;
            clock_gettime(CLOCK_MONOTONIC, &start);
            for (;;) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                long spent = (now.tv_sec - start.tv_sec) * 1000l +
                             (now.tv_nsec - start.tv_nsec) / 1000000l;
                if (spent >= (long)timeout_ms) {
                    errno = ETIMEDOUT;
                    rc = -1;
                    break;
                }
                struct pollfd pfd = {.fd = sockfd, .events = POLLOUT};
                int n = poll(&pfd, 1, (int)(timeout_ms - spent));
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0) {
                    if (n == 0)
                        errno = ETIMEDOUT;
                    rc = -1;
                    break;
                }
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 ||
                    error != 0) {
                    if (error)
                        errno = error;
                    rc = -1;
                }
                break;
            }
        }
    }
    if (fcntl(sockfd, F_SETFL, flags) < 0)
        return -1;
    return rc;
}

#define CONNECT_TIMEOUT 3000 // milliseconds

static int common_connect(const char *hostname, int port, nservers_t *ns,
                          int *s) {
    int ret = ERR_GENERAL;

    a_records_t srv;
    if (!resolv_name(ns, hostname, &srv)) {
        return ERR_GETADDRINFO;
    }

    *s = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    for (size_t i = 0; i < srv.len; i++) {
        memcpy(&addr.sin_addr, &srv.ipv4_addr[i], sizeof(uint32_t));

#ifndef NDEBUG
        char buf[256];
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        fprintf(stdout, "Connecting to %s...\n", buf);
#endif

        if (connect_with_timeout(*s, (struct sockaddr *)&addr, sizeof(addr),
                                 CONNECT_TIMEOUT) == 0) {
            ret = ERR_GENERAL; // connected: the caller's success value
            break;
        }
        close(*s);
        *s = socket(AF_INET, SOCK_STREAM, 0);
        ret = ERR_CONNECT;
    }
    return ret;
}

/* A send that cannot place a byte for this long means the body has stopped
 * moving; nothing on a working connection waits that long for buffer space. */
int http_stall_timeout_s = 30;

// send(), not write(): a server that answers early (413, 429) and closes
// must not kill ipctool with SIGPIPE before it can print the reason. Each
// send waits for buffer space through poll(), so a connection that stops
// draining fails after http_stall_timeout_s with EAGAIN -- SO_SNDTIMEO would
// restart its clock whenever a few bytes squeeze in, and a frozen upload took
// minutes to give up.
static int write_all(int s, const char *data, size_t len) {
    while (len) {
        struct pollfd pfd = {.fd = s, .events = POLLOUT};
        int ready = poll(&pfd, 1, http_stall_timeout_s * 1000);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready == 0)
            errno = EAGAIN;
        if (ready <= 0)
            return -1;
        ssize_t n = send(s, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (n <= 0)
            return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

/* The value of header name in the head of an answer (up to its blank line),
 * into out; "" when there is none. */
static void find_header(const char *head, const char *name, char *out,
                        size_t cap) {
    size_t nl = strlen(name);
    *out = '\0';
    for (const char *line = strstr(head, "\r\n"); line && line[2] != '\r';
         line = strstr(line + 2, "\r\n")) {
        const char *h = line + 2;
        if (strncasecmp(h, name, nl) || h[nl] != ':')
            continue;
        h += nl + 1;
        while (*h == ' ' || *h == '\t')
            h++;
        size_t n = strcspn(h, "\r\n");
        if (n >= cap)
            n = cap - 1;
        memcpy(out, h, n);
        out[n] = '\0';
        while (n && isspace((unsigned char)out[n - 1]))
            out[--n] = '\0';
        return;
    }
}

/* How long the server has to answer the headers alone, before the body
 * goes. A server that redirects or refuses says so at once, and a body it
 * will not read is never sent: through a filter that freezes a connection
 * after its first few kilobytes, the answer to the headers still gets back,
 * and the body would not. A server that wants the body says nothing, and
 * this is what it costs. */
int http_early_answer_ms = 2000;
/* After a stall or a refusal mid-body, how long an answer may take. */
int http_late_answer_s = 10;

int http_post(const char *hostname, int port, const char *path, nservers_t *ns,
              const char *content_type, const span_t *spans, size_t nspans,
              size_t total, char *resp, size_t cap, int *status, char *location,
              size_t loccap) {
    if (loccap)
        *location = '\0';
    int s;
    int rc = common_connect(hostname, port, ns, &s);
    /* common_connect() answers ERR_GENERAL when it is connected. */
    if (rc != ERR_GENERAL)
        return rc;

    struct timeval tv = {.tv_sec = 120};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    // The whole body is in: the server stores it, then answers.
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    bool stalled = false;

    char host[300];
    if (port == 80)
        snprintf(host, sizeof(host), "%s", hostname);
    else
        snprintf(host, sizeof(host), "%s:%d", hostname, port);
    char head[1024];
    int hl = snprintf(head, sizeof(head),
                      "POST %s HTTP/1.0\r\n"
                      "Host: %s\r\n"
                      "User-Agent: ipctool\r\n"
                      "Accept: application/json\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, host, content_type, total);
    if (hl <= 0 || (size_t)hl >= sizeof(head) || write_all(s, head, hl)) {
        close(s);
        return ERR_SEND;
    }

    struct pollfd early = {.fd = s, .events = POLLIN};
    int ready;
    do
        ready = poll(&early, 1, http_early_answer_ms);
    while (ready < 0 && errno == EINTR);
    if (ready > 0) {
        // An interim 1xx asks for the body (no server should send one to
        // HTTP/1.0, but it costs nothing to allow): send it, and the 1xx is
        // skipped with the final answer below. Anything else is the answer.
        char peek[64];
        ssize_t n = recv(s, peek, sizeof(peek) - 1, MSG_PEEK);
        if (n <= 0)
            goto answer;
        peek[n] = '\0';
        int code = get_http_respcode(peek);
        if (code < 100 || code > 199)
            goto answer;
    }

    size_t sent = 0;
    int shown = -1;
    for (size_t i = 0; i < nspans; i++) {
        /* In 64 KB pieces, so a 16 MB partition shows its progress. */
        for (size_t off = 0; off < spans[i].len;) {
            size_t chunk = spans[i].len - off;
            if (chunk > 65536)
                chunk = 65536;
            if (write_all(s, spans[i].data + off, chunk)) {
                // The server may have answered and closed: read what it
                // said, so a refusal reaches the user as a reason. Or the
                // body stopped moving, and nothing is coming back either.
                stalled = errno == EAGAIN;
                if (shown >= 0)
                    fprintf(stderr, "\n");
                tv.tv_sec = http_late_answer_s;
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                goto answer;
            }
            off += chunk;
            sent += chunk;
            if (total > (1 << 20)) {
                int pct = (int)(100.0 * sent / total);
                if (pct != shown) {
                    fprintf(stderr, "\rSending %d%%", pct);
                    shown = pct;
                }
            }
        }
    }
    if (shown >= 0)
        fprintf(stderr, "\n");

answer:;
    size_t got = 0;
    for (;;) {
        ssize_t n = recv(s, resp + got, cap - 1 - got, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        got += (size_t)n;
        if (got == cap - 1)
            break;
    }
    close(s);
    resp[got] = '\0';
    *status = get_http_respcode(resp);
    char *body = strstr(resp, "\r\n\r\n");
    while (*status >= 100 && *status <= 199 && body) {
        memmove(resp, body + 4, strlen(body + 4) + 1);
        *status = get_http_respcode(resp);
        body = strstr(resp, "\r\n\r\n");
    }
    if (*status < 0 || !body)
        return got ? ERR_HTTP : stalled ? ERR_STALLED : ERR_SEND;
    if (loccap)
        find_header(resp, "Location", location, loccap);
    memmove(resp, body + 4, strlen(body + 4) + 1);
    return 0;
}
