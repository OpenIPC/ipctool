#ifndef HTTP_H
#define HTTP_H

#define MAX_MTDBLOCKS 20

typedef struct {
    const char *data;
    size_t len;
} span_t;

#define HTTP_ERR(err) ((int)err < 0 ? (-(int)err) : 0)

#define ERR_GENERAL 1
#define ERR_SOCKET 2
#define ERR_GETADDRINFO 3
#define ERR_CONNECT 4
#define ERR_SEND 5
#define ERR_HTTP 6
#define ERR_MALLOC 7
#define ERR_BUTT 10

char *download(char *hostname, char *uri, const char *useragent, nservers_t *ns,
               int *len);
int upload(const char *hostname, const char *uri, nservers_t *ns,
           span_t blocks[MAX_MTDBLOCKS + 1], size_t len);

#endif /* HTTP_H */
