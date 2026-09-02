/* See aes.h for why this is T-table based and why the tables are generated
 * rather than stored. */

#include <string.h>

#include "aes.h"

/* Forward S-box, and the four T-tables built from it. FT0 holds
 * MixColumns(SubBytes(x)) for one byte position; FT1..FT3 are FT0 rotated, so
 * a round is four table lookups and three XORs per column. */
static uint8_t FSb[256];
static uint32_t FT0[256], FT1[256], FT2[256], FT3[256];
static uint32_t RCON[10];
static int tables_ready = 0;

#define XTIME(x) (((x) << 1) ^ (((x) & 0x80) ? 0x1B : 0x00))

static uint32_t rotl8(uint32_t v) { return (v << 8) | (v >> 24); }

static void gen_tables(void) {
    /* Log/alog over GF(2^8) with generator 3, the usual way to get the
     * multiplicative inverse the S-box is defined on. */
    uint8_t pow[256], log[256];
    uint8_t x = 1;
    for (int i = 0; i < 256; i++) {
        pow[i] = x;
        log[x] = (uint8_t)i;
        x = (uint8_t)(x ^ XTIME(x)); /* x *= 3 */
    }

    uint32_t rc = 1;
    for (int i = 0; i < 10; i++) {
        RCON[i] = rc;
        rc = (uint32_t)((uint8_t)XTIME((uint8_t)rc));
    }

    FSb[0x00] = 0x63;
    for (int i = 1; i < 256; i++) {
        uint8_t inv = pow[255 - log[i]];
        uint8_t s = inv;
        /* The affine transform: s ^= rotl(s,1..4), then ^ 0x63. */
        uint8_t t = s;
        for (int j = 0; j < 4; j++) {
            t = (uint8_t)((t << 1) | (t >> 7));
            s ^= t;
        }
        FSb[i] = (uint8_t)(s ^ 0x63);
    }

    for (int i = 0; i < 256; i++) {
        uint8_t s = FSb[i];
        uint8_t s2 = (uint8_t)XTIME(s);
        uint8_t s3 = (uint8_t)(s2 ^ s);
        /* One MixColumns column, little-endian word order to match the
         * byte packing in encrypt_block below. */
        FT0[i] = ((uint32_t)s2) | ((uint32_t)s << 8) | ((uint32_t)s << 16) |
                 ((uint32_t)s3 << 24);
        FT1[i] = rotl8(FT0[i]);
        FT2[i] = rotl8(FT1[i]);
        FT3[i] = rotl8(FT2[i]);
    }
    tables_ready = 1;
}

static uint32_t get_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int aes_setkey_enc(aes_ctx *ctx, const uint8_t *key, unsigned bits) {
    if (!tables_ready)
        gen_tables();

    unsigned nk;
    switch (bits) {
    case 128:
        ctx->rounds = 10;
        nk = 4;
        break;
    case 256:
        ctx->rounds = 14;
        nk = 8;
        break;
    default:
        return -1;
    }

    for (unsigned i = 0; i < nk; i++)
        ctx->rk[i] = get_u32_le(key + i * 4);

    const unsigned total = 4u * (unsigned)(ctx->rounds + 1);
    for (unsigned i = nk; i < total; i++) {
        uint32_t t = ctx->rk[i - 1];
        if (i % nk == 0) {
            t = (t >> 8) | (t << 24); /* RotWord */
            t = ((uint32_t)FSb[t & 0xFF]) |
                ((uint32_t)FSb[(t >> 8) & 0xFF] << 8) |
                ((uint32_t)FSb[(t >> 16) & 0xFF] << 16) |
                ((uint32_t)FSb[(t >> 24) & 0xFF] << 24);
            t ^= RCON[i / nk - 1];
        } else if (nk > 6 && i % nk == 4) {
            t = ((uint32_t)FSb[t & 0xFF]) |
                ((uint32_t)FSb[(t >> 8) & 0xFF] << 8) |
                ((uint32_t)FSb[(t >> 16) & 0xFF] << 16) |
                ((uint32_t)FSb[(t >> 24) & 0xFF] << 24);
        }
        ctx->rk[i] = ctx->rk[i - nk] ^ t;
    }
    return 0;
}

#define AES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3)                             \
    do {                                                                       \
        (X0) = *rk++ ^ FT0[(Y0) & 0xFF] ^ FT1[((Y1) >> 8) & 0xFF] ^            \
               FT2[((Y2) >> 16) & 0xFF] ^ FT3[((Y3) >> 24) & 0xFF];            \
        (X1) = *rk++ ^ FT0[(Y1) & 0xFF] ^ FT1[((Y2) >> 8) & 0xFF] ^            \
               FT2[((Y3) >> 16) & 0xFF] ^ FT3[((Y0) >> 24) & 0xFF];            \
        (X2) = *rk++ ^ FT0[(Y2) & 0xFF] ^ FT1[((Y3) >> 8) & 0xFF] ^            \
               FT2[((Y0) >> 16) & 0xFF] ^ FT3[((Y1) >> 24) & 0xFF];            \
        (X3) = *rk++ ^ FT0[(Y3) & 0xFF] ^ FT1[((Y0) >> 8) & 0xFF] ^            \
               FT2[((Y1) >> 16) & 0xFF] ^ FT3[((Y2) >> 24) & 0xFF];            \
    } while (0)

void aes_encrypt_block(const aes_ctx *ctx, const uint8_t in[16],
                       uint8_t out[16]) {
    const uint32_t *rk = ctx->rk;
    uint32_t x0 = get_u32_le(in) ^ *rk++;
    uint32_t x1 = get_u32_le(in + 4) ^ *rk++;
    uint32_t x2 = get_u32_le(in + 8) ^ *rk++;
    uint32_t x3 = get_u32_le(in + 12) ^ *rk++;
    uint32_t y0, y1, y2, y3;

    for (int i = (ctx->rounds >> 1) - 1; i > 0; i--) {
        AES_FROUND(y0, y1, y2, y3, x0, x1, x2, x3);
        AES_FROUND(x0, x1, x2, x3, y0, y1, y2, y3);
    }
    AES_FROUND(y0, y1, y2, y3, x0, x1, x2, x3);

    /* Last round: SubBytes + ShiftRows, no MixColumns — so the S-box is read
     * out of the T-table's byte lane rather than through a separate table. */
    x0 = *rk++ ^ ((uint32_t)FSb[y0 & 0xFF]) ^
         ((uint32_t)FSb[(y1 >> 8) & 0xFF] << 8) ^
         ((uint32_t)FSb[(y2 >> 16) & 0xFF] << 16) ^
         ((uint32_t)FSb[(y3 >> 24) & 0xFF] << 24);
    x1 = *rk++ ^ ((uint32_t)FSb[y1 & 0xFF]) ^
         ((uint32_t)FSb[(y2 >> 8) & 0xFF] << 8) ^
         ((uint32_t)FSb[(y3 >> 16) & 0xFF] << 16) ^
         ((uint32_t)FSb[(y0 >> 24) & 0xFF] << 24);
    x2 = *rk++ ^ ((uint32_t)FSb[y2 & 0xFF]) ^
         ((uint32_t)FSb[(y3 >> 8) & 0xFF] << 8) ^
         ((uint32_t)FSb[(y0 >> 16) & 0xFF] << 16) ^
         ((uint32_t)FSb[(y1 >> 24) & 0xFF] << 24);
    x3 = *rk++ ^ ((uint32_t)FSb[y3 & 0xFF]) ^
         ((uint32_t)FSb[(y0 >> 8) & 0xFF] << 8) ^
         ((uint32_t)FSb[(y1 >> 16) & 0xFF] << 16) ^
         ((uint32_t)FSb[(y2 >> 24) & 0xFF] << 24);

    put_u32_le(out, x0);
    put_u32_le(out + 4, x1);
    put_u32_le(out + 8, x2);
    put_u32_le(out + 12, x3);
}

void aes_ctr_xcrypt(aes_ctx *ctx, uint8_t counter[16], uint8_t *buf,
                    size_t len) {
    uint8_t ks[16];
    size_t off = 0;
    while (off < len) {
        aes_encrypt_block(ctx, counter, ks);
        for (int i = 15; i >= 0; i--)
            if (++counter[i] != 0)
                break;
        size_t n = len - off < 16 ? len - off : 16;
        for (size_t i = 0; i < n; i++)
            buf[off + i] ^= ks[i];
        off += n;
    }
}
