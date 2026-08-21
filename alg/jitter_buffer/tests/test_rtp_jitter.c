#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rtp_jitter.h"

typedef struct Capture {
  uint16_t sequences[32];
  size_t count;
} Capture;

static uint16_t read_u16(const uint8_t* data) {
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_u32(const uint8_t* data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static size_t make_packet(uint8_t* packet, uint16_t sequence, uint32_t ssrc) {
  memset(packet, 0, 12);
  packet[0] = 0x80;
  packet[1] = 8;
  packet[2] = (uint8_t)(sequence >> 8);
  packet[3] = (uint8_t)sequence;
  packet[8] = (uint8_t)(ssrc >> 24);
  packet[9] = (uint8_t)(ssrc >> 16);
  packet[10] = (uint8_t)(ssrc >> 8);
  packet[11] = (uint8_t)ssrc;
  return 12;
}

static int capture_packet(const uint8_t* packet, size_t size,
                          void* user_data) {
  Capture* capture = user_data;

  assert(size == 12);
  assert(capture->count < sizeof(capture->sequences) /
                                  sizeof(capture->sequences[0]));
  capture->sequences[capture->count++] = read_u16(packet + 2);
  return 0;
}

static RtpJitter* create_jitter(Capture* capture, uint32_t timeout_ms,
                                uint32_t packets_per_sec,
                                uint16_t reorder_margin) {
  RtpJitterConfig config = {
      .max_packet_size = 12,
      .max_timeout_ms = timeout_ms,
      .max_packets_per_sec = packets_per_sec,
      .reorder_margin = reorder_margin,
      .nack_interval_ms = 20,
  };

  return rtp_jitter_create(&config, 0x01020304, capture_packet, capture);
}

static void add_packet(RtpJitter* jitter, uint16_t sequence, uint32_t ssrc,
                       uint32_t now_ms) {
  uint8_t packet[12];

  make_packet(packet, sequence, ssrc);
  assert(rtp_jitter_add(jitter, packet, sizeof(packet), now_ms) == 0);
}

static void test_reorder_duplicate_and_nack(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 60, 50, 2);
  uint8_t nack[64] = {0};

  assert(jitter != NULL);
  add_packet(jitter, 1, 0x11223344, 1000);
  add_packet(jitter, 3, 0x11223344, 1010);
  assert(capture.count == 1 && capture.sequences[0] == 1);
  assert(rtp_jitter_get_nack(jitter, 1010, nack, sizeof(nack)) == 16);
  assert(nack[0] == 0x81 && nack[1] == 205 && read_u16(nack + 2) == 3);
  assert(read_u32(nack + 4) == 0x01020304);
  assert(read_u32(nack + 8) == 0x11223344);
  assert(read_u16(nack + 12) == 2 && read_u16(nack + 14) == 0);
  assert(rtp_jitter_get_nack(jitter, 1029, nack, sizeof(nack)) == 0);
  assert(rtp_jitter_get_nack(jitter, 1030, nack, sizeof(nack)) == 16);

  add_packet(jitter, 2, 0x11223344, 1031);
  assert(capture.count == 3);
  assert(capture.sequences[1] == 2 && capture.sequences[2] == 3);
  assert(rtp_jitter_add(jitter, nack, 11, 1032) < 0);
  make_packet(nack, 3, 0x11223344);
  assert(rtp_jitter_add(jitter, nack, 12, 1032) == 1);
  rtp_jitter_destroy(jitter);
}

static void test_pid_blp(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 100, 220, 1);
  uint8_t nack[64] = {0};

  assert(jitter != NULL);
  add_packet(jitter, 10, 7, 0);
  add_packet(jitter, 30, 7, 1);
  assert(rtp_jitter_get_nack(jitter, 1, nack, sizeof(nack)) == 20);
  assert(read_u16(nack + 12) == 11 && read_u16(nack + 14) == 0xffff);
  assert(read_u16(nack + 16) == 28 && read_u16(nack + 18) == 0x0001);
  memset(nack, 0, sizeof(nack));
  assert(rtp_jitter_get_nack(jitter, 21, nack, 16) == 16);
  assert(read_u16(nack + 12) == 11 && read_u16(nack + 14) == 0xffff);
  rtp_jitter_destroy(jitter);
}

static void test_gap_deadline_is_not_extended(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 60, 100, 2);

  assert(jitter != NULL);
  add_packet(jitter, 1, 8, 0);
  add_packet(jitter, 5, 8, 10);
  add_packet(jitter, 3, 8, 40);
  assert(rtp_jitter_poll(jitter, 69) == 0);
  assert(rtp_jitter_poll(jitter, 70) == 2);
  assert(capture.count == 3);
  assert(capture.sequences[1] == 3 && capture.sequences[2] == 5);
  rtp_jitter_destroy(jitter);
}

static void test_timeout_and_time_wrap(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 60, 50, 2);
  uint8_t nack[32] = {0};

  assert(jitter != NULL);
  add_packet(jitter, 100, 9, UINT32_MAX - 20);
  add_packet(jitter, 102, 9, UINT32_MAX - 10);
  assert(rtp_jitter_poll(jitter, 48) == 0);
  assert(rtp_jitter_get_nack(jitter, 10, nack, sizeof(nack)) == 16);
  assert(rtp_jitter_poll(jitter, 49) == 1);
  assert(capture.count == 2 && capture.sequences[1] == 102);
  rtp_jitter_destroy(jitter);
}

static void test_sequence_wrap(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 60, 50, 2);
  uint8_t nack[32] = {0};

  assert(jitter != NULL);
  add_packet(jitter, 65534, 11, 0);
  add_packet(jitter, 0, 11, 1);
  assert(rtp_jitter_get_nack(jitter, 1, nack, sizeof(nack)) == 16);
  assert(read_u16(nack + 12) == 65535);
  add_packet(jitter, 65535, 11, 2);
  assert(capture.count == 3);
  assert(capture.sequences[0] == 65534);
  assert(capture.sequences[1] == 65535);
  assert(capture.sequences[2] == 0);
  rtp_jitter_destroy(jitter);
}

static void test_configured_window_limit(void) {
  Capture capture = {0};
  RtpJitter* jitter = create_jitter(&capture, 60, 50, 2);

  assert(jitter != NULL); /* ceil(50 * 60 / 1000) + 2 = 5 slots. */
  add_packet(jitter, 1, 13, 0);
  add_packet(jitter, 8, 13, 1);
  assert(capture.count == 2);
  assert(capture.sequences[0] == 1 && capture.sequences[1] == 8);
  rtp_jitter_destroy(jitter);
}

int main(void) {
  RtpJitterConfig invalid = {0};

  assert(rtp_jitter_create(NULL, 0, capture_packet, NULL) == NULL);
  assert(rtp_jitter_create(&invalid, 0, capture_packet, NULL) == NULL);
  test_reorder_duplicate_and_nack();
  test_pid_blp();
  test_gap_deadline_is_not_extended();
  test_timeout_and_time_wrap();
  test_sequence_wrap();
  test_configured_window_limit();
  return 0;
}
