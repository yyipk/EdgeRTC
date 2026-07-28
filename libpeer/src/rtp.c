#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "address.h"
#include "config.h"
#include "peer_connection.h"
#include "rtp.h"
#include "utils.h"

typedef enum RtpH264Type {

  NALU = 23,
  STAP_A = 24,
  FU_A = 28,

} RtpH264Type;

typedef struct NaluHeader {
  uint8_t type : 5;
  uint8_t nri : 2;
  uint8_t f : 1;
} NaluHeader;

typedef struct FuHeader {
  uint8_t type : 5;
  uint8_t r : 1;
  uint8_t e : 1;
  uint8_t s : 1;
} FuHeader;

typedef struct RtpPayload {
  RtpHeader* header;
  uint8_t* data;
  size_t size;
} RtpPayload;

#define RTP_PAYLOAD_SIZE (CONFIG_MTU - sizeof(RtpHeader))
#define FU_PAYLOAD_SIZE (CONFIG_MTU - sizeof(RtpHeader) - sizeof(FuHeader) - sizeof(NaluHeader))

int rtp_packet_validate(uint8_t* packet, size_t size) {
  if (size < 12)
    return 0;

  RtpHeader* rtp_header = (RtpHeader*)packet;
  return ((rtp_header->type < 64) || (rtp_header->type >= 96));
}

uint32_t rtp_get_ssrc(uint8_t* packet) {
  RtpHeader* rtp_header = (RtpHeader*)packet;
  return ntohl(rtp_header->ssrc);
}

static int rtp_encoder_encode_h264_single(RtpEncoder* rtp_encoder, uint8_t* buf, size_t size) {
  RtpPacket* rtp_packet = (RtpPacket*)rtp_encoder->buf;

  rtp_packet->header.version = 2;
  rtp_packet->header.padding = 0;
  rtp_packet->header.extension = 0;
  rtp_packet->header.csrccount = 0;
  rtp_packet->header.markerbit = 0;
  rtp_packet->header.type = rtp_encoder->type;
  rtp_packet->header.seq_number = htons(rtp_encoder->seq_number++);
  rtp_packet->header.timestamp = htonl(rtp_encoder->timestamp);
  rtp_packet->header.ssrc = htonl(rtp_encoder->ssrc);

  // I frame and P frame
  if ((*buf & 0x1f) == 0x05 || (*buf & 0x1f) == 0x01) {
    rtp_packet->header.markerbit = 1;
    rtp_encoder->timestamp += rtp_encoder->timestamp_increment;
  }
#if 0
  LOGI("markbit: %d, timestamp: %d, nalu type: %d", rtp_packet->header.markerbit, rtp_encoder->timestamp, buf[0] & 0x1f);
#endif

  memcpy(rtp_packet->payload, buf, size);
  rtp_encoder->on_packet(rtp_encoder->buf, size + sizeof(RtpHeader), NULL,
                         rtp_encoder->user_data);
  return 0;
}

static int rtp_encoder_encode_h264_fu_a(RtpEncoder* rtp_encoder, uint8_t* buf, size_t size) {
  RtpPacket* rtp_packet = (RtpPacket*)rtp_encoder->buf;

  rtp_packet->header.version = 2;
  rtp_packet->header.padding = 0;
  rtp_packet->header.extension = 0;
  rtp_packet->header.csrccount = 0;
  rtp_packet->header.markerbit = 0;
  rtp_packet->header.type = rtp_encoder->type;
  rtp_packet->header.timestamp = htonl(rtp_encoder->timestamp);
  rtp_packet->header.ssrc = htonl(rtp_encoder->ssrc);
  uint8_t type = buf[0] & 0x1f;
  uint8_t nri = (buf[0] & 0x60) >> 5;
  buf = buf + 1;
  size = size - 1;

  // increase timestamp if I, P frame
  if (type == 0x05 || type == 0x01) {
    rtp_encoder->timestamp += rtp_encoder->timestamp_increment;
  }

  NaluHeader* fu_indicator = (NaluHeader*)rtp_packet->payload;
  FuHeader* fu_header = (FuHeader*)rtp_packet->payload + sizeof(NaluHeader);
  fu_header->s = 1;

  while (size > 0) {
    fu_indicator->type = FU_A;
    fu_indicator->nri = nri;
    fu_indicator->f = 0;
    fu_header->type = type;
    fu_header->r = 0;
    rtp_packet->header.seq_number = htons(rtp_encoder->seq_number++);

    if (size <= FU_PAYLOAD_SIZE) {
      fu_header->e = 1;
      rtp_packet->header.markerbit = 1;
      memcpy(rtp_packet->payload + sizeof(NaluHeader) + sizeof(FuHeader), buf, size);
      rtp_encoder->on_packet(rtp_encoder->buf,
                             size + sizeof(RtpHeader) + sizeof(NaluHeader) +
                                 sizeof(FuHeader),
                             NULL, rtp_encoder->user_data);
      break;
    }

    fu_header->e = 0;

    memcpy(rtp_packet->payload + sizeof(NaluHeader) + sizeof(FuHeader), buf, FU_PAYLOAD_SIZE);
    rtp_encoder->on_packet(rtp_encoder->buf, CONFIG_MTU, NULL,
                           rtp_encoder->user_data);
    size -= FU_PAYLOAD_SIZE;
    buf += FU_PAYLOAD_SIZE;

    fu_header->s = 0;
  }
  return 0;
}

static uint8_t* h264_find_nalu(uint8_t* buf_start, uint8_t* buf_end) {
  uint8_t* p = buf_start + 2;

  while (p < buf_end) {
    if (*(p - 2) == 0x00 && *(p - 1) == 0x00 && *p == 0x01)
      return p + 1;
    p++;
  }

  return buf_end;
}

static int rtp_encoder_encode_h264(RtpEncoder* rtp_encoder, uint8_t* buf, size_t size) {
  uint8_t* buf_end = buf + size;
  uint8_t *pstart, *pend;
  size_t nalu_size;

  for (pstart = h264_find_nalu(buf, buf_end); pstart < buf_end; pstart = pend) {
    pend = h264_find_nalu(pstart, buf_end);
    nalu_size = pend - pstart;

    if (pend != buf_end)
      nalu_size--;

    while (pstart[nalu_size - 1] == 0x00)
      nalu_size--;

    if (nalu_size <= RTP_PAYLOAD_SIZE) {
      rtp_encoder_encode_h264_single(rtp_encoder, pstart, nalu_size);

    } else {
      rtp_encoder_encode_h264_fu_a(rtp_encoder, pstart, nalu_size);
    }
  }

  return 0;
}

static int rtp_encoder_encode_generic(RtpEncoder* rtp_encoder, uint8_t* buf, size_t size) {
  RtpHeader* rtp_header = (RtpHeader*)rtp_encoder->buf;
  rtp_header->version = 2;
  rtp_header->padding = 0;
  rtp_header->extension = 0;
  rtp_header->csrccount = 0;
  rtp_header->markerbit = 0;
  rtp_header->type = rtp_encoder->type;
  rtp_header->seq_number = htons(rtp_encoder->seq_number++);
  rtp_header->timestamp = htonl(rtp_encoder->timestamp);
  rtp_encoder->timestamp += rtp_encoder->timestamp_increment;
  rtp_header->ssrc = htonl(rtp_encoder->ssrc);
  memcpy(rtp_encoder->buf + sizeof(RtpHeader), buf, size);

  rtp_encoder->on_packet(rtp_encoder->buf, size + sizeof(RtpHeader), NULL,
                         rtp_encoder->user_data);

  return 0;
}

void rtp_encoder_init(RtpEncoder* rtp_encoder, MediaCodec codec,
                      RtpOnPacket on_packet, void* user_data) {
  rtp_encoder->on_packet = on_packet;
  rtp_encoder->user_data = user_data;
  rtp_encoder->timestamp = 0;
  rtp_encoder->seq_number = 0;

  switch (codec) {
    case CODEC_H264:
      rtp_encoder->type = PT_H264;
      rtp_encoder->ssrc = SSRC_H264;
      rtp_encoder->timestamp_increment = 90000 / 30;  // 30 FPS.
      rtp_encoder->encode_func = rtp_encoder_encode_h264;
      break;
    case CODEC_PCMA:
      rtp_encoder->type = PT_PCMA;
      rtp_encoder->ssrc = SSRC_PCMA;
      rtp_encoder->timestamp_increment = CONFIG_AUDIO_DURATION * 8000 / 1000;
      rtp_encoder->encode_func = rtp_encoder_encode_generic;
      break;
    case CODEC_PCMU:
      rtp_encoder->type = PT_PCMU;
      rtp_encoder->ssrc = SSRC_PCMU;
      rtp_encoder->timestamp_increment = CONFIG_AUDIO_DURATION * 8000 / 1000;
      rtp_encoder->encode_func = rtp_encoder_encode_generic;
      break;
    case CODEC_OPUS:
      rtp_encoder->type = PT_OPUS;
      rtp_encoder->ssrc = SSRC_OPUS;
      rtp_encoder->timestamp_increment = CONFIG_AUDIO_DURATION * 48000 / 1000;
      rtp_encoder->encode_func = rtp_encoder_encode_generic;
      break;
    default:
      break;
  }
}

int rtp_encoder_encode(RtpEncoder* rtp_encoder, const uint8_t* buf, size_t size) {
  return rtp_encoder->encode_func(rtp_encoder, (uint8_t*)buf, size);
}

static int rtp_parse_payload(uint8_t* buf, size_t size, RtpPayload* payload) {
  RtpHeader* rtp_header;
  size_t header_size;
  uint16_t extension_words;
  uint8_t padding_size = 0;

  rtp_header = (RtpHeader*)buf;
  header_size = sizeof(RtpHeader) +
                rtp_header->csrccount * sizeof(uint32_t);
  if (size < header_size) {
    return -1;
  }

  if (rtp_header->extension) {
    if (size < header_size + 4) {
      return -1;
    }
    memcpy(&extension_words, buf + header_size + 2,
           sizeof(extension_words));
    header_size += 4 + ntohs(extension_words) * sizeof(uint32_t);
    if (size < header_size) {
      return -1;
    }
  }

  if (rtp_header->padding) {
    padding_size = buf[size - 1];
    if (padding_size == 0 || padding_size > size - header_size) {
      return -1;
    }
  }

  payload->header = rtp_header;
  payload->data = buf + header_size;
  payload->size = size - header_size - padding_size;
  return 0;
}

static int rtp_decoder_alloc_nalu_buffer(RtpDecoder* rtp_decoder) {
  if (rtp_decoder->nalu_buffer != NULL) {
    return 0;
  }

  rtp_decoder->nalu_buffer = malloc(CONFIG_MAX_NALU_SIZE);
  if (rtp_decoder->nalu_buffer == NULL) {
    return -1;
  }
  rtp_decoder->nalu_capacity = CONFIG_MAX_NALU_SIZE;
  return 0;
}

static int rtp_decoder_emit_nalu(RtpDecoder* rtp_decoder,
                                 const uint8_t* data, size_t size,
                                 const RtpPacketMetadata* metadata) {
  static const uint8_t nalu_start_code[] = {0x00, 0x00, 0x00, 0x01};
  size_t output_size = sizeof(nalu_start_code) + size;

  if (size == 0 || output_size > CONFIG_MAX_NALU_SIZE ||
      rtp_decoder_alloc_nalu_buffer(rtp_decoder) < 0) {
    rtp_decoder_reset(rtp_decoder);
    return -1;
  }

  memcpy(rtp_decoder->nalu_buffer, nalu_start_code,
         sizeof(nalu_start_code));
  memcpy(rtp_decoder->nalu_buffer + sizeof(nalu_start_code), data, size);
  rtp_decoder->nalu_size = output_size;
  if (rtp_decoder->on_packet != NULL) {
    rtp_decoder->on_packet(rtp_decoder->nalu_buffer,
                           rtp_decoder->nalu_size,
                           metadata,
                           rtp_decoder->user_data);
  }
  rtp_decoder_reset(rtp_decoder);
  return 0;
}

static int rtp_decode_h264_stap_a(RtpDecoder* rtp_decoder,
                                  const RtpPayload* payload,
                                  const RtpPacketMetadata* metadata) {
  size_t offset = 1;
  size_t nalu_size;
  int nalu_count = 0;

  rtp_decoder_reset(rtp_decoder);

  /* Validate the complete aggregation packet before emitting any NALU. */
  while (offset < payload->size) {
    if (payload->size - offset < sizeof(uint16_t)) {
      return -1;
    }
    nalu_size = ((size_t)payload->data[offset] << 8) |
                payload->data[offset + 1];
    offset += sizeof(uint16_t);
    if (nalu_size == 0 ||
        nalu_size > CONFIG_MAX_NALU_SIZE - 4 ||
        nalu_size > payload->size - offset ||
        (payload->data[offset] & 0x1f) == 0 ||
        (payload->data[offset] & 0x1f) > NALU) {
      return -1;
    }
    offset += nalu_size;
    nalu_count++;
  }

  if (nalu_count == 0) {
    return -1;
  }

  offset = 1;
  while (offset < payload->size) {
    nalu_size = ((size_t)payload->data[offset] << 8) |
                payload->data[offset + 1];
    offset += sizeof(uint16_t);
    if (rtp_decoder_emit_nalu(rtp_decoder, payload->data + offset,
                              nalu_size, metadata) < 0) {
      return -1;
    }
    offset += nalu_size;
  }
  return 0;
}

static int rtp_decode_h264(RtpDecoder* rtp_decoder, uint8_t* buf, size_t size) {
  static const uint8_t nalu_start_code[] = {0x00, 0x00, 0x00, 0x01};
  RtpPayload payload;
  uint8_t nalu_type;
  uint8_t fu_indicator;
  uint8_t fu_header;
  uint8_t reconstructed_nalu_type;
  size_t fragment_size;
  size_t required_size;
  RtpPacketMetadata metadata;

  if (rtp_parse_payload(buf, size, &payload) < 0 || payload.size == 0) {
    rtp_decoder_reset(rtp_decoder);
    return -1;
  }

  metadata.sequence_number = ntohs(payload.header->seq_number);
  metadata.timestamp = ntohl(payload.header->timestamp);
  metadata.ssrc = ntohl(payload.header->ssrc);

  nalu_type = *payload.data & 0x1f;
  if (nalu_type > 0 && nalu_type < 24) {
    rtp_decoder_reset(rtp_decoder);
    return rtp_decoder_emit_nalu(rtp_decoder, payload.data, payload.size,
                                 &metadata) < 0
               ? -1
               : (int)size;
  }

  if (nalu_type == STAP_A) {
    return rtp_decode_h264_stap_a(rtp_decoder, &payload, &metadata) < 0
               ? -1
               : (int)size;
  }

  if (nalu_type != FU_A || payload.size <= 2) {
    rtp_decoder_reset(rtp_decoder);
    return -1;
  }

  fu_indicator = payload.data[0];
  fu_header = payload.data[1];
  fragment_size = payload.size - 2;
  reconstructed_nalu_type = (fu_indicator & 0xe0) | (fu_header & 0x1f);

  if ((fu_header & 0x20) != 0 ||
      (fu_header & 0x1f) == 0 || (fu_header & 0x1f) > NALU ||
      (fu_header & 0xc0) == 0xc0) {
    rtp_decoder_reset(rtp_decoder);
    return -1;
  }

  if ((fu_header & 0x80) != 0) {
    rtp_decoder_reset(rtp_decoder);
    required_size = sizeof(nalu_start_code) + 1 + fragment_size;
    if (required_size > CONFIG_MAX_NALU_SIZE ||
        rtp_decoder_alloc_nalu_buffer(rtp_decoder) < 0) {
      return -1;
    }
    memcpy(rtp_decoder->nalu_buffer, nalu_start_code,
           sizeof(nalu_start_code));
    rtp_decoder->nalu_buffer[sizeof(nalu_start_code)] =
        reconstructed_nalu_type;
    memcpy(rtp_decoder->nalu_buffer + sizeof(nalu_start_code) + 1,
           payload.data + 2, fragment_size);
    rtp_decoder->nalu_size = required_size;
    rtp_decoder->nalu_active = 1;
    return 0;
  }

  if (!rtp_decoder->nalu_active || rtp_decoder->nalu_size < 5 ||
      rtp_decoder->nalu_buffer[4] != reconstructed_nalu_type ||
      fragment_size > CONFIG_MAX_NALU_SIZE - rtp_decoder->nalu_size) {
    rtp_decoder_reset(rtp_decoder);
    return -1;
  }

  memcpy(rtp_decoder->nalu_buffer + rtp_decoder->nalu_size,
         payload.data + 2, fragment_size);
  rtp_decoder->nalu_size += fragment_size;
  if ((fu_header & 0x40) != 0) {
    if (rtp_decoder->on_packet != NULL) {
      rtp_decoder->on_packet(rtp_decoder->nalu_buffer,
                             rtp_decoder->nalu_size,
                             &metadata,
                             rtp_decoder->user_data);
    }
    rtp_decoder_reset(rtp_decoder);
  }
  return 0;
}

static int rtp_decode_generic(RtpDecoder* rtp_decoder, uint8_t* buf, size_t size) {
  RtpPayload payload;
  RtpPacketMetadata metadata;

  if (rtp_parse_payload(buf, size, &payload) < 0) {
    return -1;
  }
  if (rtp_decoder->on_packet != NULL) {
    metadata.sequence_number = ntohs(payload.header->seq_number);
    metadata.timestamp = ntohl(payload.header->timestamp);
    metadata.ssrc = ntohl(payload.header->ssrc);
    rtp_decoder->on_packet(payload.data, payload.size, &metadata,
                           rtp_decoder->user_data);
  }
  // even if there is no callback set, assume everything is ok for caller and do not return an error
  return (int)size;
}

void rtp_decoder_init(RtpDecoder* rtp_decoder, MediaCodec codec,
                      RtpOnPacket on_packet, void* user_data) {
  rtp_decoder->on_packet = on_packet;
  rtp_decoder->decode_func = NULL;
  rtp_decoder->nalu_buffer = NULL;
  rtp_decoder->nalu_size = 0;
  rtp_decoder->nalu_capacity = 0;
  rtp_decoder->nalu_active = 0;
  rtp_decoder->user_data = user_data;

  switch (codec) {
    case CODEC_H264:
      rtp_decoder->decode_func = rtp_decode_h264;
      break;
    case CODEC_PCMA:
    case CODEC_PCMU:
    case CODEC_OPUS:
      rtp_decoder->decode_func = rtp_decode_generic;
    default:
      break;
  }
}

void rtp_decoder_reset(RtpDecoder* rtp_decoder) {
  rtp_decoder->nalu_size = 0;
  rtp_decoder->nalu_active = 0;
}

void rtp_decoder_deinit(RtpDecoder* rtp_decoder) {
  free(rtp_decoder->nalu_buffer);
  rtp_decoder->nalu_buffer = NULL;
  rtp_decoder->nalu_capacity = 0;
  rtp_decoder_reset(rtp_decoder);
}

int rtp_decoder_decode(RtpDecoder* rtp_decoder, const uint8_t* buf, size_t size) {
  if (rtp_decoder->decode_func == NULL)
    return -1;
  return rtp_decoder->decode_func(rtp_decoder, (uint8_t*)buf, size);
}
