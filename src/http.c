#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

static void get_http_date(const char *inpbuf, const char *end, char *buf) {
    while (end - inpbuf >= 15) {
        if (!strncmp(inpbuf, "Last-Modified: ", 15)) {
            const char *ptr = inpbuf + 15;
            for (int i = 0; i < DATE_BUF_LEN; i++) {
                if (ptr[i] == '\n')
                    break;
                buf[i] = ptr[i];
            }
        }
        for (;;) {
            if (inpbuf == end)
                return;
            inpbuf++;
            if (*(inpbuf - 1) == '\n')
                break;
        }
    }
}

static int get_http_payload_len(const char *inpbuf, const char *end) {
    while (end - inpbuf >= 16) {
        if (!strncmp(inpbuf, "Content-Length: ", 16))
            return strtol(inpbuf + 16, NULL, 10);
        for (;;) {
            if (inpbuf == end)
                return 0;
            inpbuf++;
            if (*(inpbuf - 1) == '\n')
                break;
        }
    }
    return 0;
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

static int common_connect(const char *hostname, const char *uri, nservers_t *ns,
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
    addr.sin_port = htons(80);

    for (int i = 0; i < srv.len; i++) {
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

#define MAKE_ERROR(err) (char *)(-err)

#define SNPRINTF(...)                                                          \
    ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), __VA_ARGS__)

char *download(char *hostname, const char *uri, const char *useragent,
               nservers_t *ns, size_t *len, char *date, bool progress) {
    int s, ret;
    if ((ret = common_connect(hostname, uri, ns, &s) != ERR_GENERAL)) {
        return MAKE_ERROR(ret);
    }

    char buf[4096] = "GET /";
    char *ptr = buf + 5;
    if (uri) {
        SNPRINTF("%s", uri);
    }
    SNPRINTF(" HTTP/1.0\r\nHost: %s\r\n", hostname);
    if (useragent)
        SNPRINTF("User-Agent: %s\r\n", useragent);
    SNPRINTF("\r\n");
    int tosend = ptr - buf;
    int nsent = send(s, buf, tosend, 0);
    if (nsent != tosend)
        return MAKE_ERROR(ERR_SEND);

    int header = 1;
    int nrecvd;
    char *binbuf = NULL, *sptr;
    int percent = 0;
    while ((nrecvd = recv(s, buf, sizeof(buf), 0))) {
        char *ptr = buf;
        if (header) {
            ptr = strstr(buf, "\r\n\r\n");
            if (!ptr)
                continue;

            int rcode = get_http_respcode(buf);
            if (rcode / 100 != 2) {
                close(s);
                return MAKE_ERROR(rcode / 100 * 10 + rcode % 10);
            }
            *len = get_http_payload_len(buf, ptr);
            if (!*len) {
                fprintf(stderr, "No length found, aborting...\n");
                close(s);
                return MAKE_ERROR(ERR_HTTP);
            }
            get_http_date(buf, ptr, date);
            binbuf = malloc(*len);
            if (!binbuf) {
                close(s);
                return MAKE_ERROR(ERR_MALLOC);
            }
            sptr = binbuf;

            header = 0;
            ptr += 4;
            nrecvd -= ptr - buf;
        }

        if (!header) {
            if (progress) {
                int np = 100 * (sptr - binbuf) / *len;
                if (np != percent) {
                    printf("Downloading %d%%\r", np);
                    fflush(stdout);
                    percent = np;
                }
            }
            // TODO: avoid copying?
            memcpy(sptr, ptr, nrecvd);
            sptr += nrecvd;
        }
    }

    return binbuf;
}

int upload(const char *hostname, const char *uri, nservers_t *ns,
           span_t blocks[MAX_MTDBLOCKS + 1], size_t blocks_num) {
    int s, ret;
    if ((ret = common_connect(hostname, uri, ns, &s) != ERR_GENERAL)) {
        return ret;
    }

    size_t len = 0;
    for (int i = 0; i < blocks_num; i++) {
        len += blocks[i].len;
        // add len header
        if (i)
            len += sizeof(uint32_t);
    }

    char buf[4096] = "PUT /";
    if (uri) {
        strncat(buf, uri, sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, " HTTP/1.0\r\nHost: ", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, hostname, sizeof(buf) - strlen(buf) - 1);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf) - 1,
             "\r\n"
             "Content-type: application/octet-stream\r\n"
             "Connection: close\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             len);
    int tosent = strlen(buf);
    int nsent = send(s, buf, tosent, 0);
    if (nsent != tosent)
        return ERR_SEND;

    for (int i = 0; i < blocks_num; i++) {
        if (i) {
            uint32_t len_header = blocks[i].len;
            write(s, &len_header, sizeof(len_header));
        }

        int nbytes = write(s, blocks[i].data, blocks[i].len);
        if (nbytes == -1)
            break;
#if 0
	printf("[%d] sent %d bytes\n", i, nbytes);
#endif
    }

    return 0;
}
