/* ChaCha20-Poly1305 sealing, RFC 8439.
 *
 * The reason this is in ipctool at all: on a core with no AES instructions
 * and no carry-less multiply, ChaCha20's 32-bit add-rotate-xor and
 * Poly1305's 26-bit-limb arithmetic are both things the CPU is actually good
 * at, while AES-GCM needs table lookups for the cipher and 32 more for every
 * block of GHASH. That is the comparison `ipctool cryptobench` measures.
 *
 * Seal only, one shot, which is what a packet-sized benchmark needs.
 */

#ifndef CRYPTO_CHACHAPOLY_H
#define CRYPTO_CHACHAPOLY_H

#include <stddef.h>
#include <stdint.h>

/* Encrypt `len` bytes in place under the 32-byte key and 12-byte nonce, and
 * write the 16-byte tag. `aad` may be NULL when `aad_len` is 0. */
void chachapoly_seal(const uint8_t key[32], const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len, uint8_t *buf,
                     size_t len, uint8_t tag[16]);

#endif /* CRYPTO_CHACHAPOLY_H */
