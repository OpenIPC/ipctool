/* report_test [body.out]: the body `ipctool upload --backup` sends, checked
 * byte by byte; with a path, also written there, so it can be POSTed to a
 * running openipc.org service as it is (see the PR for the command). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "report.h"

static int failed;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static char *flatten(const report_body_t *b, size_t *len) {
    char *out = malloc(b->total + 1), *p = out;
    for (size_t i = 0; i < b->nspans; i++) {
        memcpy(p, b->spans[i].data, b->spans[i].len);
        p += b->spans[i].len;
    }
    *len = p - out;
    *p = 0;
    return out;
}

static const char *find(const char *hay, size_t n, const char *needle,
                        size_t nl) {
    for (size_t i = 0; i + nl <= n; i++)
        if (!memcmp(hay + i, needle, nl))
            return hay + i;
    return NULL;
}

#define FIND(h, n, lit) find(h, n, lit, sizeof(lit) - 1)

int main(int argc, char **argv) {
    const char yaml[] = "chip:\n  vendor: HiSilicon\n  model: 3516CV300\n";
    static char p1[70000], p2[3];
    memset(p1, 0xab, sizeof(p1));
    memcpy(p2, "\x01\x02\x03", 3);
    span_t blocks[3] = {{yaml, sizeof(yaml)}, {p1, sizeof(p1)}, {p2, 3}};

    report_body_t *b = calloc(1, sizeof(*b));
    report_body_init(b, "ipctool-test");
    report_add_field(b, "channel", "ipctool");
    report_add_field(b, "consent", "private");
    report_add_backup(b, blocks, 3);
    report_body_finish(b);
    CHECK(!b->overflow, "overflowed");

    size_t n;
    char *body = flatten(b, &n);
    CHECK(n == b->total, "Content-Length %zu, body %zu", b->total, n);
    CHECK(!strncmp(body, "--ipctool-test\r\n", 16),
          "does not open with the boundary");
    CHECK(n > 20 && !memcmp(body + n - 18, "--ipctool-test--\r\n", 18),
          "does not close with the final boundary");
    CHECK(FIND(body, n, "name=\"consent\"\r\n\r\nprivate\r\n") != NULL,
          "consent field");

    /* The backup part is the file format: YAML, NUL, then LE length + bytes
     * per partition, and nothing else before the CRLF and boundary. */
    const char *h = FIND(body, n, "name=\"backup\"");
    CHECK(h != NULL, "no backup part");
    const char *data = h ? find(h, n - (h - body), "\r\n\r\n", 4) : NULL;
    CHECK(data != NULL, "backup part has no header end");
    if (data) {
        data += 4;
        CHECK(!memcmp(data, yaml, sizeof(yaml)),
              "backup does not start with the YAML and its NUL");
        const unsigned char *l = (const unsigned char *)data + sizeof(yaml);
        CHECK(l[0] == (70000 & 0xff) && l[1] == ((70000 >> 8) & 0xff) &&
                  l[2] == 1 && l[3] == 0,
              "first partition length is not little-endian 70000");
        const char *p = (const char *)l + 4 + 70000;
        CHECK(!memcmp(p, "\x03\x00\x00\x00\x01\x02\x03\r\n--ipctool-test--\r\n",
                      25),
              "second partition, or what follows it, is wrong");
    }
    if (argc > 1) {
        FILE *f = fopen(argv[1], "wb");
        fwrite(body, 1, n, f);
        fclose(f);
    }
    free(body);

    /* yaml alone */
    report_body_init(b, "x");
    report_add_yaml(b, yaml, strlen(yaml));
    report_body_finish(b);
    body = flatten(b, &n);
    CHECK(n == b->total, "yaml-only length");
    CHECK(FIND(body, n, "name=\"yaml\"; filename=\"ipctool.yml\"") != NULL,
          "yaml part");
    free(body);
    free(b);

    report_answer_t a;
    report_parse_answer(
        201,
        "{\"id\":\"r-abcd2345\",\"receipt_url\":\"https://openipc.org/cameras/"
        "report/?id=r-abcd2345\","
        "\"identify\":{\"known\":true,\"matches\":[{\"manufacturer\":"
        "\"Xiongmai\",\"model\":\"50H20L\"}]}}",
        &a);
    CHECK(!strcmp(a.id, "r-abcd2345") && a.known &&
              !strcmp(a.match, "Xiongmai 50H20L"),
          "answer: %s %d %s", a.id, a.known, a.match);
    report_parse_answer(429, "{\"error\":\"10 reports a day\"}", &a);
    CHECK(!strcmp(a.error, "10 reports a day") && !*a.id, "refusal");
    report_parse_answer(502, "<html>bad gateway</html>", &a);
    CHECK(*a.error, "a non-JSON failure has no explanation");

    if (!failed)
        printf("report_test: ok\n");
    return failed;
}
