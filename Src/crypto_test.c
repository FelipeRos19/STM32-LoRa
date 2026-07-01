/*
 * TEMPORARY crypto bring-up test -- see crypto_test.h. Remove together with
 * its call in app_freertos.c (StartDefaultTask) and its CMake entry.
 */
#include "crypto_test.h"
#include "crypto.h"
#include "usart.h"              /* huart2 */
#include <string.h>
#include <stdio.h>

/* Buffers are static (not on the task stack): a SecureFrame is ~216 B and the
 * test runs once, single-threaded. */
static uint8_t     s_pt[40];
static uint8_t     s_out[CRYPTO_MAX_PLAINTEXT];
static uint8_t     s_wire[CRYPTO_FRAME_MAX_WIRE];
static SecureFrame s_frame, s_frame2;

static void uart_puts(const char *s)
{
    HAL_UART_Transmit(&huart2, (const uint8_t *)s, (uint16_t)strlen(s), 500);
}

static void report(const char *name, int ok, int rc)
{
    char line[64];
    const int n = snprintf(line, sizeof line, "[crypto-test] %-16s %s (rc=%d)\r\n",
                           name, ok ? "PASS" : "FAIL", rc);
    if (n > 0) HAL_UART_Transmit(&huart2, (const uint8_t *)line, (uint16_t)n, 500);
}

/* encrypt -> serialize -> deserialize -> decrypt, checking the recovered
 * plaintext. Uses a fresh src so the replay table never rejects it. */
static int roundtrip_ok(uint16_t src, uint16_t dst)
{
    uint8_t len = 0;
    size_t  wlen = 0;
    return crypto_encrypt(s_pt, sizeof s_pt, src, dst, &s_frame) == CRYPTO_OK
        && crypto_frame_serialize(&s_frame, s_wire, sizeof s_wire, &wlen) == CRYPTO_OK
        && crypto_frame_deserialize(s_wire, wlen, &s_frame2) == CRYPTO_OK
        && crypto_decrypt(&s_frame2, s_out, &len) == CRYPTO_OK
        && len == sizeof s_pt
        && memcmp(s_out, s_pt, sizeof s_pt) == 0;
}

int crypto_test_run(void)
{
    int pass = 0, total = 0, rc, ok;
    uint8_t len = 0;

    uart_puts("\r\n=== crypto self-test (TEMP) ===\r\n");

    for (int i = 0; i < (int)sizeof s_pt; i++) s_pt[i] = (uint8_t)(0x40 + i);

    /* 1. RFC 3610 known-answer vector (validates the AES register path). */
    rc = crypto_ccm_selftest();
    total++; pass += (rc == 0);
    report("rfc3610-vector", rc == 0, rc);

    crypto_init();   /* mutex, boot_id, PSK (nobody else calls this yet) */

    /* 2. full wire round-trip. */
    ok = roundtrip_ok(0x0001, 0x0002);
    total++; pass += ok;
    report("roundtrip", ok, 0);

    /* 3. a flipped ciphertext bit must fail authentication. */
    rc = crypto_encrypt(s_pt, sizeof s_pt, 0x0001, 0x0002, &s_frame);
    s_frame.ciphertext[0] ^= 0x01;
    rc = crypto_decrypt(&s_frame, s_out, &len);
    total++; pass += (rc == CRYPTO_ERR_AUTH);
    report("tamper-detect", rc == CRYPTO_ERR_AUTH, rc);

    /* 4. the same frame decrypted twice must be rejected as replay. */
    crypto_encrypt(s_pt, sizeof s_pt, 0x0003, 0x0004, &s_frame);
    crypto_decrypt(&s_frame, s_out, &len);          /* first: accepted */
    rc = crypto_decrypt(&s_frame, s_out, &len);     /* second: replay */
    total++; pass += (rc == CRYPTO_ERR_REPLAY);
    report("replay-reject", rc == CRYPTO_ERR_REPLAY, rc);

    /* 5. round-trip still works after setting a new PSK. */
    {
        static const uint8_t k[CRYPTO_KEY_LEN] = {
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        };
        rc = crypto_set_psk(k, sizeof k);
        ok = (rc == CRYPTO_OK) && roundtrip_ok(0x0005, 0x0006);
        total++; pass += ok;
        report("set-psk", ok, rc);
    }

    /* 6. round-trip still works after a TRNG-generated PSK. */
    rc = crypto_generate_psk();
    ok = (rc == CRYPTO_OK) && roundtrip_ok(0x0007, 0x0008);
    total++; pass += ok;
    report("generate-psk", ok, rc);

    {
        char line[48];
        const int n = snprintf(line, sizeof line, "=== %d/%d passed ===\r\n", pass, total);
        if (n > 0) HAL_UART_Transmit(&huart2, (const uint8_t *)line, (uint16_t)n, 500);
    }
    return (pass == total) ? 0 : -1;
}
