#ifndef HTTP_H
#define HTTP_H

int download(char *hostname, char *uri, nservers_t *ns, int writefd);
int upload(const char *hostname, const char *uri, nservers_t *ns,
           const char *data, size_t len);

#endif /* HTTP_H */
