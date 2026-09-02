/* AES-GCM sealing.
 *
 * Only the seal (encrypt-and-tag) direction, and only 96-bit nonces — the one
 * shape every protocol that matters here uses, and the one the proposal in
 * ipctool#186 asks about.
 *
 * GHASH is the interesting half on these parts. A Cortex-A7 or a MIPS 24K has
 * no carry-less multiply, so the GF(2^128) product is done with 4-bit tables
 * — 32 table lookups and shifts per block — and it is why AES-GCM loses to
 * ChaCha20-Poly1305 on this hardware even though AES itself is not
 * catastrophic. Same method mbedTLS uses, so the number is comparable to what
 * a camera's own TLS stack would get.
 */

#ifndef CRYPTO_GCM_H
#define CRYPTO_GCM_H

#include <stddef.h>
#include <stdint.h>

#include "aes.h"

typedef struct {
    aes_ctx aes;
    uint64_t HL[16], HH[16];
} gcm_ctx;

/* `bits` is 128 or 256. Returns 0, or -1 on an unsupported key size. */
int gcm_setkey(gcm_ctx *ctx, const uint8_t *key, unsigned bits);

/* Encrypt `len` bytes in place and write the 16-byte tag.
 * `nonce` is 12 bytes. `aad` may be NULL when `aad_len` is 0. */
void gcm_seal(gcm_ctx *ctx, const uint8_t nonce[12], const uint8_t *aad,
              size_t aad_len, uint8_t *buf, size_t len, uint8_t tag[16]);

#endif /* CRYPTO_GCM_H */
