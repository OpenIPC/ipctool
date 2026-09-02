/* The HiSilicon/Goke Cipher engine through /dev/cipher.
 *
 * TRANSCRIBED, NOT LINKED. libhi_cipher.so is a thin wrapper over these
 * ioctls and most firmware images do not ship it, so a DT_NEEDED on it would
 * stop ipctool starting on the very cameras it is meant to inspect. Going
 * straight to the device also keeps the single static binary intact, which
 * dlopen would not.
 *
 * WHAT THE SILICON CAN AND CANNOT DO, because it decides what is worth
 * measuring: the gen-4 block does AES in ECB/CBC/CTR/CFB/OFB and carries a
 * separate hash engine, a TRNG and RSA — but NOT GCM or CCM.
 * CHIP_AES_CCM_GCM_SUPPORT is defined only for hi3569v100 in the vendor
 * sources, not for hi3516ev200 or hi3516cv500, so on every part OpenIPC ships
 * there is no hardware AEAD to benchmark at all. What can be measured is
 * AES-CTR, which is the confidentiality half of AES-GCM; the authentication
 * half would still be GHASH on the CPU, and the engine's own hash measured
 * five times slower than software.
 *
 * The command encoding is the vendor's own, not Linux's: direction in the top
 * two bits, then the payload size, then type 0x4D and the number. Because the
 * size is part of the number, a struct that does not match the driver's
 * produces a command it does not recognise rather than one it misreads.
 */

#ifndef CRYPTO_HISI_CIPHER_H
#define CRYPTO_HISI_CIPHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HISI_CIPHER_DEV "/dev/cipher"

/* Matches KAPI_SYMC_BATCH_MAX_PKG / KAPI_SYMC_BATCH_MAX_LEN in the driver. */
#define HISI_CIPHER_BATCH_MAX 15
#define HISI_CIPHER_MAX_LEN 2048

typedef struct {
    const uint8_t *iv; /* 16 bytes */
    uint8_t *buf;
    size_t len;
} hisi_cipher_job;

typedef struct {
    int fd;
    uint32_t chan;
    unsigned batch_max; /* 0 when the driver predates the batched ioctl */
} hisi_cipher;

/* Whether the block will accept an AEAD mode, asked rather than assumed.
 *
 * The answer on every part OpenIPC ships today is no — CHIP_AES_CCM_GCM_SUPPORT
 * is defined for hi3569v100 alone — but asking keeps that a measurement, so a
 * future part that does carry the mode reports itself instead of inheriting a
 * hardcoded "unsupported". */
bool hisi_cipher_supports_gcm(hisi_cipher *c);

/* Opens the device and takes a channel. False when the node is absent — the
 * ordinary state on most cameras, and not an error. */
bool hisi_cipher_open(hisi_cipher *c);
void hisi_cipher_close(hisi_cipher *c);

/* One packet, AES-128-CTR, in place. Lengths that are not a multiple of the
 * block are padded internally and only `len` bytes are written back, which is
 * invisible in CTR. */
bool hisi_cipher_ctr(hisi_cipher *c, const uint8_t key[16],
                     const uint8_t iv[16], uint8_t *buf, size_t len);

/* A burst under one ioctl, each job with its own IV. False if the driver has
 * no batched command, which `batch_max == 0` reports up front. */
bool hisi_cipher_ctr_batch(hisi_cipher *c, const uint8_t key[16],
                           const hisi_cipher_job *jobs, size_t count);

#endif /* CRYPTO_HISI_CIPHER_H */
