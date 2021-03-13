#ifndef DNS_H
#define DNS_H

#define MAX_NSERVERS 16
typedef struct {
    uint32_t ipv4_addr[MAX_NSERVERS];
    size_t len;
} nservers_t;

#define MAX_ARECORDS 16
typedef struct {
    uint32_t ipv4_addr[MAX_ARECORDS];
    size_t len;
} a_records_t;

bool resolv_name(nservers_t *ns, const char *hostname, a_records_t *srv);
void add_predefined_ns(nservers_t *ns, ...);
bool parse_resolv_conf(nservers_t *ns);
void print_nservers(nservers_t *ns);

#endif /* DNS_H */
