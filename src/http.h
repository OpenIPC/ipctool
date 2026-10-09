#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"

#define MAX_MTDBLOCKS 20

/* len bytes: at data, or -- when path is set -- read from that file while
 * they are sent, for what is too big to hold in RAM (a UBI volume of tens of
 * MB on a camera with less than that free). */
typedef struct {
    const char *data;
    size_t len;
    const char *path;
} span_t;

#define HTTP_ERR(err) ((int)err < 0 && (int)err > -100 ? (-(int)err) : 0)

#define ERR_GENERAL 1
#define ERR_SOCKET 2
#define ERR_GETADDRINFO 3
#define ERR_CONNECT 4
#define ERR_SEND 5
#define ERR_HTTP 6
#define ERR_MALLOC 7
#define ERR_STALLED 8
#define ERR_SOURCE 9
#define ERR_BUTT 10

/* The upload's timing, variables so http_test can shorten them: how long a
 * server has to answer the headers alone, how long a send may wait for
 * buffer space before the body counts as stalled, and how long an answer may
 * take after a stall or a refusal mid-body. */
extern int http_early_answer_ms;
extern int http_stall_timeout_s;
extern int http_late_answer_s;

/* POST spans as one body of total bytes; resp gets the answer's body, status
 * its HTTP status, location its Location header ("" without one). 0, or one
 * of the ERR_ codes above: ERR_SOURCE when a span's file could not be read
 * whole (http_post says which), ERR_STALLED when the body stopped moving and no
 * answer came. An answer that arrives before the body is sent -- a redirect
 * or a refusal on the headers alone -- is taken without sending the body. */
int http_post(const char *hostname, int port, const char *path, nservers_t *ns,
              const char *content_type, const span_t *spans, size_t nspans,
              size_t total, char *resp, size_t cap, int *status, char *location,
              size_t loccap);

#endif /* HTTP_H */
