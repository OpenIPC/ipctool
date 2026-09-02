/* See hisi_cipher.h. */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hisi_cipher.h"

/* ---- the ABI ----------------------------------------------------------- */

#define HISI_IOC_W 1U
#define HISI_IOC_R 2U
#define hisi_ioc(dir, nr, size)                                                \
    (((dir) << 30) | ((unsigned)(size) << 16) | (0x4Du << 8) | (unsigned)(nr))

/* HI_UNF_CIPHER_* enumerators. */
#define HISI_ALG_AES 2
#define HISI_MODE_CTR 4
#define HISI_MODE_GCM 6
#define HISI_WIDTH_128 3
#define HISI_KEYLEN_128 0
#define HISI_IV_SET 1
#define HISI_OP_ENCRYPT_VIRT 0x10

typedef union {
    void *p;
    const void *cp;
    unsigned long long phy;
    unsigned int word[2];
} hisi_addr;

typedef struct {
    uint32_t id, reserve;
} hisi_symc_create;

typedef struct {
    uint32_t id, reserve;
} hisi_symc_destroy;

typedef struct {
    uint32_t id, hard_key, alg, mode, width, klen, sm1_round_num;
    uint8_t fkey[32], skey[32], iv[16];
    uint32_t ivlen, iv_usage, reserve;
    hisi_addr aad;
    uint32_t alen, tlen;
} hisi_symc_cfg;

typedef struct {
    uint32_t id, len, operation, last;
    hisi_addr in, out;
} hisi_symc_encrypt;

/* OpenIPC extension (openhisilicon baf0af2): a burst under one ioctl with an
 * IV per package, at user virtual addresses. */
typedef struct {
    hisi_addr src;
    hisi_addr dst;
    uint32_t length;
    uint32_t reserve;
    uint8_t iv[16];
} hisi_via_pkg;

typedef struct {
    uint32_t id;
    hisi_addr pkg;
    uint32_t pkg_num;
    uint32_t operation;
} hisi_encrypt_via_multi;

#define HISI_CMD_CREATE hisi_ioc(HISI_IOC_R, 0x00, sizeof(hisi_symc_create))
#define HISI_CMD_DESTROY hisi_ioc(HISI_IOC_W, 0x01, sizeof(hisi_symc_destroy))
#define HISI_CMD_CONFIG hisi_ioc(HISI_IOC_W, 0x02, sizeof(hisi_symc_cfg))
#define HISI_CMD_ENCRYPT hisi_ioc(HISI_IOC_W, 0x03, sizeof(hisi_symc_encrypt))
#define HISI_CMD_ENCRYPT_VIA_MULTI                                             \
    hisi_ioc(HISI_IOC_W, 0x10, sizeof(hisi_encrypt_via_multi))

/* ---- plumbing ---------------------------------------------------------- */

/* Scratch for padding a short packet up to the block size. The engine takes
 * whole blocks; CTR is a stream cipher, so the padding changes nothing about
 * the bytes the caller gets back. */
static uint8_t g_in[HISI_CIPHER_MAX_LEN + 16] __attribute__((aligned(64)));
static uint8_t g_out[HISI_CIPHER_MAX_LEN + 16] __attribute__((aligned(64)));

/* musl declares the request as `int`, glibc as `unsigned long`. Every command
 * here has bit 31 set (the direction field sits at bit 30), so casting the
 * wrong way round matters: `(int)` on glibc would sign-extend to a 64-bit
 * request the driver never sees. OpenIPC images are musl, but glibc OEM
 * firmware is exactly where someone would want to run this. */
#ifdef __GLIBC__
typedef unsigned long ioctl_req_t;
#else
typedef int ioctl_req_t;
#endif

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do {
        r = ioctl(fd, (ioctl_req_t)req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static bool chan_config(hisi_cipher *c, const uint8_t key[16],
                        const uint8_t iv[16]) {
    hisi_symc_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = c->chan;
    cfg.alg = HISI_ALG_AES;
    cfg.mode = HISI_MODE_CTR;
    cfg.width = HISI_WIDTH_128;
    cfg.klen = HISI_KEYLEN_128;
    memcpy(cfg.fkey, key, 16);
    memcpy(cfg.iv, iv, 16);
    cfg.ivlen = 16;
    cfg.iv_usage = HISI_IV_SET;
    return xioctl(c->fd, HISI_CMD_CONFIG, &cfg) == 0;
}

bool hisi_cipher_ctr(hisi_cipher *c, const uint8_t key[16],
                     const uint8_t iv[16], uint8_t *buf, size_t len) {
    if (c->fd < 0 || len == 0 || len > HISI_CIPHER_MAX_LEN)
        return false;

    const size_t padded = (len + 15) / 16 * 16;
    memcpy(g_in, buf, len);
    if (padded > len)
        memset(g_in + len, 0, padded - len);

    if (!chan_config(c, key, iv))
        return false;

    hisi_symc_encrypt e;
    memset(&e, 0, sizeof(e));
    e.id = c->chan;
    e.len = (uint32_t)padded;
    e.operation = HISI_OP_ENCRYPT_VIRT;
    e.in.cp = g_in;
    e.out.p = g_out;
    if (xioctl(c->fd, HISI_CMD_ENCRYPT, &e) != 0)
        return false;

    memcpy(buf, g_out, len);
    return true;
}

/* Lengths go to the driver unrounded, unlike the single-packet path above,
 * which pads into its own scratch. That asymmetry is deliberate and measured,
 * not an oversight: the batched command encrypts in place at the caller's
 * buffers, so padding here would mean a bounce buffer per package and would
 * make the benchmark measure that copy instead of the engine. The batched
 * driver rounds the descriptor itself, which the caller's known-answer test
 * confirms at a length that is not a multiple of the block -- verified on a
 * gk7205v200 at 1100 bytes, where all fifteen packages match software CTR. */
bool hisi_cipher_ctr_batch(hisi_cipher *c, const uint8_t key[16],
                           const hisi_cipher_job *jobs, size_t count) {
    if (c->fd < 0 || count == 0 || count > HISI_CIPHER_BATCH_MAX)
        return false;

    hisi_via_pkg pkg[HISI_CIPHER_BATCH_MAX];
    memset(pkg, 0, sizeof(pkg));
    for (size_t i = 0; i < count; i++) {
        if (jobs[i].len == 0 || jobs[i].len > HISI_CIPHER_MAX_LEN)
            return false;
        pkg[i].src.cp = jobs[i].buf;
        pkg[i].dst.p = jobs[i].buf;
        pkg[i].length = (uint32_t)jobs[i].len;
        memcpy(pkg[i].iv, jobs[i].iv, 16);
    }

    /* One config for the whole burst — its IV field is written and then
     * ignored, because every package carries its own. */
    if (!chan_config(c, key, pkg[0].iv))
        return false;

    hisi_encrypt_via_multi req;
    memset(&req, 0, sizeof(req));
    req.id = c->chan;
    req.pkg.p = pkg;
    req.pkg_num = (uint32_t)count;
    req.operation = 0;
    return xioctl(c->fd, HISI_CMD_ENCRYPT_VIA_MULTI, &req) == 0;
}

bool hisi_cipher_supports_gcm(hisi_cipher *c) {
    if (c->fd < 0)
        return false;

    /* Configuring the channel is enough to settle it: the driver looks up an
     * implementation for the (alg, mode) pair and refuses here when there is
     * none, long before any data is submitted. The channel is put back to
     * CTR afterwards so a failed probe leaves nothing behind. */
    hisi_symc_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = c->chan;
    cfg.alg = HISI_ALG_AES;
    cfg.mode = HISI_MODE_GCM;
    cfg.width = HISI_WIDTH_128;
    cfg.klen = HISI_KEYLEN_128;
    cfg.ivlen = 12;
    cfg.iv_usage = HISI_IV_SET;
    const bool ok = xioctl(c->fd, HISI_CMD_CONFIG, &cfg) == 0;

    uint8_t zero[16] = {0};
    (void)chan_config(c, zero, zero);
    return ok;
}

bool hisi_cipher_open(hisi_cipher *c) {
    c->fd = -1;
    c->chan = 0;
    c->batch_max = 0;

    c->fd = open(HISI_CIPHER_DEV, O_RDWR | O_CLOEXEC);
    if (c->fd < 0)
        return false;

    hisi_symc_create cr;
    memset(&cr, 0, sizeof(cr));
    if (xioctl(c->fd, HISI_CMD_CREATE, &cr) != 0) {
        close(c->fd);
        c->fd = -1;
        return false;
    }
    c->chan = cr.id;

    /* Probe the batched command by trying it. A driver without it answers
     * EINVAL, which is the common case in the field and not a fault.
     *
     * This asks only whether the command EXISTS. It deliberately does not
     * check the ciphertext, because a probe that submitted one IV and one
     * plaintext could not tell a correct driver from one that ignored the
     * per-package IV, and a probe that looked like it had checked would be
     * worse than one that plainly has not. Correctness is established by the
     * caller's known-answer test, which uses a distinct IV per package. */
    uint8_t key[16] = {0}, iv[16] = {0}, a[16] = {0}, b[16] = {0};
    hisi_cipher_job probe[2] = {
        {iv, a, sizeof(a)},
        {iv, b, sizeof(b)},
    };
    if (hisi_cipher_ctr_batch(c, key, probe, 2))
        c->batch_max = HISI_CIPHER_BATCH_MAX;

    return true;
}

void hisi_cipher_close(hisi_cipher *c) {
    if (c->fd < 0)
        return;
    hisi_symc_destroy d;
    memset(&d, 0, sizeof(d));
    d.id = c->chan;
    (void)xioctl(c->fd, HISI_CMD_DESTROY, &d);
    close(c->fd);
    c->fd = -1;
}
