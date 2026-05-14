/* `ipctool cpubench` — multi-pattern CPU clock triangulation.
 *
 * Runs tight inline-asm loops with three known instruction patterns and
 * back-calculates the CPU clock from each. Used in ipctool#161 to settle
 * which CRG register really drives the V4 CPU PLL (turned out: register
 * pair at 0x12010000/0x12010004, NOT the 0x12010014 shadow the issue body
 * originally identified). Kept as a battle-proven test for future board
 * bring-up where the PLL register decode is suspect.
 *
 * Cortex-A7 reference throughput (Arm Cortex-A7 MPCore TRM):
 *   - Dependent ADD chain    : 1.0 cyc/op  (in-order, RAW stalls)
 *   - Independent ADD pair   : 0.5 cyc/op  (dual-issue, two ALU pipes)
 *   - Dependent MUL chain    : 3.0 cyc/op  (3-cyc latency, latency-bound)
 *
 * Inner-loop block has 16 ops; outer-loop overhead is 3 cycles per iter
 * (sub + cmp + branch). Implied clock per pattern:
 *
 *     f_dep_add  = ops_per_sec * (16 + 3) / 16          = ops_per_sec * 1.1875
 *     f_indep    = ops_per_sec * (16 + 3) / 32          = ops_per_sec * 0.594
 *     f_dep_mul  = ops_per_sec * (16*3 + 3) / 16        = ops_per_sec * 3.1875
 *
 * Three independent timing models should converge to within ~2% on a
 * Cortex-A7. Build is ARM-gated; on x86/etc. the subcommand reports
 * "not supported".
 */

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "cpubench.h"
#include "tools.h"

#ifdef __arm__

/* Replicate a string 16 times so the inner asm block has 16 ops, making
 * outer-loop overhead negligible. */
#define REP16(X) X X X X X X X X X X X X X X X X

static volatile uint32_t sink_u;

static double bench_dep_add(uint64_t loops) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint32_t r0 = 0;
    for (uint64_t i = 0; i < loops; i++) {
        __asm__ __volatile__(REP16("add %0, %0, #1\n\t") : "+r"(r0));
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sink_u = r0;
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    return (double)loops * 16.0 / dt;
}

static double bench_indep_add(uint64_t loops) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint32_t r0 = 0, r1 = 0;
    for (uint64_t i = 0; i < loops; i++) {
        __asm__ __volatile__(REP16("add %0, %0, #1\n\t"
                                   "add %1, %1, #1\n\t")
                             : "+r"(r0), "+r"(r1));
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sink_u = r0 + r1;
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    return (double)loops * 32.0 / dt;
}

static double bench_dep_mul(uint64_t loops) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint32_t r0 = 3;
    for (uint64_t i = 0; i < loops; i++) {
        __asm__ __volatile__(REP16("mul %0, %0, %0\n\t") : "+r"(r0));
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sink_u = r0;
    double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    return (double)loops * 16.0 / dt;
}

struct pattern_result {
    const char *name;  /* JSON key */
    const char *label; /* short human label */
    double ops_per_sec;
    double freq_mhz;
    double cycles_per_op; /* assumed model */
    const char *model_note;
};

static cJSON *result_to_json(const struct pattern_result *r) {
    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_NUM("ops_per_sec_M", r->ops_per_sec / 1e6);
    ADD_PARAM_NUM("cycles_per_op", r->cycles_per_op);
    ADD_PARAM_NUM("freq_mhz", r->freq_mhz);
    if (r->model_note)
        ADD_PARAM("model", r->model_note);
    return j_inner;
}

static cJSON *build_cpubench_json(uint64_t loops) {
    /* Three timing models, with the 16-ops-per-block + 3-cyc outer overhead
     * accounted for in the implied-clock calculation. */
    struct pattern_result results[3];
    results[0].name = "dep_add";
    results[0].label = "Dependent integer ADD";
    results[0].ops_per_sec = bench_dep_add(loops);
    results[0].cycles_per_op = 19.0 / 16.0; /* 1 cyc/op + 3 cyc loop overhead */
    results[0].freq_mhz =
        results[0].ops_per_sec * results[0].cycles_per_op / 1e6;
    results[0].model_note = "A7: 1 cyc/op dep ALU";

    results[1].name = "indep_add";
    results[1].label = "Independent integer ADD pair (dual-issue)";
    results[1].ops_per_sec = bench_indep_add(loops);
    results[1].cycles_per_op = 19.0 / 32.0; /* 16 cyc for 32 ops + 3 ovh */
    results[1].freq_mhz =
        results[1].ops_per_sec * results[1].cycles_per_op / 1e6;
    results[1].model_note = "A7: 0.5 cyc/op dual-issue ALU";

    results[2].name = "dep_mul";
    results[2].label = "Dependent integer MUL";
    results[2].ops_per_sec = bench_dep_mul(loops);
    results[2].cycles_per_op =
        (16.0 * 3.0 + 3.0) / 16.0; /* 3-cyc-latency dep MUL */
    results[2].freq_mhz =
        results[2].ops_per_sec * results[2].cycles_per_op / 1e6;
    results[2].model_note = "A7: 3-cyc latency dep MUL";

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM(
        "model",
        "Cortex-A7 in-order, 3-cyc outer-loop overhead, 16-op inner block");
    ADD_PARAM_NUM("loops", (double)loops);

    cJSON *patterns = cJSON_CreateObject();
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        cJSON_AddItemToObject(patterns, results[i].name,
                              result_to_json(&results[i]));
    }
    cJSON_AddItemToObject(j_inner, "patterns", patterns);

    /* Consensus: median of the three clock estimates (robust to one
     * off-model estimator; in practice indep_add tends to under-shoot
     * because A7's second ALU pipe doesn't always accept ADD-imm). Plus
     * the min-to-max spread as a confidence indicator. */
    double v[3] = {results[0].freq_mhz, results[1].freq_mhz,
                   results[2].freq_mhz};
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (v[b] < v[a]) {
                double t = v[a];
                v[a] = v[b];
                v[b] = t;
            }
    double median = v[1];
    double spread_pct = (v[2] - v[0]) / median * 100.0;
    ADD_PARAM_NUM("consensus_freq_mhz", median);
    ADD_PARAM_NUM("spread_pct", spread_pct);
    return j_inner;
}

static void print_cpubench_usage(void) {
    printf("Usage: ipctool cpubench [--json] [--loops N]\n"
           "\n"
           "Triangulate CPU clock by running three tight inline-asm patterns\n"
           "(dependent ADD, independent ADD pair, dependent MUL) and\n"
           "back-calculating MHz from the known Cortex-A7 throughput.\n"
           "\n"
           "Three independent timing models converge to within ~2%% on a\n"
           "healthy chip; large divergence suggests CPU contention (kill\n"
           "majestic/encoder first) or a non-A7 core.\n"
           "\n"
           "Output is YAML by default; --json emits JSON.\n"
           "Default loops = 30000000 (~5-7 s of runtime on Cortex-A7 @ ~900 "
           "MHz).\n");
}

int cpubench_cmd(int argc, char **argv) {
    bool want_json = false;
    uint64_t loops = 30000000ULL;

    const struct option long_options[] = {
        {"json", no_argument, NULL, 'j'},
        {"loops", required_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "jl:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'j':
            want_json = true;
            break;
        case 'l':
            loops = strtoull(optarg, NULL, 10);
            if (loops < 1000000ULL) {
                fprintf(stderr, "cpubench: --loops must be >= 1000000\n");
                return EXIT_FAILURE;
            }
            break;
        case 'h':
            print_cpubench_usage();
            return EXIT_SUCCESS;
        default:
            print_cpubench_usage();
            return EXIT_FAILURE;
        }
    }

    cJSON *bench = build_cpubench_json(loops);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "cpubench", bench);

    char *out = want_json ? cJSON_Print(root) : cYAML_Print(root);
    if (out) {
        printf("%s", out);
        if (want_json)
            printf("\n");
        free(out);
    }
    cJSON_Delete(root);
    return EXIT_SUCCESS;
}

#else /* !__arm__ */

int cpubench_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fprintf(stderr,
            "cpubench: only supported on ARM builds (the inline-asm patterns\n"
            "          rely on Cortex-A7 throughput numbers).\n");
    return EXIT_FAILURE;
}

#endif /* __arm__ */
