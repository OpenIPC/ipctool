#ifndef HTTP_H
#define HTTP_H

#define MAX_MTDBLOCKS 20

typedef struct {
    const char *data;
    size_t len;
} span_t;

int download(char *hostname, char *uri, nservers_t *ns, int writefd);
int upload(const char *hostname, const char *uri, nservers_t *ns,
           span_t blocks[MAX_MTDBLOCKS + 1], size_t len);

#endif /* HTTP_H */
