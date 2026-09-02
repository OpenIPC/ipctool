/* See gcm.h. GHASH follows the 4-bit-table method mbedTLS uses, so the
 * throughput this reports is comparable with a camera's own TLS stack. */

#include <string.h>

#include "gcm.h"

/* Reduction values for the low nibble shifted out of the accumulator, i.e.
 * x^128 + x^7 + x^2 + x + 1 folded back four bits at a time. */
static const uint16_t last4[16] = {
    0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
    0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0};

static uint64_t get_u64_be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}

static void put_u64_be(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

int gcm_setkey(gcm_ctx *ctx, const uint8_t *key, unsigned bits) {
    if (aes_setkey_enc(&ctx->aes, key, bits) != 0)
        return -1;

    uint8_t h[16] = {0};
    aes_encrypt_block(&ctx->aes, h, h);

    uint64_t hi = get_u64_be(h);
    uint64_t lo = get_u64_be(h + 8);

    ctx->HL[0] = 0;
    ctx->HH[0] = 0;
    ctx->HH[8] = hi;
    ctx->HL[8] = lo;

    for (int i = 4; i > 0; i >>= 1) {
        uint32_t t = (uint32_t)(lo & 1) * 0xe1000000u;
        lo = (lo >> 1) | (hi << 63);
        hi = (hi >> 1) ^ ((uint64_t)t << 32);
        ctx->HL[i] = lo;
        ctx->HH[i] = hi;
    }
    for (int i = 2; i <= 8; i *= 2) {
        uint64_t vh = ctx->HH[i], vl = ctx->HL[i];
        for (int j = 1; j < i; j++) {
            ctx->HH[i + j] = vh ^ ctx->HH[j];
            ctx->HL[i + j] = vl ^ ctx->HL[j];
        }
    }
    return 0;
}

/* acc = acc * H over GF(2^128), acc in place. */
static void ghash_mult(const gcm_ctx *ctx, uint8_t acc[16]) {
    uint8_t lo = acc[15] & 0x0f;
    uint64_t zh = ctx->HH[lo], zl = ctx->HL[lo];

    for (int i = 15; i >= 0; i--) {
        lo = acc[i] & 0x0f;
        uint8_t hi = (uint8_t)((acc[i] >> 4) & 0x0f);
        uint8_t rem;

        if (i != 15) {
            rem = (uint8_t)(zl & 0x0f);
            zl = (zh << 60) | (zl >> 4);
            zh = zh >> 4;
            zh ^= (uint64_t)last4[rem] << 48;
            zh ^= ctx->HH[lo];
            zl ^= ctx->HL[lo];
        }
        rem = (uint8_t)(zl & 0x0f);
        zl = (zh << 60) | (zl >> 4);
        zh = zh >> 4;
        zh ^= (uint64_t)last4[rem] << 48;
        zh ^= ctx->HH[hi];
        zl ^= ctx->HL[hi];
    }
    put_u64_be(acc, zh);
    put_u64_be(acc + 8, zl);
}

static void ghash_update(const gcm_ctx *ctx, uint8_t acc[16],
                         const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t n = len < 16 ? len : 16;
        for (size_t i = 0; i < n; i++)
            acc[i] ^= data[i];
        ghash_mult(ctx, acc);
        data += n;
        len -= n;
    }
}

void gcm_seal(gcm_ctx *ctx, const uint8_t nonce[12], const uint8_t *aad,
              size_t aad_len, uint8_t *buf, size_t len, uint8_t tag[16]) {
    /* 96-bit nonce: J0 is the nonce with a counter of 1 appended. */
    uint8_t j0[16];
    memcpy(j0, nonce, 12);
    j0[12] = 0;
    j0[13] = 0;
    j0[14] = 0;
    j0[15] = 1;

    /* The data runs from counter 2; counter 1 is kept for the tag. */
    uint8_t ctr[16];
    memcpy(ctr, j0, 16);
    ctr[15] = 2;
    aes_ctr_xcrypt(&ctx->aes, ctr, buf, len);

    uint8_t acc[16] = {0};
    if (aad_len > 0)
        ghash_update(ctx, acc, aad, aad_len);
    ghash_update(ctx, acc, buf, len);

    uint8_t lenblk[16];
    put_u64_be(lenblk, (uint64_t)aad_len * 8);
    put_u64_be(lenblk + 8, (uint64_t)len * 8);
    ghash_update(ctx, acc, lenblk, 16);

    uint8_t s[16];
    aes_encrypt_block(&ctx->aes, j0, s);
    for (int i = 0; i < 16; i++)
        tag[i] = (uint8_t)(s[i] ^ acc[i]);
}
