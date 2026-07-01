#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/*
 * End-to-end security for the LoRa mesh: AES-128-CCM (RFC 3610) built on the
 * STM32WL AES accelerator driven directly via registers. Provides payload confidentiality (CTR) plus authenticity
 * of the payload and the routing header (src/dst as AAD, boot_id/counter in
 * the nonce), replay protection (checked only after authentication), and
 * nonce uniqueness across reboots via a persisted boot_id.
 */

#define CRYPTO_KEY_LEN 16 /* AES-128 */
#define CRYPTO_TAG_LEN 8 /* 64-bit CCM tag */
#define CRYPTO_NONCE_LEN 13 /* CCM: N + q = 15, q = 2 */
#define CRYPTO_AAD_LEN 4 /* src_id(2) + dst_id(2): authenticated, not encrypted */

/* High spreading factors shrink the LoRa MTU (~51 B at SF12), so the
 * application may still need to fragment below this. */
#define CRYPTO_MAX_PLAINTEXT 200

#define CRYPTO_REPLAY_TABLE_SIZE 16

/* Wire format: src(2)+dst(2)+boot_id(4)+counter(4)+len(1) + ciphertext + tag(8) */
#define CRYPTO_FRAME_HEADER_LEN 13
#define CRYPTO_FRAME_OVERHEAD (CRYPTO_FRAME_HEADER_LEN + CRYPTO_TAG_LEN)
#define CRYPTO_FRAME_MAX_WIRE (CRYPTO_FRAME_OVERHEAD + CRYPTO_MAX_PLAINTEXT)

#define CRYPTO_OK 0
#define CRYPTO_ERR_PARAM (-1) /* null/invalid argument */
#define CRYPTO_ERR_TOO_LONG (-2) /* plaintext over the limit */
#define CRYPTO_ERR_CRYPTO (-3) /* peripheral failure */
#define CRYPTO_ERR_AUTH (-4) /* invalid tag: forged/corrupted frame */
#define CRYPTO_ERR_REPLAY (-5) /* counter <= last seen */
#define CRYPTO_ERR_TABLE_FULL (-6) /* no room in the replay table */
#define CRYPTO_ERR_WIRE (-7) /* malformed wire buffer */

typedef struct {
    uint16_t src_id;
    uint16_t dst_id;
    uint32_t boot_id; /* persisted boot session id */
    uint32_t counter; /* monotonic within a boot session */
    uint8_t ciphertext_len;
    uint8_t ciphertext[CRYPTO_MAX_PLAINTEXT];
    uint8_t tag[CRYPTO_TAG_LEN];
} SecureFrame;

/* Call once at boot (from task context): enables the AES clock, clears state
 * and loads the persisted PSK (or the built-in placeholder if none). */
int crypto_init(void);

/*
 * Set and persist the active pre-shared key (for provisioning by the mobile
 * app). 'len' must be CRYPTO_KEY_LEN. The change takes effect on the next
 * encrypt/decrypt.
 *
 * Security: the command carrying a new PSK must not travel in clear. Intended
 * flow is authenticated rekey -- the app sends the new PSK inside a SecureFrame
 * encrypted under the current PSK, and only after a successful decrypt does the
 * application call this. The initial PSK is provisioned out of band.
 */
int crypto_set_psk(const uint8_t *key, size_t len);

/* Generate a random PSK from the hardware TRNG, set it active and persist it.
 * Returns CRYPTO_OK, or CRYPTO_ERR_CRYPTO if the TRNG fails. */
int crypto_generate_psk(void);

int crypto_encrypt(const uint8_t *plaintext, uint8_t len,
                   uint16_t src, uint16_t dst,
                   SecureFrame *out);

int crypto_decrypt(const SecureFrame *in,
                   uint8_t *plaintext, uint8_t *len);

/* Wire (de)serialization (big-endian, no struct padding). */
int crypto_frame_serialize(const SecureFrame *f, uint8_t *buf,
                           size_t buf_len, size_t *out_len);
int crypto_frame_deserialize(const uint8_t *buf, size_t buf_len,
                             SecureFrame *f);

/* Hardware-path self-test against the RFC 3610 vector; call once at boot.
 * Returns 0 on success, negative on failure (see crypto.c). Catches key/data
 * byte-order and AES clock mistakes immediately. */
int crypto_ccm_selftest(void);

/* Persistence in internal flash (see crypto.c). next_boot_id returns a fresh,
 * never-reused boot_id that survives power loss; load_psk returns 1 if a PSK
 * was stored. */
uint32_t crypto_persist_next_boot_id(void);
void     crypto_persist_store_psk(const uint8_t key[CRYPTO_KEY_LEN]);
int      crypto_persist_load_psk(uint8_t key[CRYPTO_KEY_LEN]);

#endif /* CRYPTO_H */
