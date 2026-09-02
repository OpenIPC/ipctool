/* AES-128/256 block encryption, encrypt direction only.
 *
 * GCM and CTR both build their keystream out of forward AES, so the inverse
 * cipher and its tables are not here — they would be a third of the code and
 * a quarter of the tables for something nothing in ipctool decrypts.
 *
 * The tables are BUILT AT STARTUP rather than stored, which is what mbedTLS
 * does by default (its MBEDTLS_AES_ROM_TABLES is off), and the reason matters
 * for what this file is for: `ipctool cryptobench` exists to compare AES
 * against ChaCha20 on cores with no AES instructions, so the AES here has to
 * be about as fast as the AES a camera actually runs. A compact
 * S-box-and-MixColumns implementation would be several times slower and would
 * inflate ChaCha20's lead into a number about this file rather than about the
 * silicon. Four 1 KB T-tables, generated once, is what the comparison needs.
 */

#ifndef CRYPTO_AES_H
#define CRYPTO_AES_H

#include <stddef.h>
#include <stdint.h>

#define AES_BLOCK_SIZE 16
#define AES_MAX_ROUNDS 14

typedef struct {
    uint32_t rk[4 * (AES_MAX_ROUNDS + 1)];
    int rounds;
} aes_ctx;

/* `bits` is 128 or 256. Returns 0, or -1 for an unsupported size. */
int aes_setkey_enc(aes_ctx *ctx, const uint8_t *key, unsigned bits);

/* One block, in != out is fine and in == out is fine. */
void aes_encrypt_block(const aes_ctx *ctx, const uint8_t in[16],
                       uint8_t out[16]);

/* AES-CTR over `len` bytes, in place, starting from `counter` and advancing
 * it over the whole 128-bit block. `counter` is updated. */
void aes_ctr_xcrypt(aes_ctx *ctx, uint8_t counter[16], uint8_t *buf,
                    size_t len);

#endif /* CRYPTO_AES_H */
