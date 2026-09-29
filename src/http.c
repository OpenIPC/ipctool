#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
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

int connect_with_timeout(int sockfd, const struct sockaddr *addr,
                         socklen_t addrlen, unsigned int timeout_ms) {
    int rc = 0;
    // Set O_NONBLOCK
    int sockfd_flags_before;
    if ((sockfd_flags_before = fcntl(sockfd, F_GETFL, 0) < 0))
        return -1;
    if (fcntl(sockfd, F_SETFL, sockfd_flags_before | O_NONBLOCK) < 0)
        return -1;
    // Start connecting (asynchronously)
    do {
        if (connect(sockfd, addr, addrlen) < 0) {
            // Did connect return an error? If so, we'll fail.
            if ((errno != EWOULDBLOCK) && (errno != EINPROGRESS)) {
                rc = -1;
            }
            // Otherwise, we'll wait for it to complete.
            else {
                // Set a deadline timestamp 'timeout' ms from now (needed b/c
                // poll can be interrupted)
                struct timespec now;
                if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                    rc = -1;
                    break;
                }
                struct timespec deadline = {.tv_sec = now.tv_sec,
                                            .tv_nsec = now.tv_nsec +
                                                       timeout_ms * 1000000l};
                // Wait for the connection to complete.
                do {
                    // Calculate how long until the deadline
                    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                        rc = -1;
                        break;
                    }
                    int ms_until_deadline =
                        (int)((deadline.tv_sec - now.tv_sec) * 1000l +
                              (deadline.tv_nsec - now.tv_nsec) / 1000000l);
                    if (ms_until_deadline < 0) {
                        rc = 0;
                        break;
                    }
                    // Wait for connect to complete (or for the timeout
                    // deadline)
                    struct pollfd pfds[] = {{.fd = sockfd, .events = POLLOUT}};
                    rc = poll(pfds, 1, ms_until_deadline);
                    // If poll 'succeeded', make sure it *really* succeeded
                    if (rc > 0) {
                        int error = 0;
                        socklen_t len = sizeof(error);
                        int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                                                &error, &len);
                        if (retval == 0)
                            errno = error;
                        if (error != 0)
                            rc = -1;
                    }
                }
                // If poll was interrupted, try again.
                while (rc == -1 && errno == EINTR);
                // Did poll timeout? If so, fail.
                if (rc == 0) {
                    errno = ETIMEDOUT;
                    rc = -1;
                }
            }
        }
    } while (0);
    // Restore original O_NONBLOCK state
    if (fcntl(sockfd, F_SETFL, sockfd_flags_before) < 0)
        return -1;
    // Success
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
                                 CONNECT_TIMEOUT) != 1) {
            ret = ERR_GENERAL;
            break;
        }
        close(*s);
        *s = socket(AF_INET, SOCK_STREAM, 0);
        ret = ERR_CONNECT;
    }
    return ret;
}

static int write_all(int s, const char *data, size_t len) {
    while (len) {
        ssize_t n = write(s, data, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

int http_post(const char *hostname, int port, const char *path, nservers_t *ns,
              const char *content_type, const span_t *spans, size_t nspans,
              size_t total, char *resp, size_t cap, int *status) {
    int s;
    int rc = common_connect(hostname, port, ns, &s);
    /* common_connect() answers ERR_GENERAL when it is connected. */
    if (rc != ERR_GENERAL)
        return rc;

    struct timeval tv = {.tv_sec = 120};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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

    size_t sent = 0;
    int shown = -1;
    for (size_t i = 0; i < nspans; i++) {
        /* In 64 KB pieces, so a 16 MB partition shows its progress. */
        for (size_t off = 0; off < spans[i].len;) {
            size_t chunk = spans[i].len - off;
            if (chunk > 65536)
                chunk = 65536;
            if (write_all(s, spans[i].data + off, chunk)) {
                close(s);
                return ERR_SEND;
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
    if (*status < 0 || !body)
        return ERR_HTTP;
    memmove(resp, body + 4, strlen(body + 4) + 1);
    return 0;
}
