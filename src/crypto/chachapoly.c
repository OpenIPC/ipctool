/* ChaCha20-Poly1305 (RFC 8439), seal direction, one shot. See chachapoly.h. */

#include <string.h>

#include "chachapoly.h"

/* ---- ChaCha20 ---------------------------------------------------------- */

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

#define QR(a, b, c, d)                                                         \
    do {                                                                       \
        a += b;                                                                \
        d = rotl32(d ^ a, 16);                                                 \
        c += d;                                                                \
        b = rotl32(b ^ c, 12);                                                 \
        a += b;                                                                \
        d = rotl32(d ^ a, 8);                                                  \
        c += d;                                                                \
        b = rotl32(b ^ c, 7);                                                  \
    } while (0)

static void chacha20_block(const uint32_t in[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++)
        wr_le32(out + i * 4, x[i] + in[i]);
}

static void chacha20_init(uint32_t st[16], const uint8_t key[32],
                          const uint8_t nonce[12], uint32_t counter) {
    st[0] = 0x61707865;
    st[1] = 0x3320646e;
    st[2] = 0x79622d32;
    st[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        st[4 + i] = rd_le32(key + i * 4);
    st[12] = counter;
    for (int i = 0; i < 3; i++)
        st[13 + i] = rd_le32(nonce + i * 4);
}

static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, uint8_t *buf, size_t len) {
    uint32_t st[16];
    uint8_t ks[64];
    chacha20_init(st, key, nonce, counter);
    size_t off = 0;
    while (off < len) {
        chacha20_block(st, ks);
        st[12]++;
        size_t n = len - off < 64 ? len - off : 64;
        for (size_t i = 0; i < n; i++)
            buf[off + i] ^= ks[i];
        off += n;
    }
}

/* ---- Poly1305 ---------------------------------------------------------- */

/* Five 26-bit limbs, the usual arrangement for a 32-bit core: every partial
 * product fits a uint64 and the reduction is a shift and a multiply by 5. */
typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
} poly1305;

static void poly1305_init(poly1305 *st, const uint8_t key[32]) {
    st->r[0] = (rd_le32(key + 0)) & 0x3ffffff;
    st->r[1] = (rd_le32(key + 3) >> 2) & 0x3ffff03;
    st->r[2] = (rd_le32(key + 6) >> 4) & 0x3ffc0ff;
    st->r[3] = (rd_le32(key + 9) >> 6) & 0x3f03fff;
    st->r[4] = (rd_le32(key + 12) >> 8) & 0x00fffff;
    for (int i = 0; i < 5; i++)
        st->h[i] = 0;
    for (int i = 0; i < 4; i++)
        st->pad[i] = rd_le32(key + 16 + i * 4);
}

/* Whole 16-byte blocks only. Every block of the AEAD's MAC input is a full
 * one — see poly1305_pad_update — so the short-last-block case that plain
 * Poly1305 has does not arise here and the high bit is always implied. */
static void poly1305_blocks(poly1305 *st, const uint8_t *m, size_t bytes) {
    const uint32_t hibit = 1u << 24;
    const uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3],
                   r4 = st->r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3],
             h4 = st->h[4];

    while (bytes >= 16) {
        h0 += (rd_le32(m + 0)) & 0x3ffffff;
        h1 += (rd_le32(m + 3) >> 2) & 0x3ffffff;
        h2 += (rd_le32(m + 6) >> 4) & 0x3ffffff;
        h3 += (rd_le32(m + 9) >> 6) & 0x3ffffff;
        h4 += (rd_le32(m + 12) >> 8) | hibit;

        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 +
                      (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 +
                      (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 +
                      (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 +
                      (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 +
                      (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        uint32_t c = (uint32_t)(d0 >> 26);
        h0 = (uint32_t)d0 & 0x3ffffff;
        d1 += c;
        c = (uint32_t)(d1 >> 26);
        h1 = (uint32_t)d1 & 0x3ffffff;
        d2 += c;
        c = (uint32_t)(d2 >> 26);
        h2 = (uint32_t)d2 & 0x3ffffff;
        d3 += c;
        c = (uint32_t)(d3 >> 26);
        h3 = (uint32_t)d3 & 0x3ffffff;
        d4 += c;
        c = (uint32_t)(d4 >> 26);
        h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5;
        c = h0 >> 26;
        h0 &= 0x3ffffff;
        h1 += c;

        m += 16;
        bytes -= 16;
    }
    st->h[0] = h0;
    st->h[1] = h1;
    st->h[2] = h2;
    st->h[3] = h3;
    st->h[4] = h4;
}

static void poly1305_finish(poly1305 *st, uint8_t mac[16]) {
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3],
             h4 = st->h[4];

    uint32_t c = h1 >> 26;
    h1 &= 0x3ffffff;
    h2 += c;
    c = h2 >> 26;
    h2 &= 0x3ffffff;
    h3 += c;
    c = h3 >> 26;
    h3 &= 0x3ffffff;
    h4 += c;
    c = h4 >> 26;
    h4 &= 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;

    /* h + -p, kept only if it did not borrow. */
    uint32_t g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c;
    c = g1 >> 26;
    g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c;
    c = g2 >> 26;
    g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c;
    c = g3 >> 26;
    g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1; /* all ones when g >= 0 */
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* Back to four 32-bit words, then + pad. */
    h0 = (h0 | (h1 << 26)) & 0xffffffff;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;

    uint64_t f = (uint64_t)h0 + st->pad[0];
    h0 = (uint32_t)f;
    f = (uint64_t)h1 + st->pad[1] + (f >> 32);
    h1 = (uint32_t)f;
    f = (uint64_t)h2 + st->pad[2] + (f >> 32);
    h2 = (uint32_t)f;
    f = (uint64_t)h3 + st->pad[3] + (f >> 32);
    h3 = (uint32_t)f;

    wr_le32(mac + 0, h0);
    wr_le32(mac + 4, h1);
    wr_le32(mac + 8, h2);
    wr_le32(mac + 12, h3);
}

/* One field of the MAC input: the data, then zeros to the next 16-byte
 * boundary, exactly as RFC 8439 §2.8 pad16().
 *
 * This is NOT Poly1305's own handling of a short final block, which appends a
 * 0x01 byte and drops the implicit high bit. Getting those two confused is
 * invisible on any message that happens to be a multiple of 16 and wrong on
 * every other one — which is what the §2.8.2 vector, at 114 bytes, catches. */
static void poly1305_pad_update(poly1305 *st, const uint8_t *data, size_t len) {
    if (len == 0)
        return;
    size_t whole = len & ~(size_t)15;
    if (whole)
        poly1305_blocks(st, data, whole);
    size_t rem = len - whole;
    if (rem) {
        uint8_t blk[16] = {0};
        memcpy(blk, data + whole, rem);
        poly1305_blocks(st, blk, 16);
    }
}

/* ---- the AEAD ---------------------------------------------------------- */

void chachapoly_seal(const uint8_t key[32], const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len, uint8_t *buf,
                     size_t len, uint8_t tag[16]) {
    /* Block zero of the keystream is the one-time Poly1305 key; the data
     * starts at block one. */
    uint8_t polykey[64];
    uint32_t st0[16];
    chacha20_init(st0, key, nonce, 0);
    chacha20_block(st0, polykey);

    chacha20_xor(key, nonce, 1, buf, len);

    poly1305 p;
    poly1305_init(&p, polykey);
    poly1305_pad_update(&p, aad, aad_len);
    poly1305_pad_update(&p, buf, len);

    uint8_t lenblk[16];
    uint64_t a = aad_len, c = len;
    for (int i = 0; i < 8; i++) {
        lenblk[i] = (uint8_t)(a >> (8 * i));
        lenblk[8 + i] = (uint8_t)(c >> (8 * i));
    }
    poly1305_blocks(&p, lenblk, 16);
    poly1305_finish(&p, tag);
}
