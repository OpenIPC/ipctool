/* `ipctool cryptobench` — what crypto this SoC can sustain at packet size.
 *
 * Asked for in ipctool#186, after ChaCha20-Poly1305 measured 2.3x AES-128-GCM
 * on a GK7202V300 — the opposite of the server-side ordering, because these
 * cores have no AES instructions and no carry-less multiply. Two things
 * decide the answer on this hardware and neither is visible from a bulk
 * throughput figure:
 *
 *   - AES-GCM pays for GHASH, which without a carry-less multiply is 32 table
 *     lookups and shifts per block, on top of an AES that is itself
 *     table-driven. ChaCha20 and Poly1305 are 32-bit add-rotate-xor and
 *     26-bit-limb arithmetic, which is what these cores are good at.
 *   - Packet-sized buffers, not bulk. At ~1200 bytes the per-call setup is a
 *     real share of the cost, and that is the size a streamer actually seals.
 *
 * THE HARDWARE HALF IS THE OTHER HALF OF THE ANSWER. HiSilicon and Goke parts
 * carry a Cipher engine on /dev/cipher, and the obvious hope is that it makes
 * AES-GCM cheap again. It does not: the gen-4 block has no GCM or CCM mode at
 * all, which this probes rather than assumes. What it can do is AES-CTR, the
 * confidentiality half — so the engine cannot seal, and the authentication
 * half would still be GHASH on the CPU. Measuring AES-CTR anyway is worth it
 * because it says how much of AES-GCM could ever be offloaded, and because
 * the per-ioctl floor (the 16-byte row) explains why the engine is a poor
 * bargain per packet and a good one per burst.
 *
 * READING THE OUTPUT. Every row carries both clocks. CLOCK_MONOTONIC answers
 * "how long did it take"; CLOCK_THREAD_CPUTIME_ID answers "how much core did
 * it cost", and on the hardware rows they differ by about a factor of two
 * because the driver sleeps on its completion interrupt and hands the core
 * back. Quoting one of them alone hides the entire point. `cpu_wall` near
 * 1.00 on a hardware row means the run was fighting something else for the
 * CPU — stop majestic and try again.
 *
 * Nothing is printed between timed blocks: buffering the results and printing
 * once at the end is not tidiness, it is because a single printf to a serial
 * console or an ssh pipe between two identical runs has been measured making
 * the second one read twice as slow.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "chipid.h"
#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "crypto/aes.h"
#include "crypto/chachapoly.h"
#include "crypto/gcm.h"
#include "crypto/hisi_cipher.h"
#include "cryptobench.h"
#include "tools.h"

/* The engine's own per-packet ceiling, which the software rows share so that
 * every row in one run is the same size. */
#define MAX_PACKET HISI_CIPHER_MAX_LEN

static double now_wall(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static double now_cpu(void) {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Keeps the optimiser from deleting work whose result nothing reads. */
static volatile uint8_t g_sink;

/* ---- known answers ----------------------------------------------------- */

/* A benchmark of a wrong implementation is worth nothing, and a wrong one is
 * invisible from the outside because ciphertext is supposed to look like
 * noise. Each primitive is checked against a published vector before it is
 * timed, and a row that fails is reported unverified with no numbers rather
 * than as a fast wrong answer.
 *
 * The tag covers the ciphertext in all three constructions, so checking the
 * tag checks both halves. */

static bool verify_gcm128(void) {
    /* GCM spec test case 2: zero key, zero IV, one zero block. */
    static const uint8_t key[16] = {0};
    static const uint8_t nonce[12] = {0};
    static const uint8_t want_ct[16] = {0x03, 0x88, 0xda, 0xce, 0x60, 0xb6,
                                        0xa3, 0x92, 0xf3, 0x28, 0xc2, 0xb9,
                                        0x71, 0xb2, 0xfe, 0x78};
    static const uint8_t want_tag[16] = {0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec,
                                         0x13, 0xbd, 0xf5, 0x3a, 0x67, 0xb2,
                                         0x12, 0x57, 0xbd, 0xdf};
    gcm_ctx c;
    uint8_t buf[16] = {0}, tag[16];
    if (gcm_setkey(&c, key, 128) != 0)
        return false;
    gcm_seal(&c, nonce, NULL, 0, buf, sizeof(buf), tag);
    return memcmp(buf, want_ct, 16) == 0 && memcmp(tag, want_tag, 16) == 0;
}

static bool verify_gcm256(void) {
    /* GCM spec test case 14: zero 256-bit key, zero IV, one zero block. */
    static const uint8_t key[32] = {0};
    static const uint8_t nonce[12] = {0};
    static const uint8_t want_ct[16] = {0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60,
                                        0x6b, 0x6e, 0x07, 0x4e, 0xc5, 0xd3,
                                        0xba, 0xf3, 0x9d, 0x18};
    static const uint8_t want_tag[16] = {0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99,
                                         0x6b, 0xf0, 0x26, 0x5b, 0x98, 0xb5,
                                         0xd4, 0x8a, 0xb9, 0x19};
    gcm_ctx c;
    uint8_t buf[16] = {0}, tag[16];
    if (gcm_setkey(&c, key, 256) != 0)
        return false;
    gcm_seal(&c, nonce, NULL, 0, buf, sizeof(buf), tag);
    return memcmp(buf, want_ct, 16) == 0 && memcmp(tag, want_tag, 16) == 0;
}

static bool verify_chachapoly(void) {
    /* RFC 8439 section 2.8.2. */
    static const uint8_t key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
        0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
        0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
    static const uint8_t nonce[12] = {0x07, 0x00, 0x00, 0x00, 0x40, 0x41,
                                      0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    static const uint8_t aad[12] = {0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1,
                                    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};
    static const char pt[] =
        "Ladies and Gentlemen of the class of '99: If I could offer you only "
        "one tip for the future, sunscreen would be it.";
    static const uint8_t want_tag[16] = {0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09,
                                         0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb,
                                         0xd0, 0x60, 0x06, 0x91};
    uint8_t buf[128], tag[16];
    const size_t len = sizeof(pt) - 1;
    memcpy(buf, pt, len);
    chachapoly_seal(key, nonce, aad, sizeof(aad), buf, len, tag);
    return memcmp(tag, want_tag, 16) == 0;
}

/* The AES-128 key SP 800-38A uses, shared by the engine's vectors and its
 * benchmark rows. */
static const uint8_t g_hw_key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae,
                                     0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
                                     0x09, 0xcf, 0x4f, 0x3c};

/* The engine, against NIST SP 800-38A F.5.1 and then across a 32-bit counter
 * wrap. The second one is not optional: a block that carries only the low 32
 * bits of the counter agrees with software for every IV that is not near a
 * wrap, which is nearly all of them, so it would pass the first vector, ship,
 * and then disagree on one packet in millions with nothing to say so. */
static bool verify_hw_ctr(hisi_cipher *hw) {
    static const uint8_t iv[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
                                   0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
                                   0xfc, 0xfd, 0xfe, 0xff};
    static const uint8_t plain[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40,
                                      0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11,
                                      0x73, 0x93, 0x17, 0x2a};
    static const uint8_t want[16] = {0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20,
                                     0xe3, 0x26, 0x1b, 0xef, 0x68, 0x64,
                                     0x99, 0x0d, 0xb6, 0xce};
    uint8_t buf[16];
    memcpy(buf, plain, sizeof(buf));
    if (!hisi_cipher_ctr(hw, g_hw_key, iv, buf, sizeof(buf)))
        return false;
    if (memcmp(buf, want, sizeof(want)) != 0)
        return false;

    static const uint8_t wrap_iv[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae,
                                        0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
                                        0xff, 0xff, 0xff, 0xff};
    static const uint8_t wrap_want[32] = {
        0xa9, 0xd5, 0xcc, 0x9c, 0xa3, 0x90, 0xbf, 0x56, 0x0f, 0x26, 0x0d,
        0x21, 0x31, 0x95, 0xf5, 0xa3, 0xb3, 0x34, 0x2b, 0xcf, 0xf2, 0x44,
        0x50, 0xc2, 0xfa, 0x5c, 0x65, 0x9a, 0x07, 0x96, 0x30, 0x22};
    uint8_t wrap[32];
    memset(wrap, 0, sizeof(wrap));
    if (!hisi_cipher_ctr(hw, g_hw_key, wrap_iv, wrap, sizeof(wrap)))
        return false;
    return memcmp(wrap, wrap_want, sizeof(wrap_want)) == 0;
}

/* The batched command needs a known answer of its own, and specifically one
 * with a DIFFERENT IV PER PACKAGE. A driver that accepted the burst but
 * applied the channel's single IV to every package would return correct
 * ciphertext for package 0 and silent nonsense for the other fourteen — which
 * is exactly the shape of the batch benchmark's real jobs, and would have been
 * reported as a fast verified row. Each package is checked against the
 * software AES-CTR in this same binary, which the F.5.1 vector above has
 * already pinned to the standard.
 *
 * `len` is the length the benchmark will actually use, and is deliberately not
 * required to be a multiple of the block: the single-packet path pads a short
 * tail itself while this one hands the length straight to the driver, so any
 * disagreement between them about a partial final block surfaces here as a
 * failed vector rather than as a wrong number in the table. */
static bool verify_hw_ctr_batch(hisi_cipher *hw, size_t len) {
    if (hw->batch_max == 0 || len == 0 || len > MAX_PACKET)
        return false;

    static uint8_t bufs[HISI_CIPHER_BATCH_MAX][MAX_PACKET];
    static uint8_t want[HISI_CIPHER_BATCH_MAX][MAX_PACKET];
    static uint8_t ivs[HISI_CIPHER_BATCH_MAX][16];
    hisi_cipher_job jobs[HISI_CIPHER_BATCH_MAX];

    aes_ctx aes;
    if (aes_setkey_enc(&aes, g_hw_key, 128) != 0)
        return false;

    const unsigned depth = hw->batch_max;
    for (unsigned i = 0; i < depth; i++) {
        /* Distinct plaintext as well as distinct IV, so a driver that
         * processed the same package fifteen times cannot pass either. */
        for (size_t j = 0; j < len; j++)
            bufs[i][j] = (uint8_t)(j + i * 31u);
        memcpy(want[i], bufs[i], len);

        memset(ivs[i], 0, sizeof(ivs[i]));
        ivs[i][0] = (uint8_t)(0xa0 + i);
        ivs[i][15] = (uint8_t)(i * 7u + 1u);

        uint8_t counter[16];
        memcpy(counter, ivs[i], sizeof(counter));
        aes_ctr_xcrypt(&aes, counter, want[i], len);

        jobs[i].iv = ivs[i];
        jobs[i].buf = bufs[i];
        jobs[i].len = len;
    }

    if (!hisi_cipher_ctr_batch(hw, g_hw_key, jobs, depth))
        return false;
    for (unsigned i = 0; i < depth; i++)
        if (memcmp(bufs[i], want[i], len) != 0)
            return false;
    return true;
}

/* ---- rows -------------------------------------------------------------- */

struct row {
    bool verified;
    bool ran;
    double wall_us; /* per packet */
    double cpu_us;
    double mb_per_sec; /* wall, payload bytes only */
};

/* These are timings, not measurements of a constant: the run-to-run spread is
 * percents, so printing a double's full 17 significant digits would suggest a
 * precision that is not there and makes the table unreadable when pasted. */
static double rnd(double v, int places) {
    double scale = 1;
    for (int i = 0; i < places; i++)
        scale *= 10;
    return (double)(long long)(v * scale + (v < 0 ? -0.5 : 0.5)) / scale;
}

static cJSON *row_to_json(const struct row *r) {
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(j_inner, "verified", cJSON_CreateBool(r->verified));
    if (!r->ran)
        return j_inner;
    ADD_PARAM_NUM("wall_us", rnd(r->wall_us, 3));
    ADD_PARAM_NUM("cpu_us", rnd(r->cpu_us, 3));
    ADD_PARAM_NUM("cpu_wall",
                  rnd(r->wall_us > 0 ? r->cpu_us / r->wall_us : 0, 3));
    ADD_PARAM_NUM("mb_per_sec", rnd(r->mb_per_sec, 2));
    return j_inner;
}

static cJSON *batch_row_json(const struct row *r, unsigned depth) {
    cJSON *j_inner = row_to_json(r);
    ADD_PARAM_NUM("depth", depth);
    return j_inner;
}

static cJSON *batch_absent_json(void) {
    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM("status", "unsupported");
    ADD_PARAM("note",
              "driver predates the batched ioctl (OpenIPC/openhisilicon#217)");
    return j_inner;
}

static void finish_row(struct row *r, double w0, double c0, double w1,
                       double c1, unsigned iters, size_t bytes) {
    const double wall = (w1 - w0) / iters;
    const double cpu = (c1 - c0) / iters;
    r->ran = true;
    r->wall_us = wall * 1e6;
    r->cpu_us = cpu * 1e6;
    r->mb_per_sec = wall > 0 ? (double)bytes / wall / 1048576.0 : 0;
}

/* ---- software ---------------------------------------------------------- */

static void bench_gcm(struct row *r, unsigned bits, uint8_t *buf, size_t bytes,
                      unsigned iters) {
    r->verified = (bits == 128) ? verify_gcm128() : verify_gcm256();
    if (!r->verified)
        return;

    uint8_t key[32];
    memset(key, 0x2b, sizeof(key));
    uint8_t nonce[12];
    memset(nonce, 0x07, sizeof(nonce));
    uint8_t tag[16];
    gcm_ctx c;
    gcm_setkey(&c, key, bits);

    const double w0 = now_wall(), c0 = now_cpu();
    for (unsigned i = 0; i < iters; i++)
        gcm_seal(&c, nonce, NULL, 0, buf, bytes, tag);
    const double c1 = now_cpu(), w1 = now_wall();
    g_sink = tag[0];
    finish_row(r, w0, c0, w1, c1, iters, bytes);
}

static void bench_chachapoly(struct row *r, uint8_t *buf, size_t bytes,
                             unsigned iters) {
    r->verified = verify_chachapoly();
    if (!r->verified)
        return;

    uint8_t key[32];
    memset(key, 0x2b, sizeof(key));
    uint8_t nonce[12];
    memset(nonce, 0x07, sizeof(nonce));
    uint8_t tag[16];

    const double w0 = now_wall(), c0 = now_cpu();
    for (unsigned i = 0; i < iters; i++)
        chachapoly_seal(key, nonce, NULL, 0, buf, bytes, tag);
    const double c1 = now_cpu(), w1 = now_wall();
    g_sink = tag[0];
    finish_row(r, w0, c0, w1, c1, iters, bytes);
}

/* ---- hardware ---------------------------------------------------------- */

static void bench_hw_single(struct row *r, hisi_cipher *hw, uint8_t *buf,
                            size_t bytes, unsigned iters) {
    uint8_t iv[16];
    memset(iv, 0x11, sizeof(iv));

    const double w0 = now_wall(), c0 = now_cpu();
    for (unsigned i = 0; i < iters; i++) {
        if (!hisi_cipher_ctr(hw, g_hw_key, iv, buf, bytes)) {
            r->ran = false;
            return;
        }
    }
    const double c1 = now_cpu(), w1 = now_wall();
    g_sink = buf[0];
    finish_row(r, w0, c0, w1, c1, iters, bytes);
}

static void bench_hw_batch(struct row *r, hisi_cipher *hw, size_t bytes,
                           unsigned iters, unsigned depth) {
    static uint8_t bufs[HISI_CIPHER_BATCH_MAX][MAX_PACKET];
    static uint8_t ivs[HISI_CIPHER_BATCH_MAX][16];
    hisi_cipher_job jobs[HISI_CIPHER_BATCH_MAX];

    for (unsigned i = 0; i < depth; i++) {
        memset(bufs[i], (int)i + 1, bytes);
        memset(ivs[i], (int)i + 0x40, sizeof(ivs[i]));
        jobs[i].iv = ivs[i];
        jobs[i].buf = bufs[i];
        jobs[i].len = bytes;
    }

    const unsigned rounds = iters / depth ? iters / depth : 1;
    const double w0 = now_wall(), c0 = now_cpu();
    for (unsigned i = 0; i < rounds; i++) {
        if (!hisi_cipher_ctr_batch(hw, g_hw_key, jobs, depth)) {
            r->ran = false;
            return;
        }
    }
    const double c1 = now_cpu(), w1 = now_wall();
    g_sink = bufs[0][0];
    /* Per packet, not per call — that is the number the software rows are in.
     */
    finish_row(r, w0, c0, w1, c1, rounds * depth, bytes);
}

static cJSON *build_hw_json(size_t bytes, unsigned iters) {
    hisi_cipher hw;
    if (!hisi_cipher_open(&hw)) {
        cJSON *j_inner = cJSON_CreateObject();
        ADD_PARAM("device", "absent");
        ADD_PARAM("note", "no " HISI_CIPHER_DEV
                          " — no cipher engine, or its module is not loaded");
        return j_inner;
    }

    const bool aead = hisi_cipher_supports_gcm(&hw);

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM("device", HISI_CIPHER_DEV);
    ADD_PARAM("aead", aead ? "supported, not measured" : "unsupported");
    ADD_PARAM("aead_note",
              aead ? "this part accepts AES-GCM, which no part available when "
                     "this was written did; the sealing benchmark is not "
                     "implemented, because an unverifiable number is worse "
                     "than a missing one. Please open an issue"
                   : "block refuses AES-GCM, so there is no hardware sealing "
                     "on this part and only the AES-CTR half could be "
                     "offloaded");

    struct row single = {false, false, 0, 0, 0};
    struct row floor = {false, false, 0, 0, 0};
    struct row batch = {false, false, 0, 0, 0};

    single.verified = verify_hw_ctr(&hw);
    floor.verified = single.verified;
    /* Not inherited: the batched path is a different command with different
     * per-package state, and passing the single-packet vector says nothing
     * about it. */
    batch.verified = single.verified && verify_hw_ctr_batch(&hw, bytes);

    if (single.verified) {
        static uint8_t buf[MAX_PACKET];
        memset(buf, 0x5a, sizeof(buf));
        bench_hw_single(&single, &hw, buf, bytes, iters);
        /* One block through the same path: nearly all of it is the syscall
         * and the channel programming, so this is the fixed cost per trip
         * and the number to reason with before writing any code against
         * this engine. */
        bench_hw_single(&floor, &hw, buf, 16, iters);
        if (batch.verified)
            bench_hw_batch(&batch, &hw, bytes, iters, hw.batch_max);
    }

    cJSON *ctr = cJSON_CreateObject();
    cJSON_AddItemToObject(ctr, "single", row_to_json(&single));
    cJSON_AddItemToObject(ctr, "ioctl_floor_16b", row_to_json(&floor));
    cJSON_AddItemToObject(ctr, "batched",
                          hw.batch_max > 0
                              ? batch_row_json(&batch, hw.batch_max)
                              : batch_absent_json());
    cJSON_AddItemToObject(j_inner, "aes_128_ctr", ctr);

    hisi_cipher_close(&hw);
    return j_inner;
}

/* ---- the command ------------------------------------------------------- */

static cJSON *software_json(const struct row *aes128, const struct row *aes256,
                            const struct row *chacha) {
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(j_inner, "aes_128_gcm", row_to_json(aes128));
    cJSON_AddItemToObject(j_inner, "aes_256_gcm", row_to_json(aes256));
    cJSON_AddItemToObject(j_inner, "chacha20_poly1305", row_to_json(chacha));
    if (aes128->ran && chacha->ran && chacha->wall_us > 0)
        /* The ratio ipctool#186 is about: above 1 means ChaCha20 wins. */
        ADD_PARAM_NUM("chacha_vs_aes128",
                      rnd(aes128->wall_us / chacha->wall_us, 2));
    return j_inner;
}

static cJSON *build_cryptobench_json(size_t bytes, unsigned iters,
                                     bool want_hw) {
    static uint8_t buf[MAX_PACKET];
    memset(buf, 0x5a, sizeof(buf));

    struct row aes128 = {false, false, 0, 0, 0};
    struct row aes256 = {false, false, 0, 0, 0};
    struct row chacha = {false, false, 0, 0, 0};

    bench_gcm(&aes128, 128, buf, bytes, iters);
    bench_gcm(&aes256, 256, buf, bytes, iters);
    bench_chachapoly(&chacha, buf, bytes, iters);

    cJSON *j_inner = cJSON_CreateObject();
    ADD_PARAM_NUM("packet_bytes", (double)bytes);
    ADD_PARAM_NUM("iters", (double)iters);

    cJSON_AddItemToObject(j_inner, "software",
                          software_json(&aes128, &aes256, &chacha));

    if (want_hw)
        cJSON_AddItemToObject(j_inner, "hardware", build_hw_json(bytes, iters));
    return j_inner;
}

/* strtoul() alone would take "-1" as ULONG_MAX and "100junk" as 100, and the
 * narrowing to unsigned then hides the first of those: --iters -1 becomes
 * UINT_MAX and the tool sits there for a week. Require the whole argument to
 * be a number and range-check before narrowing. */
static bool parse_bounded(const char *arg, unsigned long lo, unsigned long hi,
                          unsigned long *out) {
    if (!arg || !*arg)
        return false;
    errno = 0;
    char *end = NULL;
    const unsigned long v = strtoul(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0')
        return false;
    /* strtoul() accepts a leading '-' and wraps it; reject the sign itself. */
    if (strchr(arg, '-'))
        return false;
    if (v < lo || v > hi)
        return false;
    *out = v;
    return true;
}

static void print_cryptobench_usage(void) {
    printf(
        "Usage: ipctool cryptobench [--json] [--bytes N] [--iters N] "
        "[--no-hw]\n"
        "\n"
        "Seal throughput at packet size, in software and on the SoC's cipher\n"
        "engine where it has one.\n"
        "\n"
        "Software rows are AES-128-GCM, AES-256-GCM and ChaCha20-Poly1305,\n"
        "each checked against a published test vector before it is timed; a\n"
        "row that fails its vector is reported unverified with no numbers.\n"
        "\n"
        "Hardware rows come from /dev/cipher and are AES-CTR only, because\n"
        "the gen-4 HiSilicon/Goke block has no GCM or CCM mode — which the\n"
        "probe asks the driver rather than assuming. The 16-byte row is the\n"
        "per-ioctl floor: nearly all of it is syscall and channel setup, so\n"
        "it is the number to reason with before building on this engine.\n"
        "\n"
        "Every row reports both clocks. cpu_wall well below 1 on a hardware\n"
        "row is the driver sleeping on its completion interrupt and handing\n"
        "the core back, which is the point of it; cpu_wall near 1 there means\n"
        "the run was contending for CPU — stop majestic and repeat.\n"
        "\n"
        "Output is YAML by default; --json emits JSON.\n"
        "Defaults: --bytes 1200 (a packet, not bulk), --iters 20000.\n");
}

int cryptobench_cmd(int argc, char **argv) {
    bool want_json = false, want_hw = true;
    size_t bytes = 1200;
    unsigned iters = 20000;

    const struct option long_options[] = {
        {"json", no_argument, NULL, 'j'},
        {"bytes", required_argument, NULL, 'b'},
        {"iters", required_argument, NULL, 'i'},
        {"no-hw", no_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "jb:i:nh", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 'j':
            want_json = true;
            break;
        case 'b': {
            unsigned long v;
            if (!parse_bounded(optarg, 16, MAX_PACKET, &v)) {
                fprintf(stderr,
                        "cryptobench: --bytes must be a number 16..%d (the "
                        "engine's own limit)\n",
                        MAX_PACKET);
                return EXIT_FAILURE;
            }
            bytes = (size_t)v;
            break;
        }
        case 'i': {
            unsigned long v;
            if (!parse_bounded(optarg, 100, 100000000UL, &v)) {
                fprintf(stderr,
                        "cryptobench: --iters must be a number 100..%lu\n",
                        100000000UL);
                return EXIT_FAILURE;
            }
            iters = (unsigned)v;
            break;
        }
        case 'n':
            want_hw = false;
            break;
        case 'h':
            print_cryptobench_usage();
            return EXIT_SUCCESS;
        default:
            print_cryptobench_usage();
            return EXIT_FAILURE;
        }
    }

    cJSON *bench = build_cryptobench_json(bytes, iters, want_hw);

    /* A row of numbers is only useful in the per-SoC table #186 asks for if it
     * says which SoC. Same tagging membw does; absent on a host build, where
     * chip detection never ran. */
    const char *chip = getchipname();
    if (chip)
        cJSON_AddItemToObject(bench, "chip", cJSON_CreateString(chip));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "cryptobench", bench);

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
