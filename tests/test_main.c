/**
  ******************************************************************************
  * @file    test_main.c
  * @brief   Host unit tests for the hardware-independent protocol modules.
  *          Run with: cmake --preset host-tests && cmake --build --preset host-tests
  *                    && ctest --preset host-tests
  ******************************************************************************
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alert_packet.h"
#include "byteorder.h"
#include "crc16.h"
#include "lora_toa.h"
#include "proto_frame.h"
#include "proto_msgs.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                          \
  do                                                                         \
  {                                                                          \
    g_checks++;                                                              \
    if (!(cond))                                                             \
    {                                                                        \
      g_failures++;                                                          \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do                                                                         \
  {                                                                          \
    long long va_ = (long long)(a);                                          \
    long long vb_ = (long long)(b);                                          \
    g_checks++;                                                              \
    if (va_ != vb_)                                                          \
    {                                                                        \
      g_failures++;                                                          \
      printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__,  \
             #a, #b, va_, vb_);                                              \
    }                                                                        \
  } while (0)

/* ---------------------------------------------------------------- CRC ---- */

static void test_crc16(void)
{
  const uint8_t check[] = "123456789";
  CHECK_EQ(crc16(check, 9), 0x29B1);
  CHECK_EQ(crc16(NULL, 0), 0xFFFF);
  /* Incremental update equals one-shot */
  CHECK_EQ(crc16_update(crc16(check, 4), &check[4], 5), 0x29B1);
}

/* -------------------------------------------------------------- frames ---- */

typedef struct
{
  int frames;
  int crc_errors;
  int len_errors;
  int timeouts;
  uint8_t last_type;
  uint8_t last_len;
  uint8_t last_payload[PROTO_MAX_PAYLOAD];
} sink_t;

static void sink_cb(void *ctx, proto_evt_t evt, uint8_t type, const uint8_t *payload, uint8_t len)
{
  sink_t *s = (sink_t *)ctx;
  switch (evt)
  {
    case PROTO_EVT_FRAME:
      s->frames++;
      s->last_type = type;
      s->last_len = len;
      memcpy(s->last_payload, payload, len);
      break;
    case PROTO_EVT_ERR_CRC:
      s->crc_errors++;
      break;
    case PROTO_EVT_ERR_LEN:
      s->len_errors++;
      break;
    case PROTO_EVT_TIMEOUT:
      s->timeouts++;
      break;
  }
}

static void test_frame_roundtrip(void)
{
  uint8_t payload[PROTO_MAX_PAYLOAD];
  uint8_t buf[PROTO_MAX_FRAME];
  proto_decoder_t dec;
  sink_t sink;

  for (size_t i = 0; i < sizeof(payload); i++)
  {
    payload[i] = (uint8_t)(i * 7U);   /* includes 0x7E bytes inside the payload */
  }

  /* Every length, fed in one chunk */
  for (unsigned len = 0; len <= PROTO_MAX_PAYLOAD; len++)
  {
    memset(&sink, 0, sizeof(sink));
    proto_decoder_init(&dec);
    size_t n = proto_frame_encode(0x42, payload, (uint8_t)len, buf, sizeof(buf));
    CHECK_EQ(n, len + PROTO_OVERHEAD);
    proto_decoder_feed(&dec, buf, n, 0, sink_cb, &sink);
    CHECK_EQ(sink.frames, 1);
    CHECK_EQ(sink.last_len, len);
    CHECK(memcmp(sink.last_payload, payload, len) == 0);
  }

  /* Byte by byte, with garbage before and between frames */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  const uint8_t garbage[] = { 0x00, 0xFF, 0x13, 0x55 };
  proto_decoder_feed(&dec, garbage, sizeof(garbage), 0, sink_cb, &sink);
  for (int f = 0; f < 3; f++)
  {
    size_t n = proto_frame_encode((uint8_t)(0x10 + f), payload, 20, buf, sizeof(buf));
    for (size_t i = 0; i < n; i++)
    {
      proto_decoder_feed(&dec, &buf[i], 1, (uint32_t)i, sink_cb, &sink);
    }
    proto_decoder_feed(&dec, garbage, sizeof(garbage), 0, sink_cb, &sink);
  }
  CHECK_EQ(sink.frames, 3);
  CHECK_EQ(sink.last_type, 0x12);
  CHECK_EQ(sink.crc_errors, 0);

  /* Encoder refuses an undersized buffer */
  CHECK_EQ(proto_frame_encode(0x01, payload, 10, buf, 14), 0);
}

static void test_frame_resync(void)
{
  uint8_t payload[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  uint8_t good[PROTO_MAX_FRAME];
  uint8_t stream[3 * PROTO_MAX_FRAME];
  proto_decoder_t dec;
  sink_t sink;
  size_t n_good = proto_frame_encode(0x05, payload, sizeof(payload), good, sizeof(good));

  /* Corrupted frame followed by a good one */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  memcpy(stream, good, n_good);
  stream[5] ^= 0x01;
  memcpy(&stream[n_good], good, n_good);
  proto_decoder_feed(&dec, stream, 2 * n_good, 0, sink_cb, &sink);
  CHECK_EQ(sink.crc_errors, 1);
  CHECK_EQ(sink.frames, 1);

  /* A stray SOF right before a real frame: the spurious candidate swallows
   * the real frame, fails, and the rescan must still recover it. */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  stream[0] = PROTO_SOF;
  memcpy(&stream[1], good, n_good);
  memcpy(&stream[1 + n_good], good, n_good);   /* gives the spurious candidate enough bytes */
  proto_decoder_feed(&dec, stream, 1 + 2 * n_good, 0, sink_cb, &sink);
  CHECK_EQ(sink.frames, 2);
  CHECK(sink.crc_errors >= 1);

  /* Invalid length byte, then a good frame */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  const uint8_t bad_len[] = { PROTO_SOF, 0x01, 0xFB };
  proto_decoder_feed(&dec, bad_len, sizeof(bad_len), 0, sink_cb, &sink);
  proto_decoder_feed(&dec, good, n_good, 0, sink_cb, &sink);
  CHECK_EQ(sink.len_errors, 1);
  CHECK_EQ(sink.frames, 1);

  /* Inter-byte timeout drops a partial frame */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  proto_decoder_feed(&dec, good, 4, 1000, sink_cb, &sink);
  proto_decoder_poll(&dec, 1050, sink_cb, &sink);
  CHECK_EQ(sink.timeouts, 0);
  proto_decoder_poll(&dec, 1000 + PROTO_BYTE_TIMEOUT_MS + 1, sink_cb, &sink);
  CHECK_EQ(sink.timeouts, 1);
  proto_decoder_feed(&dec, good, n_good, 2000, sink_cb, &sink);
  CHECK_EQ(sink.frames, 1);

  /* Random noise never crashes the decoder, and good frames still get through afterwards */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  srand(1234);
  for (int i = 0; i < 20000; i++)
  {
    uint8_t b = (uint8_t)(rand() & 0xFF);
    proto_decoder_feed(&dec, &b, 1, 0, sink_cb, &sink);
  }
  /* After the inter-byte gap any stalled noise candidate is dropped */
  proto_decoder_feed(&dec, good, n_good, 1000, sink_cb, &sink);
  proto_decoder_feed(&dec, good, n_good, 1000, sink_cb, &sink);
  CHECK_EQ(sink.frames, 2);

  /* A spurious SOF with a huge len stalls on a real frame; the timeout
   * rescan must still deliver that frame. */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  const uint8_t stall[] = { PROTO_SOF, 0x01, 0xF0 };
  proto_decoder_feed(&dec, stall, sizeof(stall), 0, sink_cb, &sink);
  proto_decoder_feed(&dec, good, n_good, 10, sink_cb, &sink);
  CHECK_EQ(sink.frames, 0);
  proto_decoder_poll(&dec, 10 + PROTO_BYTE_TIMEOUT_MS + 1, sink_cb, &sink);
  CHECK_EQ(sink.timeouts, 1);
  CHECK_EQ(sink.frames, 1);

  /* Noise followed by frames that arrive while a noise candidate is stalled:
   * the timeout rescan must deliver both frames, for many noise patterns. */
  for (unsigned seed = 0; seed < 50U; seed++)
  {
    memset(&sink, 0, sizeof(sink));
    proto_decoder_init(&dec);
    srand(seed);
    for (int i = 0; i < 3000; i++)
    {
      uint8_t b = (uint8_t)(rand() & 0xFF);
      proto_decoder_feed(&dec, &b, 1, 0, sink_cb, &sink);
    }
    int noise_frames = sink.frames;
    proto_decoder_feed(&dec, good, n_good, 50, sink_cb, &sink);
    proto_decoder_feed(&dec, good, n_good, 50, sink_cb, &sink);
    proto_decoder_poll(&dec, 500, sink_cb, &sink);
    CHECK_EQ(sink.frames - noise_frames, 2);
  }

  /* Two stalled spurious candidates nested before a real frame */
  memset(&sink, 0, sizeof(sink));
  proto_decoder_init(&dec);
  const uint8_t stall2[] = { PROTO_SOF, 0x01, 0xF0, 0x00, PROTO_SOF, 0x02, 0xE0 };
  proto_decoder_feed(&dec, stall2, sizeof(stall2), 0, sink_cb, &sink);
  proto_decoder_feed(&dec, good, n_good, 10, sink_cb, &sink);
  proto_decoder_poll(&dec, 500, sink_cb, &sink);
  CHECK_EQ(sink.frames, 1);
}

/* --------------------------------------------------------- time on air ---- */

static uint32_t toa_ms(uint8_t sf, uint8_t pl)
{
  lora_phy_t phy = { .sf = sf, .bw = LORA_BW_125K, .cr = 1, .preamble = 8, .crc_on = true,
                     .implicit_header = false };
  return (lora_toa_us(&phy, pl) + 500U) / 1000U;
}

static int16_t max_pl(uint8_t sf)
{
  lora_phy_t phy = { .sf = sf, .bw = LORA_BW_125K, .cr = 1, .preamble = 8, .crc_on = true,
                     .implicit_header = false };
  return lora_max_payload(&phy, LORA_DWELL_LIMIT_US);
}

static void test_toa_table1(void)
{
  /* TCC, Tabela 1 (BW 125 kHz, CR 4/5, preamble 8, explicit header, CRC on) */
  CHECK_EQ(toa_ms(7, 20), 57);
  CHECK_EQ(toa_ms(7, 51), 103);
  CHECK_EQ(toa_ms(8, 20), 103);
  CHECK_EQ(toa_ms(8, 51), 185);
  CHECK_EQ(toa_ms(9, 20), 185);
  CHECK_EQ(toa_ms(9, 51), 329);
  CHECK_EQ(toa_ms(10, 20), 371);
  CHECK_EQ(toa_ms(10, 51), 616);
  CHECK_EQ(toa_ms(11, 20), 741);
  CHECK_EQ(toa_ms(11, 51), 1315);
  CHECK_EQ(toa_ms(12, 20), 1319);
  CHECK_EQ(toa_ms(12, 51), 2466);

  CHECK_EQ(max_pl(7), 255);
  CHECK_EQ(max_pl(8), 138);
  CHECK_EQ(max_pl(9), 66);
  CHECK_EQ(max_pl(10), 24);
  CHECK_EQ(max_pl(11), 0);     /* only an empty packet fits: "no useful packet" */
  CHECK_EQ(max_pl(12), -1);

  /* Invalid configurations */
  lora_phy_t bad = { .sf = 6, .bw = 0, .cr = 1, .preamble = 8, .crc_on = true };
  CHECK_EQ(lora_toa_us(&bad, 10), 0);
  CHECK_EQ(lora_max_payload(&bad, LORA_DWELL_LIMIT_US), -1);

  /* Huge preamble saturates instead of overflowing */
  lora_phy_t huge = { .sf = 12, .bw = 0, .cr = 4, .preamble = 65535, .crc_on = true };
  CHECK(lora_toa_us(&huge, 255) > LORA_DWELL_LIMIT_US);
}

/* -------------------------------------------------------- radio packet ---- */

static void test_alert_packet(void)
{
  alert_packet_t in = { 0 };
  alert_packet_t out;
  uint8_t buf[ALERT_MAX_PACKET];

  in.port = ALERT_PORT_ALERT;
  in.user_id = 0xDEADBEEF;
  in.seq = 0x1234;
  in.lat = -275000000;    /* -27.5 deg */
  in.lon = -503000000;    /* -50.3 deg */
  in.status = ALERT_STATUS_FIRE;
  in.battery = 42;
  in.msg_len = 5;
  memcpy(in.msg, "fogo!", 5);

  size_t n = alert_packet_encode(&in, buf, sizeof(buf));
  CHECK_EQ(n, ALERT_HDR_LEN + 5);
  /* Big-endian on the wire */
  CHECK_EQ(buf[0], 0x01);
  CHECK_EQ(buf[1], 0xDE);
  CHECK_EQ(buf[4], 0xEF);
  CHECK_EQ(buf[5], 0x12);
  CHECK_EQ(buf[6], 0x34);
  CHECK_EQ(be_get32(&buf[7]), (uint32_t)in.lat);

  CHECK(alert_packet_decode(buf, n, &out));
  CHECK_EQ(out.user_id, in.user_id);
  CHECK_EQ(out.seq, in.seq);
  CHECK_EQ(out.lat, in.lat);
  CHECK_EQ(out.lon, in.lon);
  CHECK_EQ(out.status, in.status);
  CHECK_EQ(out.battery, in.battery);
  CHECK_EQ(out.msg_len, 5);
  CHECK(memcmp(out.msg, "fogo!", 5) == 0);

  CHECK(!alert_packet_decode(buf, ALERT_HDR_LEN - 1U, &out));
  CHECK(alert_packet_decode(buf, ALERT_HDR_LEN, &out));
  CHECK_EQ(out.msg_len, 0);

  CHECK(alert_coords_valid(900000000, -1800000000));
  CHECK(!alert_coords_valid(900000001, 0));
  CHECK(!alert_coords_valid(0, -1800000001));
}

static void test_utf8(void)
{
  uint8_t ok[] = "S\xC3\xA3o Jos\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x94\xA5";
  uint8_t copy[sizeof(ok)];
  memcpy(copy, ok, sizeof(ok));
  alert_msg_sanitize_utf8(copy, sizeof(ok) - 1U);
  CHECK(memcmp(copy, ok, sizeof(ok)) == 0);

  uint8_t bad[] = { 'a', 0xC3, 'b', 0x80, 0xE2, 0x82, 0xF8, 0xC0, 0xAF, 0xED, 0xA0, 0x80, 0xC3 };
  alert_msg_sanitize_utf8(bad, sizeof(bad));
  for (size_t i = 0; i < sizeof(bad); i++)
  {
    CHECK(bad[i] < 0x80U);   /* every invalid sequence became ASCII '?' */
  }
  CHECK_EQ(bad[0], 'a');
  CHECK_EQ(bad[1], '?');
  CHECK_EQ(bad[2], 'b');
}

/* ------------------------------------------------------ serial payloads ---- */

static void test_config(void)
{
  node_config_t cfg;
  node_config_t parsed;
  uint8_t buf[32];

  node_config_defaults(&cfg);
  cfg.role = NODE_ROLE_BEACON;
  cfg.user_id = 0x01020304;
  CHECK_EQ(node_config_validate(&cfg), RESULT_OK);
  CHECK_EQ(node_config_max_msg(&cfg), 49);      /* TCC: 49 bytes at SF9 */

  size_t n = proto_build_config(&cfg, buf, sizeof(buf));
  CHECK_EQ(n, CONFIG_PAYLOAD_LEN);
  CHECK_EQ(proto_parse_config(buf, (uint8_t)n, &parsed), RESULT_OK);
  CHECK_EQ(parsed.user_id, cfg.user_id);
  CHECK_EQ(parsed.interval_s, cfg.interval_s);
  CHECK_EQ(parsed.tx_power, cfg.tx_power);

  cfg.sf = 10;
  CHECK_EQ(node_config_max_msg(&cfg), 7);       /* TCC: 7 bytes at SF10 */
  cfg.sf = 11;
  CHECK_EQ(node_config_validate(&cfg), RESULT_E_TOA);
  cfg.sf = 7;
  CHECK_EQ(node_config_max_msg(&cfg), PROTO_MSG_MAX);

  /* Field validation */
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN - 1U, &parsed), RESULT_E_LEN);
  buf[0] = PROTO_VERSION + 1U;
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_E_PARAM);
  buf[0] = PROTO_VERSION;
  buf[1] = 3;
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_E_PARAM);
  buf[1] = NODE_ROLE_BEACON;
  be_put16(&buf[6], 10);    /* interval below the minimum */
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_E_PARAM);
  be_put16(&buf[6], 300);
  buf[11] = 23;             /* power above 22 dBm */
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_E_PARAM);
  buf[11] = 14;
  be_put32(&buf[2], 0);     /* Beacon needs a user_id */
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_E_PARAM);
  buf[1] = NODE_ROLE_SNIFFER; /* ... a Sniffer does not */
  CHECK_EQ(proto_parse_config(buf, CONFIG_PAYLOAD_LEN, &parsed), RESULT_OK);
}

static void test_alert_state(void)
{
  uint8_t p[ALERT_STATE_HDR_LEN + 60];
  alert_state_t st;

  be_put32(&p[0], (uint32_t)-275000000);
  be_put32(&p[4], (uint32_t)-503000000);
  p[8] = ALERT_STATUS_LOST;
  p[9] = 80;
  memcpy(&p[10], "perdido", 7);
  CHECK_EQ(proto_parse_alert_state(p, 17, 49, &st), RESULT_OK);
  CHECK_EQ(st.lat, -275000000);
  CHECK_EQ(st.status, ALERT_STATUS_LOST);
  CHECK_EQ(st.msg_len, 7);

  CHECK_EQ(proto_parse_alert_state(p, 9, 49, &st), RESULT_E_LEN);
  CHECK_EQ(proto_parse_alert_state(p, 17, 6, &st), RESULT_E_TOA);   /* message too long for SF */
  p[9] = 101;
  CHECK_EQ(proto_parse_alert_state(p, 17, 49, &st), RESULT_E_PARAM);
  p[9] = BATTERY_UNKNOWN;
  CHECK_EQ(proto_parse_alert_state(p, 17, 49, &st), RESULT_OK);
  be_put32(&p[0], 950000000);
  CHECK_EQ(proto_parse_alert_state(p, 17, 49, &st), RESULT_E_PARAM);
}

static void test_status_and_rx_alert(void)
{
  uint8_t buf[PROTO_MAX_PAYLOAD];
  node_status_t st = { .ack_type = FRAME_CONFIG, .result = RESULT_OK, .state = NODE_STATE_BEACON_IDLE,
                       .role = NODE_ROLE_BEACON, .max_msg_len = 49, .last_seq = 0xABCD,
                       .err_flags = 0x0102, .reset_cause = RESET_CAUSE_IWDG };
  CHECK_EQ(proto_build_status(&st, buf, sizeof(buf)), STATUS_PAYLOAD_LEN);
  CHECK_EQ(buf[0], FRAME_CONFIG);
  CHECK_EQ(buf[2], NODE_STATE_BEACON_IDLE);
  CHECK_EQ(buf[7], PROTO_VERSION);
  CHECK_EQ(be_get16(&buf[9]), 0xABCD);
  CHECK_EQ(be_get16(&buf[11]), 0x0102);
  CHECK_EQ(buf[13], RESET_CAUSE_IWDG);

  alert_packet_t pkt = { .port = ALERT_PORT_ALERT, .user_id = 7, .seq = 9, .lat = 1, .lon = -1,
                         .status = 3, .battery = 50, .msg_len = ALERT_MSG_MAX };
  memset(pkt.msg, 'x', sizeof(pkt.msg));
  size_t n = proto_build_rx_alert(&pkt, -120, -7, buf, sizeof(buf));
  CHECK_EQ(n, RX_ALERT_HDR_LEN + PROTO_MSG_MAX);   /* truncated to fit one frame */
  CHECK(n <= PROTO_MAX_PAYLOAD);
  CHECK_EQ((int16_t)be_get16(&buf[16]), -120);
  CHECK_EQ((int8_t)buf[18], -7);
}

int main(void)
{
  struct
  {
    const char *name;
    void (*fn)(void);
  } tests[] = {
    { "crc16", test_crc16 },
    { "frame_roundtrip", test_frame_roundtrip },
    { "frame_resync", test_frame_resync },
    { "toa_table1", test_toa_table1 },
    { "alert_packet", test_alert_packet },
    { "utf8", test_utf8 },
    { "config", test_config },
    { "alert_state", test_alert_state },
    { "status_rx_alert", test_status_and_rx_alert },
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
  {
    int before = g_failures;
    tests[i].fn();
    printf("%-18s %s\n", tests[i].name, (g_failures == before) ? "ok" : "FAILED");
  }
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return (g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
