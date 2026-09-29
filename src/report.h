#ifndef REPORT_H
#define REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"
#include "http.h"

/*
 * A report for openipc.org's board catalogue: POST /api/v1/reports as
 * multipart/form-data. The body is a list of spans -- part headers, the YAML,
 * each flash partition as it sits mapped in memory -- so a 16 MB backup is
 * sent without being copied.
 */

#define REPORT_MAX_PARTS 8
#define REPORT_MAX_SPANS (2 * MAX_MTDBLOCKS + 3 * REPORT_MAX_PARTS + 4)

typedef struct {
    char boundary[48];
    span_t spans[REPORT_MAX_SPANS];
    size_t nspans;
    size_t total;
    char heads[REPORT_MAX_PARTS][256];
    size_t nheads;
    uint32_t lens[MAX_MTDBLOCKS];
    char tail[64];
    bool overflow;
} report_body_t;

void report_body_init(report_body_t *b, const char *boundary);
/* A text field: consent, channel, tool, note. */
void report_add_field(report_body_t *b, const char *name, const char *value);
/* ipctool's output alone. */
void report_add_yaml(report_body_t *b, const char *yaml, size_t len);
/* A backup exactly as `ipctool backup <file>` writes it: the YAML and a NUL,
 * then each partition as a little-endian uint32 length and its bytes.
 * blocks[0] is the YAML (with its NUL); the rest are partitions. */
void report_add_backup(report_body_t *b, const span_t *blocks, size_t n);
void report_body_finish(report_body_t *b);

/* What openipc.org answered. */
typedef struct {
    int status;
    char id[32];
    char receipt_url[256];
    char error[512];
    bool known;
    char match[128];
} report_answer_t;

void report_parse_answer(int status, const char *body, report_answer_t *a);

/* ipctool's report as the no-argument run prints it; malloc'ed (main.c). */
char *build_report_yaml(void);

int report_cmd(int argc, char **argv);

#endif /* REPORT_H */
