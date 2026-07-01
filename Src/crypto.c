#include "crypto.h"
#include "stm32wlxx_hal.h" /* CMSIS device header (AES) + HAL FLASH */
#include "rng.h" /* hrng + MX_RNG_Init for crypto_generate_psk */
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

/*
 * AES-128-CCM (RFC 3610). The AES peripheral is driven directly via registers
 * as a single-block ECB cipher; CCM (CBC-MAC + CTR) is built here. No HAL CRYP
 * and no external crypto library. Persistence uses HAL FLASH.
 */

/* CCM length field L = 2 bytes  =>  nonce is 15 - L = 13 bytes. */
#define CCM_L  2

/*
 * One mutex serializes everything that touches shared state (counter, replay
 * table, g_psk, and the single AES peripheral). A CCM op spans dozens of ECB
 * blocks, so we yield via a mutex instead of disabling IRQs.
 *
 * Contract: crypto_* must be called from task context, never from an ISR.
 * Before crypto_init() the mutex is NULL and the lock is a no-op (boot is
 * single-threaded). Redefine CRYPTO_LOCK/UNLOCK for a bare-metal build.
 */
static SemaphoreHandle_t s_aes_lock;

#ifndef CRYPTO_LOCK
#  define CRYPTO_LOCK() do { if (s_aes_lock) (void)xSemaphoreTake(s_aes_lock, portMAX_DELAY); } while (0)
#  define CRYPTO_UNLOCK() do { if (s_aes_lock) (void)xSemaphoreGive(s_aes_lock); } while (0)
#endif

/*
 * Bootstrap placeholder only: this is the public AES test vector (FIPS-197).
 * Real deployments must provision a random PSK (crypto_set_psk /
 * crypto_generate_psk); it is used until one is provisioned.
 */
static const uint8_t PSK_DEFAULT[CRYPTO_KEY_LEN] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static uint8_t  g_psk[CRYPTO_KEY_LEN]; /* active key */
static uint32_t g_boot_id = 0;
static uint32_t g_msg_counter = 0;

/* Anti-replay: highest (boot_id,counter) accepted per node. */
typedef struct {
    uint8_t  valid;
    uint16_t id;
    uint64_t seq; /* (boot_id << 32) | counter */
} replay_entry_t;

static replay_entry_t replay_table[CRYPTO_REPLAY_TABLE_SIZE];

/* ------------------------------------------------------------------ */
/* AES-128 ECB block cipher (direct register access)                  */
/* ------------------------------------------------------------------ */
/*
 * Words are assembled big-endian via explicit shifts (host-endian agnostic)
 * and DATATYPE=00 (no swap): the peripheral takes word bits [31:24] as the
 * first AES byte, matching the NIST/RFC byte order. KEYR3 holds the most
 * significant key bytes. crypto_ccm_selftest() validates this on-target.
 */

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, const uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v);
}

static void aes_ecb_init(const uint8_t key[16])
{
    RCC->AHB3ENR |= RCC_AHB3ENR_AESEN;
    (void)RCC->AHB3ENR;

    AES->CR = 0; /* EN=0; ECB, encrypt, DATATYPE=00, 128b */
    AES->KEYR3 = be32(&key[0]);
    AES->KEYR2 = be32(&key[4]);
    AES->KEYR1 = be32(&key[8]);
    AES->KEYR0 = be32(&key[12]);
    AES->CR |= AES_CR_EN;
}

static void aes_ecb_deinit(void)
{
    AES->CR = 0;
}

static void aes_ecb_block(const uint8_t in[16], uint8_t out[16])
{
    AES->DINR = be32(&in[0]);
    AES->DINR = be32(&in[4]);
    AES->DINR = be32(&in[8]);
    AES->DINR = be32(&in[12]);

    while ((AES->SR & AES_SR_CCF) == 0) { }

    put_be32(&out[0],  AES->DOUTR);
    put_be32(&out[4],  AES->DOUTR);
    put_be32(&out[8],  AES->DOUTR);
    put_be32(&out[12], AES->DOUTR);

    AES->CR |= AES_CR_CCFC;
}

/* ------------------------------------------------------------------ */
/* CCM block formatting and primitives                                */
/* ------------------------------------------------------------------ */

/* B0 = flags | nonce(13) | l(m)(2); flags encode AAD-present, tag len, L. */
static void ccm_format_b0(uint8_t b0[16], const uint8_t *nonce,
                          const uint16_t msg_len, const uint16_t aad_len)
{
    b0[0] = (uint8_t)(((aad_len > 0) ? 0x40u : 0x00u)
                      | (uint8_t)((((CRYPTO_TAG_LEN - 2u) / 2u) & 0x07u) << 3)
                      | (uint8_t)((CCM_L - 1u) & 0x07u));
    memcpy(&b0[1], nonce, CRYPTO_NONCE_LEN);
    b0[14] = (uint8_t)(msg_len >> 8);
    b0[15] = (uint8_t)(msg_len & 0xFF);
}

/* Counter block A_i = flags | nonce(13) | i(2). */
static void ccm_format_ctr(uint8_t a[16], const uint8_t *nonce, const uint16_t ctr)
{
    a[0] = (uint8_t)((CCM_L - 1u) & 0x07u);
    memcpy(&a[1], nonce, CRYPTO_NONCE_LEN);
    a[14] = (uint8_t)(ctr >> 8);
    a[15] = (uint8_t)(ctr & 0xFF);
}

/* CBC-MAC over B0 || AAD || payload -> T. Assumes AAD fits one block
 * (2 + aad_len <= 16), which holds since CRYPTO_AAD_LEN = 4. */
static void ccm_mac(const uint8_t *nonce,
                    const uint8_t *aad, const uint16_t aad_len,
                    const uint8_t *data, const uint16_t data_len,
                    uint8_t T[16])
{
    uint8_t x[16], blk[16];
    int i;

    ccm_format_b0(blk, nonce, data_len, aad_len);
    aes_ecb_block(blk, x); /* X1 = E(B0) */

    if (aad_len > 0) {
        memset(blk, 0, 16);
        blk[0] = (uint8_t)(aad_len >> 8); /* length prefix */
        blk[1] = (uint8_t)(aad_len & 0xFF);
        memcpy(&blk[2], aad, aad_len);
        for (i = 0; i < 16; i++) blk[i] ^= x[i];
        aes_ecb_block(blk, x);
    }

    uint16_t off = 0;
    while (off < data_len) {
        const uint16_t n = (uint16_t)((data_len - off >= 16) ? 16 : (data_len - off));
        memset(blk, 0, 16);
        memcpy(blk, &data[off], n);
        for (i = 0; i < 16; i++) blk[i] ^= x[i];
        aes_ecb_block(blk, x);
        off = (uint16_t)(off + n);
    }

    memcpy(T, x, 16);
}

/* CTR encrypt/decrypt (symmetric) using A_1, A_2, ... */
static void ccm_ctr_crypt(const uint8_t *nonce,
                          const uint8_t *in, uint8_t *out, const uint16_t len)
{
    uint8_t a[16], s[16];
    uint16_t off = 0, ctr = 1;
    while (off < len) {
        const uint16_t n = (uint16_t)((len - off >= 16) ? 16 : (len - off));
        ccm_format_ctr(a, nonce, ctr);
        aes_ecb_block(a, s);
        for (uint16_t i = 0; i < n; i++)
            out[off + i] = (uint8_t)(in[off + i] ^ s[i]);
        off = (uint16_t)(off + n);
        ctr++;
    }
}

/* S0 = E(A_0): keystream block that masks the tag. */
static void ccm_s0(const uint8_t *nonce, uint8_t s0[16])
{
    uint8_t a0[16];
    ccm_format_ctr(a0, nonce, 0);
    aes_ecb_block(a0, s0);
}

/* Key is passed in (not g_psk) so the self-test can use its own vector. */
static void ccm_encrypt_core(const uint8_t *key, const uint8_t *nonce,
                             const uint8_t *aad, const uint16_t aad_len,
                             const uint8_t *pt, const uint16_t pt_len,
                             uint8_t *ct, uint8_t *tag)
{
    uint8_t T[16], s0[16];

    aes_ecb_init(key);
    ccm_mac(nonce, aad, aad_len, pt, pt_len, T);
    ccm_ctr_crypt(nonce, pt, ct, pt_len);
    ccm_s0(nonce, s0);
    aes_ecb_deinit();

    for (uint8_t i = 0; i < CRYPTO_TAG_LEN; i++)
        tag[i] = (uint8_t)(T[i] ^ s0[i]); /* U = T XOR S0 */
}

/* Returns 0 if the tag is valid, CRYPTO_ERR_AUTH otherwise. */
static int ccm_decrypt_core(const uint8_t *key, const uint8_t *nonce,
                            const uint8_t *aad, const uint16_t aad_len,
                            const uint8_t *ct, const uint16_t ct_len,
                            const uint8_t *tag,
                            uint8_t *pt)
{
    uint8_t T[16], s0[16], expect[16];
    uint8_t diff = 0;

    aes_ecb_init(key);
    ccm_ctr_crypt(nonce, ct, pt, ct_len);
    ccm_mac(nonce, aad, aad_len, pt, ct_len, T);
    ccm_s0(nonce, s0);
    aes_ecb_deinit();

    for (uint8_t i = 0; i < CRYPTO_TAG_LEN; i++)
        expect[i] = (uint8_t)(T[i] ^ s0[i]);

    for (uint8_t i = 0; i < CRYPTO_TAG_LEN; i++) /* constant-time compare */
        diff |= (uint8_t)(expect[i] ^ tag[i]);

    return (diff == 0) ? 0 : CRYPTO_ERR_AUTH;
}

/* ------------------------------------------------------------------ */
/* Nonce / AAD / anti-replay                                          */
/* ------------------------------------------------------------------ */

static void build_nonce(uint8_t *n, const uint16_t src,
                        const uint32_t boot_id, const uint32_t counter)
{
    n[0] = (uint8_t)(src >> 8); n[1] = (uint8_t)src;
    n[2] = (uint8_t)(boot_id >> 24); n[3] = (uint8_t)(boot_id >> 16);
    n[4] = (uint8_t)(boot_id >> 8); n[5] = (uint8_t)boot_id;
    n[6] = (uint8_t)(counter >> 24); n[7] = (uint8_t)(counter >> 16);
    n[8] = (uint8_t)(counter >> 8); n[9] = (uint8_t)counter;
    n[10] = 0; n[11] = 0; n[12] = 0;
}

static void build_aad(uint8_t *aad, const uint16_t src, const uint16_t dst)
{
    aad[0] = (uint8_t)(src >> 8); aad[1] = (uint8_t)src;
    aad[2] = (uint8_t)(dst >> 8); aad[3] = (uint8_t)dst;
}

/* Call ONLY after the tag is verified, else a forged frame poisons the
 * table. No internal lock: always runs under the caller's CRYPTO_LOCK(). */
static int replay_check_and_update(const uint16_t src, const uint32_t boot_id, const uint32_t counter)
{
    const uint64_t seq = ((uint64_t)boot_id << 32) | (uint64_t)counter;
    int free_slot = -1;

    for (int i = 0; i < CRYPTO_REPLAY_TABLE_SIZE; i++) {
        if (replay_table[i].valid && replay_table[i].id == src) {
            if (seq > replay_table[i].seq) { replay_table[i].seq = seq; return CRYPTO_OK; }
            return CRYPTO_ERR_REPLAY;
        }
        if (!replay_table[i].valid && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return CRYPTO_ERR_TABLE_FULL;

    replay_table[free_slot].valid = 1;
    replay_table[free_slot].id = src;
    replay_table[free_slot].seq = seq;
    return CRYPTO_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int crypto_init(void)
{
    if (!s_aes_lock) s_aes_lock = xSemaphoreCreateMutex();

    RCC->AHB3ENR |= RCC_AHB3ENR_AESEN;
    (void)RCC->AHB3ENR;

    memset(replay_table, 0, sizeof(replay_table));
    g_boot_id = crypto_persist_next_boot_id();
    g_msg_counter = 0;

    if (!crypto_persist_load_psk(g_psk))
        memcpy(g_psk, PSK_DEFAULT, CRYPTO_KEY_LEN);

    return CRYPTO_OK;
}

int crypto_set_psk(const uint8_t *key, const size_t len)
{
    if (!key || len != CRYPTO_KEY_LEN) return CRYPTO_ERR_PARAM;

    CRYPTO_LOCK();
    memcpy(g_psk, key, CRYPTO_KEY_LEN);
    CRYPTO_UNLOCK();

    crypto_persist_store_psk(g_psk); /* persist outside the lock */
    return CRYPTO_OK;
}

int crypto_generate_psk(void)
{
    uint8_t key[CRYPTO_KEY_LEN];

    if (hrng.State == HAL_RNG_STATE_RESET) MX_RNG_Init(); /* not inited at boot */

    for (uint32_t i = 0; i < CRYPTO_KEY_LEN; i += 4) {
        uint32_t r;
        if (HAL_RNG_GenerateRandomNumber(&hrng, &r) != HAL_OK) {
            memset(key, 0, sizeof(key));
            return CRYPTO_ERR_CRYPTO;
        }
        memcpy(&key[i], &r, 4);
    }

    const int rc = crypto_set_psk(key, CRYPTO_KEY_LEN);
    memset(key, 0, sizeof(key)); /* wipe key off the stack */
    return rc;
}

int crypto_encrypt(const uint8_t *plaintext, const uint8_t len,
                   const uint16_t src, const uint16_t dst,
                   SecureFrame *out)
{
    if (!plaintext || !out) return CRYPTO_ERR_PARAM;
    if (len > CRYPTO_MAX_PLAINTEXT) return CRYPTO_ERR_TOO_LONG;

    uint8_t nonce[CRYPTO_NONCE_LEN];
    uint8_t aad[CRYPTO_AAD_LEN];

    CRYPTO_LOCK();
    if (g_msg_counter == 0xFFFFFFFFu) { /* new boot_id on wrap: avoid nonce reuse */
        g_boot_id = crypto_persist_next_boot_id();
        g_msg_counter = 0;
    }
    const uint32_t counter = ++g_msg_counter; /* first frame uses counter = 1 */
    const uint32_t boot_id = g_boot_id;
    build_nonce(nonce, src, boot_id, counter);
    build_aad(aad, src, dst);
    ccm_encrypt_core(g_psk, nonce, aad, CRYPTO_AAD_LEN,
                     plaintext, len, out->ciphertext, out->tag);
    CRYPTO_UNLOCK();

    out->src_id = src;
    out->dst_id = dst;
    out->boot_id = boot_id;
    out->counter = counter;
    out->ciphertext_len = len;
    return CRYPTO_OK;
}

int crypto_decrypt(const SecureFrame *in,
                   uint8_t *plaintext, uint8_t *len)
{
    if (!in || !plaintext || !len) return CRYPTO_ERR_PARAM;
    if (in->ciphertext_len > CRYPTO_MAX_PLAINTEXT) return CRYPTO_ERR_WIRE;

    uint8_t nonce[CRYPTO_NONCE_LEN];
    uint8_t aad[CRYPTO_AAD_LEN];
    build_nonce(nonce, in->src_id, in->boot_id, in->counter);
    build_aad(aad, in->src_id, in->dst_id);

    CRYPTO_LOCK();
    int rc = ccm_decrypt_core(g_psk, nonce, aad, CRYPTO_AAD_LEN,
                              in->ciphertext, in->ciphertext_len,
                              in->tag, plaintext);
    if (rc == CRYPTO_OK) /* anti-replay only after auth */
        rc = replay_check_and_update(in->src_id, in->boot_id, in->counter);
    CRYPTO_UNLOCK();

    if (rc != CRYPTO_OK) {
        memset(plaintext, 0, in->ciphertext_len);  /* don't leak unauthenticated output */
        return rc;
    }

    *len = in->ciphertext_len;
    return CRYPTO_OK;
}

/* ------------------------------------------------------------------ */
/* Wire (de)serialization: big-endian, no struct padding              */
/* ------------------------------------------------------------------ */

int crypto_frame_serialize(const SecureFrame *f, uint8_t *buf,
                           const size_t buf_len, size_t *out_len)
{
    if (!f || !buf || !out_len) return CRYPTO_ERR_PARAM;
    if (f->ciphertext_len > CRYPTO_MAX_PLAINTEXT) return CRYPTO_ERR_PARAM;

    const size_t total = (size_t)CRYPTO_FRAME_HEADER_LEN + f->ciphertext_len + CRYPTO_TAG_LEN;
    if (buf_len < total) return CRYPTO_ERR_WIRE;

    size_t p = 0;
    buf[p++] = (uint8_t)(f->src_id >> 8); buf[p++] = (uint8_t)(f->src_id);
    buf[p++] = (uint8_t)(f->dst_id >> 8); buf[p++] = (uint8_t)(f->dst_id);
    buf[p++] = (uint8_t)(f->boot_id >> 24); buf[p++] = (uint8_t)(f->boot_id >> 16);
    buf[p++] = (uint8_t)(f->boot_id >> 8); buf[p++] = (uint8_t)(f->boot_id);
    buf[p++] = (uint8_t)(f->counter >> 24); buf[p++] = (uint8_t)(f->counter >> 16);
    buf[p++] = (uint8_t)(f->counter >> 8); buf[p++] = (uint8_t)(f->counter);
    buf[p++] = f->ciphertext_len;

    memcpy(&buf[p], f->ciphertext, f->ciphertext_len); p += f->ciphertext_len;
    memcpy(&buf[p], f->tag, CRYPTO_TAG_LEN); p += CRYPTO_TAG_LEN;

    *out_len = p;
    return CRYPTO_OK;
}

int crypto_frame_deserialize(const uint8_t *buf, const size_t buf_len,
                             SecureFrame *f)
{
    if (!buf || !f) return CRYPTO_ERR_PARAM;
    if (buf_len < (size_t)CRYPTO_FRAME_HEADER_LEN + CRYPTO_TAG_LEN) return CRYPTO_ERR_WIRE;

    size_t p = 0;
    f->src_id  = (uint16_t)(buf[p] << 8 | buf[p + 1]); p += 2;
    f->dst_id  = (uint16_t)(buf[p] << 8 | buf[p + 1]); p += 2;
    f->boot_id = (uint32_t)buf[p] << 24 | (uint32_t)buf[p + 1] << 16 |
                 (uint32_t)buf[p + 2] << 8 | (uint32_t)buf[p + 3]; p += 4;
    f->counter = (uint32_t)buf[p] << 24 | (uint32_t)buf[p + 1] << 16 |
                 (uint32_t)buf[p + 2] << 8 | (uint32_t)buf[p + 3]; p += 4;

    const uint8_t clen = buf[p++];
    if (clen > CRYPTO_MAX_PLAINTEXT) return CRYPTO_ERR_WIRE;
    if (buf_len < (size_t)CRYPTO_FRAME_HEADER_LEN + clen + CRYPTO_TAG_LEN) return CRYPTO_ERR_WIRE;

    f->ciphertext_len = clen;
    memcpy(f->ciphertext, &buf[p], clen);
    p += clen;
    memcpy(f->tag, &buf[p], CRYPTO_TAG_LEN);
    return CRYPTO_OK;
}

/*
 * Self-test against RFC 3610 packet vector #1 (M=8, L=2). Return codes:
 *   0 ok; -2 ciphertext mismatch; -3 tag mismatch; -4 round-trip failed;
 *   -5 recovered plaintext mismatch; -6 tampering not detected.
 */
int crypto_ccm_selftest(void)
{
    static const uint8_t key[16] = {
        0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,
        0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF
    };
    static const uint8_t nonce[13] = {
        0x00,0x00,0x00,0x03,0x02,0x01,0x00,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5
    };
    static const uint8_t aad[8] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 };
    static const uint8_t pt[23] = {
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E
    };
    static const uint8_t expect_ct[23] = {
        0x58,0x8C,0x97,0x9A,0x61,0xC6,0x63,0xD2,
        0xF0,0x66,0xD0,0xC2,0xC0,0xF9,0x89,0x80,
        0x6D,0x5F,0x6B,0x61,0xDA,0xC3,0x84
    };
    static const uint8_t expect_tag[8] = {
        0x17,0xE8,0xD1,0x2C,0xFD,0xF9,0x26,0xE0
    };

    uint8_t ct[23], tag[8], dec[23], bad[23];

    ccm_encrypt_core(key, nonce, aad, 8, pt, 23, ct, tag);
    if (memcmp(ct,  expect_ct,  23) != 0) return -2;
    if (memcmp(tag, expect_tag,  8) != 0) return -3;

    if (ccm_decrypt_core(key, nonce, aad, 8, ct, 23, tag, dec) != 0) return -4;
    if (memcmp(dec, pt, 23) != 0) return -5;

    /* a single flipped bit must fail authentication */
    memcpy(bad, ct, 23);
    bad[0] ^= 0x01;
    if (ccm_decrypt_core(key, nonce, aad, 8, bad, 23, tag, dec) != CRYPTO_ERR_AUTH)
        return -6;

    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal-flash persistence (HAL FLASH)                             */
/* ------------------------------------------------------------------ */
/*
 * Uses the last three 2 KB pages, reserved as the NVM region in
 * STM32WLE5XX_FLASH.ld and addressed absolutely here.
 *
 * boot_id (pages 125/126, ping-pong append-only log): each doubleword stores
 * boot_id in the low word and ~boot_id in the high word (self-check). The page
 * holding the current value is never erased before the new value is committed
 * to the other page, so the latest committed boot_id always survives a power
 * loss -> the counter never resets -> no nonce reuse. ~512 boots per erase.
 *
 * PSK (page 127, single record): the magic word is written LAST, so an
 * interrupted write is not accepted (load falls back to PSK_DEFAULT).
 */

#define PERSIST_BOOTID_PAGE_A 125u
#define PERSIST_BOOTID_PAGE_B 126u
#define PERSIST_PSK_PAGE 127u
#define PERSIST_PAGE_ADDR(pg) ((uint32_t)(FLASH_BASE + (pg) * FLASH_PAGE_SIZE))
#define PERSIST_PSK_PAGE_ADDR PERSIST_PAGE_ADDR(PERSIST_PSK_PAGE)
#define PERSIST_PSK_MAGIC 0x314B5350u /* "PSK1" */
#define FLASH_DW_ERASED 0xFFFFFFFFFFFFFFFFull

static int flash_erase_page(const uint32_t page)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t page_err = 0;

    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Page = page;
    e.NbPages = 1;

    HAL_FLASH_Unlock();
    const HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &page_err);
    HAL_FLASH_Lock();
    return st == HAL_OK && page_err == 0xFFFFFFFFu ? 0 : -1;
}

static int flash_program_dw(const uint32_t addr, const uint64_t data)
{
    HAL_FLASH_Unlock();
    const HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data);
    HAL_FLASH_Lock();
    return st == HAL_OK ? 0 : -1;
}

/*
 * Scan one boot_id log page. *out_max = highest valid boot_id (0 if none),
 * *out_free = first free slot index (== slot count if full), return = 1 if any
 * valid record. The first slot failing the self-check (erased or half-written
 * after a power loss) marks the end of the log.
 */
static int bootid_scan(const uint32_t page, uint32_t *out_max, uint32_t *out_free)
{
    const uint32_t base = PERSIST_PAGE_ADDR(page);
    const uint32_t slots = FLASH_PAGE_SIZE / 8u; /* 256 doublewords */
    uint32_t max = 0, i;
    int found = 0;

    for (i = 0; i < slots; i++) {
        const uint64_t rec = *(volatile const uint64_t *)(base + i * 8u);
        const uint32_t lo = (uint32_t)rec;
        const uint32_t hi = (uint32_t)(rec >> 32);
        if (hi == ~lo) { max = lo; found = 1; }
        else break;
    }
    *out_max = max;
    *out_free = i;
    return found;
}

uint32_t crypto_persist_next_boot_id(void)
{
    const uint32_t slots = FLASH_PAGE_SIZE / 8u;
    uint32_t maxA, freeA, maxB, freeB;
    const int foundA = bootid_scan(PERSIST_BOOTID_PAGE_A, &maxA, &freeA);
    const int foundB = bootid_scan(PERSIST_BOOTID_PAGE_B, &maxB, &freeB);

    /* Current page = the one holding the highest committed boot_id. */
    uint32_t cur_page, cur_free, oth_page, last;
    if (foundB && (!foundA || maxB >= maxA)) {
        cur_page = PERSIST_BOOTID_PAGE_B; cur_free = freeB; oth_page = PERSIST_BOOTID_PAGE_A; last = maxB;
    } else {
        cur_page = PERSIST_BOOTID_PAGE_A; cur_free = freeA; oth_page = PERSIST_BOOTID_PAGE_B; last = foundA ? maxA : 0u;
    }

    const uint32_t next = last + 1u;
    uint32_t write_addr;

    /* Append to the current page if its next slot is free; otherwise (full or
     * corrupt) recycle the other page -- the current one still holds 'last'
     * until the new record is committed. */
    if (cur_free < slots &&
        *(volatile const uint64_t *)(PERSIST_PAGE_ADDR(cur_page) + cur_free * 8u) == FLASH_DW_ERASED) {
        write_addr = PERSIST_PAGE_ADDR(cur_page) + cur_free * 8u;
    } else {
        flash_erase_page(oth_page);
        write_addr = PERSIST_PAGE_ADDR(oth_page);
    }

    const uint64_t rec = (uint64_t)next | (uint64_t)~next << 32;
    flash_program_dw(write_addr, rec);
    return next;
}

void crypto_persist_store_psk(const uint8_t key[CRYPTO_KEY_LEN])
{
    const uint32_t base = PERSIST_PSK_PAGE_ADDR;
    uint64_t dw0, dw1;

    memcpy(&dw0, &key[0], 8);
    memcpy(&dw1, &key[8], 8);

    flash_erase_page(PERSIST_PSK_PAGE);
    flash_program_dw(base + 0u,  dw0);
    flash_program_dw(base + 8u,  dw1);
    flash_program_dw(base + 16u, PERSIST_PSK_MAGIC); /* commit last */
}

int crypto_persist_load_psk(uint8_t key[CRYPTO_KEY_LEN])
{
    const uint32_t base  = PERSIST_PSK_PAGE_ADDR;
    const uint32_t magic = *(volatile const uint32_t *)(base + 16u);

    if (magic != PERSIST_PSK_MAGIC) return 0;

    memcpy(&key[0], (const void *)(base + 0u), 8);
    memcpy(&key[8], (const void *)(base + 8u), 8);
    return 1;
}
