/* `ipctool membw` -- synthetic DDR bandwidth probe.
 *
 * Implements OpenIPC/ipctool#160. Runs three memory-bandwidth ops
 * against large anonymous DDR buffers and reports MB/s:
 *
 *   write : memset over the buffer            (W-only, libc-dependent)
 *   read  : volatile uint32_t accumulator     (R-only, libc-INdependent --
 *                                              the most trustworthy number
 *                                              when comparing firmwares
 *                                              with different libcs)
 *   copy  : memcpy between two buffers        (R+W, counted as 2x bytes)
 *
 * Caveats baked into the design (per the issue body):
 *   - Buffers are obtained via `mmap(/dev/zero)`, NOT `malloc`, so they
 *     come from clean anonymous DDR pages rather than tmpfs / page cache.
 *   - Default 16 MB per buffer comfortably exceeds the L2 cache on V4
 *     family (256 KB - 1 MB). Smaller sizes measure L2/L1, not DDR.
 *   - Streamer / encoder DMA traffic loads DDR. To measure the DDR
 *     *config* baseline, stop majestic / vendor App first. To measure
 *     real *workload* bandwidth, leave them running.
 *   - The default of 16 MB × 16 iters processes ~1 GB across all three
 *     ops, which takes <2 s on a healthy V4 board and is light enough
 *     to run with the streamer up.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "membw.h"
#include "tools.h"

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct membw_opts {
    size_t mb;
    int iters;
    bool do_write;
    bool do_read;
    bool do_copy;
    bool want_json;
};

struct op_result {
    const char *name;
    double duration_s;
    double mb_per_sec;
};

static cJSON *result_to_json(const struct op_result *r) {
    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_NUM("mb_per_sec", r->mb_per_sec);
    ADD_PARAM_NUM("duration_s", r->duration_s);
    return j_inner;
}

static cJSON *run_bench(const struct membw_opts *o) {
    size_t sz = o->mb * 1024UL * 1024UL;

    int fd = open("/dev/zero", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "membw: open /dev/zero: %s\n", strerror(errno));
        return NULL;
    }
    char *a = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    char *b = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (a == MAP_FAILED || b == MAP_FAILED) {
        fprintf(stderr, "membw: mmap %zu MB: %s\n", o->mb, strerror(errno));
        if (a != MAP_FAILED)
            munmap(a, sz);
        if (b != MAP_FAILED)
            munmap(b, sz);
        close(fd);
        return NULL;
    }

    /* Fault pages in so the first real iteration isn't measuring
     * page-fault cost. */
    memset(a, 1, sz);
    memset(b, 2, sz);

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_NUM("buffer_mb", (double)o->mb);
    ADD_PARAM_NUM("iters", (double)o->iters);

    cJSON *results = cJSON_CreateObject();

    if (o->do_write) {
        double t0 = now_sec();
        for (int i = 0; i < o->iters; i++)
            memset(a, i & 0xff, sz);
        double dt = now_sec() - t0;
        struct op_result r = {.name = "write",
                              .duration_s = dt,
                              .mb_per_sec = (double)sz * o->iters / dt / 1e6};
        cJSON_AddItemToObject(results, "write", result_to_json(&r));
    }

    if (o->do_read) {
        volatile uint32_t sum = 0;
        double t0 = now_sec();
        for (int i = 0; i < o->iters; i++) {
            uint32_t *p = (uint32_t *)a;
            size_t n = sz / 4;
            for (size_t k = 0; k < n; k++)
                sum += p[k];
        }
        double dt = now_sec() - t0;
        struct op_result r = {.name = "read",
                              .duration_s = dt,
                              .mb_per_sec = (double)sz * o->iters / dt / 1e6};
        cJSON_AddItemToObject(results, "read", result_to_json(&r));
        /* sink the sum so the optimizer can't elide the loop entirely */
        (void)sum;
    }

    if (o->do_copy) {
        double t0 = now_sec();
        for (int i = 0; i < o->iters; i++)
            memcpy(b, a, sz);
        double dt = now_sec() - t0;
        /* memcpy moves 2× bytes (one read, one write) per byte of buffer */
        struct op_result r = {.name = "copy",
                              .duration_s = dt,
                              .mb_per_sec =
                                  (double)sz * o->iters * 2.0 / dt / 1e6};
        cJSON_AddItemToObject(results, "copy", result_to_json(&r));
    }

    cJSON_AddItemToObject(j_inner, "results", results);

    munmap(a, sz);
    munmap(b, sz);
    close(fd);
    return j_inner;
}

static bool parse_ops(const char *spec, struct membw_opts *o) {
    o->do_write = o->do_read = o->do_copy = false;
    char buf[64];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        if (!strcmp(tok, "write"))
            o->do_write = true;
        else if (!strcmp(tok, "read"))
            o->do_read = true;
        else if (!strcmp(tok, "copy"))
            o->do_copy = true;
        else
            return false;
    }
    return o->do_write || o->do_read || o->do_copy;
}

static void print_membw_usage(void) {
    printf(
        "Usage: ipctool membw [--size MB] [--iters N] [--ops set,...] "
        "[--json]\n"
        "\n"
        "Synthetic DDR bandwidth probe. Runs memset (write) / volatile-sum\n"
        "(read) / memcpy (copy) over anonymous DDR buffers and reports\n"
        "MB/s for each. Useful for separating CPU-bound from DDR-bound\n"
        "performance regressions, and for fleet comparison across boards\n"
        "with the same SoC.\n"
        "\n"
        "  --size MB     buffer size per pass (default: 16; must exceed L2)\n"
        "  --iters N     passes per op       (default: 16)\n"
        "  --ops a,b,c   comma list of write / read / copy (default: all)\n"
        "  --json        machine-readable JSON instead of YAML\n"
        "\n"
        "The `read` op is libc-INdependent and the most trustworthy number\n"
        "for cross-firmware comparison; `write` and `copy` are bounded by\n"
        "libc memset/memcpy vectorization.\n"
        "\n"
        "Run with majestic / vendor encoder stopped to measure the DDR\n"
        "config baseline; leave them running to measure real workload\n"
        "bandwidth.\n");
}

int membw_cmd(int argc, char **argv) {
    struct membw_opts o = {
        .mb = 16,
        .iters = 16,
        .do_write = true,
        .do_read = true,
        .do_copy = true,
        .want_json = false,
    };

    const struct option long_options[] = {
        {"size", required_argument, NULL, 's'},
        {"iters", required_argument, NULL, 'i'},
        {"ops", required_argument, NULL, 'o'},
        {"json", no_argument, NULL, 'j'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "s:i:o:jh", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 's': {
            long mb = strtol(optarg, NULL, 10);
            if (mb < 1 || mb > 4096) {
                fprintf(stderr, "membw: --size must be 1..4096 MB\n");
                return EXIT_FAILURE;
            }
            o.mb = (size_t)mb;
            break;
        }
        case 'i': {
            long it = strtol(optarg, NULL, 10);
            if (it < 1 || it > 1024) {
                fprintf(stderr, "membw: --iters must be 1..1024\n");
                return EXIT_FAILURE;
            }
            o.iters = (int)it;
            break;
        }
        case 'o':
            if (!parse_ops(optarg, &o)) {
                fprintf(stderr, "membw: --ops must be a comma list of "
                                "write,read,copy\n");
                return EXIT_FAILURE;
            }
            break;
        case 'j':
            o.want_json = true;
            break;
        case 'h':
            print_membw_usage();
            return EXIT_SUCCESS;
        default:
            print_membw_usage();
            return EXIT_FAILURE;
        }
    }

    cJSON *bench = run_bench(&o);
    if (!bench)
        return EXIT_FAILURE;

    /* Tag with chip identity for context. Falls through cleanly if
     * chip detection didn't run (e.g. on a host build). */
    const char *chip = getchipname();
    if (chip) {
        cJSON_AddItemToObject(bench, "chip", cJSON_CreateString(chip));
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "membw", bench);

    char *out = o.want_json ? cJSON_Print(root) : cYAML_Print(root);
    if (out) {
        printf("%s", out);
        if (o.want_json)
            printf("\n");
        free(out);
    }
    cJSON_Delete(root);
    return EXIT_SUCCESS;
}
