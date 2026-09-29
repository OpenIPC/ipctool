#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"

#define MAX_MTDBLOCKS 20

typedef struct {
    const char *data;
    size_t len;
} span_t;

#define HTTP_ERR(err) ((int)err < 0 && (int)err > -100 ? (-(int)err) : 0)

#define ERR_GENERAL 1
#define ERR_SOCKET 2
#define ERR_GETADDRINFO 3
#define ERR_CONNECT 4
#define ERR_SEND 5
#define ERR_HTTP 6
#define ERR_MALLOC 7
#define ERR_BUTT 10

/* POST spans as one body of total bytes; resp gets the answer's body, status
 * its HTTP status. 0, or one of the ERR_ codes above. */
int http_post(const char *hostname, int port, const char *path, nservers_t *ns,
              const char *content_type, const span_t *spans, size_t nspans,
              size_t total, char *resp, size_t cap, int *status);

#endif /* HTTP_H */
