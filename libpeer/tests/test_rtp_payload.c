#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rtp.h"

typedef struct TestResult {
  uint8_t data[64];
  size_t size;
  int calls;
  RtpPacketMetadata metadata;
} TestResult;

static void on_video(uint8_t* data, size_t size,
                     const RtpPacketMetadata* metadata, void* user_data) {
  TestResult* result = user_data;

  result->metadata = *metadata;
  assert(size <= sizeof(result->data) - result->size);
  memcpy(result->data + result->size, data, size);
  result->size += size;
  result->calls++;
}

static void on_audio(uint8_t* data, size_t size,
                     const RtpPacketMetadata* metadata, void* user_data) {
  TestResult* result = user_data;

  memcpy(result->data, data, size);
  result->size = size;
  result->metadata = *metadata;
}

static size_t create_extended_packet(uint8_t* packet,
                                     const uint8_t* payload,
                                     size_t payload_size) {
  RtpHeader* header = (RtpHeader*)packet;
  uint16_t extension_words = htons(1);
  size_t offset;

  memset(packet, 0, 64);
  header->version = 2;
  header->padding = 1;
  header->extension = 1;
  header->csrccount = 1;
  header->type = PT_H264;
  header->seq_number = htons(123);
  header->timestamp = htonl(456);
  header->ssrc = htonl(789);

  offset = sizeof(RtpHeader) + sizeof(uint32_t);
  packet[offset] = 0xbe;
  packet[offset + 1] = 0xde;
  memcpy(packet + offset + 2, &extension_words, sizeof(extension_words));
  offset += 4;
  memset(packet + offset, 0xa5, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(packet + offset, payload, payload_size);
  offset += payload_size;
  memset(packet + offset, 0, 4);
  packet[offset + 3] = 4;
  return offset + 4;
}

static size_t create_packet(uint8_t* packet, const uint8_t* payload,
                            size_t payload_size, uint16_t sequence_number) {
  RtpHeader* header = (RtpHeader*)packet;

  memset(packet, 0, sizeof(RtpHeader) + payload_size);
  header->version = 2;
  header->type = PT_H264;
  header->seq_number = htons(sequence_number);
  header->timestamp = htonl(456);
  header->ssrc = htonl(789);
  memcpy(packet + sizeof(RtpHeader), payload, payload_size);
  return sizeof(RtpHeader) + payload_size;
}

static void test_decoder_isolation(void) {
  static const uint8_t start_one[] = {0x7c, 0x85, 0x11};
  static const uint8_t start_two[] = {0x7c, 0x85, 0x22};
  static const uint8_t end_one[] = {0x7c, 0x45, 0x33};
  static const uint8_t end_two[] = {0x7c, 0x45, 0x44};
  static const uint8_t expected_one[] = {0, 0, 0, 1, 0x65, 0x11, 0x33};
  static const uint8_t expected_two[] = {0, 0, 0, 1, 0x65, 0x22, 0x44};
  uint8_t packet[32];
  RtpDecoder decoder_one = {0};
  RtpDecoder decoder_two = {0};
  TestResult result_one = {0};
  TestResult result_two = {0};
  size_t packet_size;

  rtp_decoder_init(&decoder_one, CODEC_H264, on_video, &result_one);
  rtp_decoder_init(&decoder_two, CODEC_H264, on_video, &result_two);

  packet_size = create_packet(packet, start_one, sizeof(start_one), 1);
  assert(rtp_decoder_decode(&decoder_one, packet, packet_size) == 0);
  packet_size = create_packet(packet, start_two, sizeof(start_two), 1);
  assert(rtp_decoder_decode(&decoder_two, packet, packet_size) == 0);
  packet_size = create_packet(packet, end_one, sizeof(end_one), 2);
  assert(rtp_decoder_decode(&decoder_one, packet, packet_size) == 0);
  packet_size = create_packet(packet, end_two, sizeof(end_two), 2);
  assert(rtp_decoder_decode(&decoder_two, packet, packet_size) == 0);

  assert(result_one.calls == 1);
  assert(result_one.size == sizeof(expected_one));
  assert(memcmp(result_one.data, expected_one, sizeof(expected_one)) == 0);
  assert(result_two.calls == 1);
  assert(result_two.size == sizeof(expected_two));
  assert(memcmp(result_two.data, expected_two, sizeof(expected_two)) == 0);
  rtp_decoder_deinit(&decoder_one);
  rtp_decoder_deinit(&decoder_two);
}

static void test_nalu_size_limit(void) {
  RtpDecoder decoder = {0};
  TestResult result = {0};
  size_t packet_size = sizeof(RtpHeader) + CONFIG_MAX_NALU_SIZE;
  uint8_t* packet = malloc(packet_size);

  assert(packet != NULL);
  memset(packet, 0, packet_size);
  ((RtpHeader*)packet)->version = 2;
  ((RtpHeader*)packet)->type = PT_H264;
  packet[sizeof(RtpHeader)] = 0x65;
  rtp_decoder_init(&decoder, CODEC_H264, on_video, &result);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) < 0);
  assert(result.calls == 0);
  rtp_decoder_deinit(&decoder);
  free(packet);
}

static void test_stap_a(void) {
  static const uint8_t stap_a[] = {
      0x78,
      0x00, 0x02, 0x67, 0x11,
      0x00, 0x03, 0x68, 0x22, 0x33,
  };
  static const uint8_t expected[] = {
      0x00, 0x00, 0x00, 0x01, 0x67, 0x11,
      0x00, 0x00, 0x00, 0x01, 0x68, 0x22, 0x33,
  };
  uint8_t packet[32];
  RtpDecoder decoder = {0};
  TestResult result = {0};
  size_t packet_size;

  rtp_decoder_init(&decoder, CODEC_H264, on_video, &result);
  packet_size = create_packet(packet, stap_a, sizeof(stap_a), 1);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) > 0);
  assert(result.calls == 2);
  assert(result.size == sizeof(expected));
  assert(memcmp(result.data, expected, sizeof(expected)) == 0);
  rtp_decoder_deinit(&decoder);
}

static void test_invalid_fu_a(void) {
  static const uint8_t orphan_end[] = {0x7c, 0x45, 0x33};
  static const uint8_t start[] = {0x7c, 0x85, 0x11};
  uint8_t packet[32];
  RtpDecoder decoder = {0};
  TestResult result = {0};
  size_t packet_size;

  rtp_decoder_init(&decoder, CODEC_H264, on_video, &result);
  packet_size = create_packet(packet, orphan_end, sizeof(orphan_end), 1);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) < 0);

  packet_size = create_packet(packet, start, sizeof(start), 2);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) == 0);
  rtp_decoder_reset(&decoder);
  packet_size = create_packet(packet, orphan_end, sizeof(orphan_end), 3);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) < 0);
  assert(result.calls == 0);
  rtp_decoder_deinit(&decoder);
}

static void test_buffer_reuse(void) {
  static const uint8_t nalu[] = {0x65, 0x11};
  uint8_t packet[32];
  uint8_t* allocated_buffer;
  RtpDecoder decoder = {0};
  TestResult result = {0};
  size_t packet_size;

  rtp_decoder_init(&decoder, CODEC_H264, on_video, &result);
  packet_size = create_packet(packet, nalu, sizeof(nalu), 1);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) > 0);
  allocated_buffer = decoder.nalu_buffer;
  assert(allocated_buffer != NULL);
  assert(decoder.nalu_capacity == CONFIG_MAX_NALU_SIZE);

  rtp_decoder_reset(&decoder);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) > 0);
  assert(decoder.nalu_buffer == allocated_buffer);
  rtp_decoder_deinit(&decoder);
}

int main(void) {
  static const uint8_t h264_payload[] = {0x65, 0x11, 0x22};
  static const uint8_t expected_h264[] = {0, 0, 0, 1, 0x65, 0x11, 0x22};
  uint8_t packet[64];
  size_t packet_size;
  RtpDecoder decoder = {0};
  TestResult result = {0};

  packet_size = create_extended_packet(packet, h264_payload,
                                       sizeof(h264_payload));
  rtp_decoder_init(&decoder, CODEC_H264, on_video, &result);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) > 0);
  assert(result.size == sizeof(expected_h264));
  assert(memcmp(result.data, expected_h264, sizeof(expected_h264)) == 0);
  assert(result.metadata.sequence_number == 123);
  assert(result.metadata.timestamp == 456);
  assert(result.metadata.ssrc == 789);
  rtp_decoder_deinit(&decoder);

  memset(&decoder, 0, sizeof(decoder));
  memset(&result, 0, sizeof(result));
  rtp_decoder_init(&decoder, CODEC_PCMA, on_audio, &result);
  assert(rtp_decoder_decode(&decoder, packet, packet_size) > 0);
  assert(result.size == sizeof(h264_payload));
  assert(memcmp(result.data, h264_payload, sizeof(h264_payload)) == 0);
  assert(result.metadata.sequence_number == 123);
  assert(result.metadata.timestamp == 456);
  assert(result.metadata.ssrc == 789);
  rtp_decoder_deinit(&decoder);

  test_decoder_isolation();
  test_nalu_size_limit();
  test_stap_a();
  test_invalid_fu_a();
  test_buffer_reuse();
  return 0;
}
