#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cjson/cJSON.h"
#include "report.h"

static void add_span(report_body_t *b, const char *data, size_t len) {
    if (b->nspans == REPORT_MAX_SPANS) {
        b->overflow = true;
        return;
    }
    b->spans[b->nspans].data = data;
    b->spans[b->nspans].len = len;
    b->spans[b->nspans].path = NULL;
    b->nspans++;
    b->total += len;
}

static void add_head(report_body_t *b, const char *name, const char *filename,
                     const char *type) {
    if (b->nheads == REPORT_MAX_PARTS) {
        b->overflow = true;
        return;
    }
    char *h = b->heads[b->nheads++];
    if (filename)
        snprintf(h, sizeof(b->heads[0]),
                 "--%s\r\nContent-Disposition: form-data; name=\"%s\"; "
                 "filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                 b->boundary, name, filename, type);
    else
        snprintf(h, sizeof(b->heads[0]),
                 "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n",
                 b->boundary, name);
    add_span(b, h, strlen(h));
}

static const char crlf[] = "\r\n";

void report_body_init(report_body_t *b, const char *boundary) {
    memset(b, 0, sizeof(*b));
    snprintf(b->boundary, sizeof(b->boundary), "%s", boundary);
}

void report_add_field(report_body_t *b, const char *name, const char *value) {
    add_head(b, name, NULL, NULL);
    add_span(b, value, strlen(value));
    add_span(b, crlf, 2);
}

void report_add_yaml(report_body_t *b, const char *yaml, size_t len) {
    add_head(b, "yaml", "ipctool.yml", "text/plain; charset=utf-8");
    add_span(b, yaml, len);
    add_span(b, crlf, 2);
}

void report_add_backup(report_body_t *b, const span_t *blocks, size_t n) {
    add_head(b, "backup", "backup.bin", "application/octet-stream");
    for (size_t i = 0; i < n; i++) {
        if (i) {
            if (i - 1 >= MAX_MTDBLOCKS) {
                b->overflow = true;
                return;
            }
            /* Little-endian on every camera ipctool runs on; stored as the
             * bytes the file format wants whatever the host. */
            uint32_t l = (uint32_t)blocks[i].len;
            unsigned char *le = (unsigned char *)&b->lens[i - 1];
            le[0] = l & 0xff;
            le[1] = (l >> 8) & 0xff;
            le[2] = (l >> 16) & 0xff;
            le[3] = (l >> 24) & 0xff;
            add_span(b, (const char *)le, 4);
        }
        add_span(b, blocks[i].data, blocks[i].len);
        if (!b->overflow)
            b->spans[b->nspans - 1].path = blocks[i].path;
    }
    add_span(b, crlf, 2);
}

void report_body_finish(report_body_t *b) {
    snprintf(b->tail, sizeof(b->tail), "--%s--\r\n", b->boundary);
    add_span(b, b->tail, strlen(b->tail));
}

static void copy_string(const cJSON *obj, const char *key, char *dst,
                        size_t cap) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring)
        snprintf(dst, cap, "%s", v->valuestring);
}

void report_parse_answer(int status, const char *body, report_answer_t *a) {
    memset(a, 0, sizeof(*a));
    a->status = status;
    cJSON *root = body ? cJSON_Parse(body) : NULL;
    if (!root) {
        if (status / 100 != 2)
            snprintf(a->error, sizeof(a->error), "no explanation came with it");
        return;
    }
    copy_string(root, "id", a->id, sizeof(a->id));
    copy_string(root, "receipt_url", a->receipt_url, sizeof(a->receipt_url));
    copy_string(root, "error", a->error, sizeof(a->error));
    const cJSON *ident = cJSON_GetObjectItemCaseSensitive(root, "identify");
    if (ident) {
        a->known =
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ident, "known"));
        const cJSON *m = cJSON_GetArrayItem(
            cJSON_GetObjectItemCaseSensitive(ident, "matches"), 0);
        if (m) {
            char maker[64] = "", model[64] = "";
            copy_string(m, "manufacturer", maker, sizeof(maker));
            copy_string(m, "model", model, sizeof(model));
            snprintf(a->match, sizeof(a->match), "%s %s", maker, model);
        }
    }
    cJSON_Delete(root);
}

bool report_redirect(const char *location, char *host, size_t hostcap,
                     int *port, char *path, size_t pathcap) {
    static const char scheme[] = "http://";
    if (strncasecmp(location, scheme, sizeof(scheme) - 1))
        return false;
    const char *h = location + sizeof(scheme) - 1;
    const char *p = strpbrk(h, "/?#");
    if (!p)
        p = h + strlen(h);
    size_t hl = (size_t)(p - h);
    const char *colon = memchr(h, ':', hl);
    size_t namelen = colon ? (size_t)(colon - h) : hl;
    if (!namelen || namelen >= hostcap || memchr(h, '@', hl))
        return false;
    int prt = 80;
    if (colon) {
        char *end;
        long v = strtol(colon + 1, &end, 10);
        if (end != p || v <= 0 || v > 65535)
            return false;
        prt = (int)v;
    }
    // The fragment is the client's alone; a query with no path before it
    // still asks for "/".
    size_t pl = strcspn(p, "#");
    const char *lead = *p == '/' ? "" : "/";
    if (strlen(lead) + pl >= pathcap || memchr(p, ' ', pl) ||
        memchr(p, '\r', pl) || memchr(p, '\n', pl))
        return false;
    memcpy(host, h, namelen);
    host[namelen] = '\0';
    *port = prt;
    snprintf(path, pathcap, "%s%.*s", lead, (int)pl, p);
    return true;
}
