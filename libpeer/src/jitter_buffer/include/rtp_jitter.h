#ifndef RTP_JITTER_H_
#define RTP_JITTER_H_

#include <stddef.h>
#include <stdint.h>

#ifndef RTP_JITTER_LOG_ENABLE
#define RTP_JITTER_LOG_ENABLE 1
#endif

typedef struct RtpJitter RtpJitter;

typedef int (*RtpJitterOnPacket)(const uint8_t* packet, size_t size,
                                 void* user_data);

typedef struct RtpJitterConfig {
  size_t max_packet_size;
  uint32_t max_timeout_ms;
  uint32_t max_packets_per_sec;
  uint16_t reorder_margin;
  uint16_t nack_interval_ms;
} RtpJitterConfig;

RtpJitter* rtp_jitter_create(const RtpJitterConfig* config,
                             uint32_t sender_ssrc,
                             RtpJitterOnPacket on_packet, void* user_data);

void rtp_jitter_destroy(RtpJitter* jitter);

void rtp_jitter_reset(RtpJitter* jitter);

int rtp_jitter_add(RtpJitter* jitter, const uint8_t* packet, size_t size,
                   uint32_t now_ms);

int rtp_jitter_get_nack(RtpJitter* jitter, uint32_t now_ms, uint8_t* packet,
                        size_t capacity);

int rtp_jitter_poll(RtpJitter* jitter, uint32_t now_ms);

#endif  // RTP_JITTER_H_
