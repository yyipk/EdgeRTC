#include "rtp_jitter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RTP_JITTER_LOG_ENABLE
#define RTP_JITTER_LOG(...) printf(__VA_ARGS__)
#else
#define RTP_JITTER_LOG(...)    \
  do {                         \
    if (0) printf(__VA_ARGS__); \
  } while (0)
#endif

#define RTP_JITTER_MIN_PACKET_SIZE 12U
#define RTP_JITTER_MAX_SLOTS INT16_MAX

typedef struct RtpJitterSlot {
  uint8_t* packet;
  uint32_t ext_seq;
  uint32_t nack_ext_seq;
  uint32_t arrival_ms;
  uint16_t size;
  bool valid;
  bool nack_requested;
} RtpJitterSlot;

struct RtpJitter {
  RtpJitterConfig config;
  uint32_t sender_ssrc;
  RtpJitterOnPacket on_packet;
  void* user_data;
  size_t slot_count;
  RtpJitterSlot* slots;
  uint8_t* packet_storage;
  uint32_t expected_ext_seq;
  uint32_t highest_ext_seq;
  uint32_t gap_since_ms;
  uint32_t last_nack_ms;
  uint32_t media_ssrc;
  bool initialized;
  bool gap_active;
  bool have_last_nack;
};

static bool elapsed_at_least(uint32_t now_ms, uint32_t start_ms,
                             uint32_t duration_ms) {
  return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

static size_t calculate_slot_count(const RtpJitterConfig* config) {
  uint64_t window_packets;
  uint64_t slot_count;

  window_packets = ((uint64_t)config->max_packets_per_sec *
                    config->max_timeout_ms + 999U) /
                   1000U;
  slot_count = window_packets + config->reorder_margin;
  if (slot_count == 0 || slot_count > RTP_JITTER_MAX_SLOTS) {
    return 0;
  }
  return (size_t)slot_count;
}

static RtpJitterSlot* find_slot(RtpJitter* jitter, uint32_t ext_seq) {
  RtpJitterSlot* slot = &jitter->slots[ext_seq % jitter->slot_count];

  return slot->valid && slot->ext_seq == ext_seq ? slot : NULL;
}

static bool take_nack_request(RtpJitter* jitter, uint32_t ext_seq) {
  RtpJitterSlot* slot = &jitter->slots[ext_seq % jitter->slot_count];

  if (!slot->nack_requested || slot->nack_ext_seq != ext_seq) {
    return false;
  }
  slot->nack_requested = false;
  return true;
}

static unsigned mark_nack_fci(RtpJitter* jitter, uint32_t pid_ext_seq,
                              uint16_t blp) {
  unsigned offset;
  unsigned count = 0;

  for (offset = 0; offset <= 16; offset++) {
    uint32_t ext_seq;
    RtpJitterSlot* slot;

    if (offset != 0 && (blp & (uint16_t)(1U << (offset - 1))) == 0) {
      continue;
    }
    ext_seq = pid_ext_seq + offset;
    slot = &jitter->slots[ext_seq % jitter->slot_count];
    slot->nack_ext_seq = ext_seq;
    slot->nack_requested = true;
    count++;
  }
  return count;
}

static unsigned clear_nack_range(RtpJitter* jitter, uint32_t first,
                                 uint32_t last) {
  unsigned count = 0;

  for (;;) {
    if (take_nack_request(jitter, first)) {
      count++;
    }
    if (first == last) {
      break;
    }
    first++;
  }
  return count;
}

static void log_recovered_after_nack(RtpJitter* jitter, uint32_t ext_seq,
                                     uint32_t now_ms) {
  if (take_nack_request(jitter, ext_seq)) {
    RTP_JITTER_LOG("[rtp_jitter] recovered after NACK: media_ssrc=0x%08x "
                   "seq=%u at_ms=%u\n",
                   (unsigned)jitter->media_ssrc,
                   (unsigned)(uint16_t)ext_seq, (unsigned)now_ms);
  }
}

static bool extend_sequence(const RtpJitter* jitter, uint16_t sequence,
                            uint32_t* ext_seq) {
  uint16_t reference = (uint16_t)jitter->highest_ext_seq;
  int16_t delta = (int16_t)(sequence - reference);

  if (delta < 0 && (uint32_t)(-(int32_t)delta) > jitter->highest_ext_seq) {
    return false;
  }
  *ext_seq = jitter->highest_ext_seq + delta;
  return true;
}

static void clear_slots(RtpJitter* jitter) {
  size_t index;

  for (index = 0; index < jitter->slot_count; index++) {
    jitter->slots[index].valid = false;
    jitter->slots[index].nack_requested = false;
  }
}

static void start_window(RtpJitter* jitter, uint32_t ext_seq) {
  clear_slots(jitter);
  jitter->expected_ext_seq = ext_seq;
  jitter->highest_ext_seq = ext_seq;
  jitter->gap_since_ms = 0;
  jitter->last_nack_ms = 0;
  jitter->initialized = true;
  jitter->gap_active = false;
  jitter->have_last_nack = false;
}

static RtpJitterSlot* find_first_buffered(RtpJitter* jitter) {
  uint32_t ext_seq;

  for (ext_seq = jitter->expected_ext_seq;
       ext_seq <= jitter->highest_ext_seq; ext_seq++) {
    RtpJitterSlot* slot = find_slot(jitter, ext_seq);

    if (slot != NULL) {
      return slot;
    }
  }
  return NULL;
}

static uint32_t find_oldest_arrival(RtpJitter* jitter, uint32_t now_ms) {
  uint32_t oldest_arrival = now_ms;
  uint32_t oldest_age = 0;
  size_t index;

  for (index = 0; index < jitter->slot_count; index++) {
    RtpJitterSlot* slot = &jitter->slots[index];
    uint32_t age;

    if (!slot->valid || slot->ext_seq <= jitter->expected_ext_seq) {
      continue;
    }
    age = now_ms - slot->arrival_ms;
    if (age >= oldest_age) {
      oldest_age = age;
      oldest_arrival = slot->arrival_ms;
    }
  }
  return oldest_arrival;
}

static int flush_contiguous(RtpJitter* jitter) {
  int result = 0;

  while (jitter->expected_ext_seq <= jitter->highest_ext_seq) {
    RtpJitterSlot* slot = find_slot(jitter, jitter->expected_ext_seq);
    int callback_result;

    if (slot == NULL) {
      break;
    }
    callback_result = jitter->on_packet(slot->packet, slot->size,
                                        jitter->user_data);
    if (callback_result < 0) {
      result = callback_result;
    }
    slot->valid = false;
    jitter->expected_ext_seq++;
  }
  return result;
}

static void update_gap_state(RtpJitter* jitter, uint32_t previous_expected,
                             bool had_gap, uint32_t now_ms) {
  RtpJitterSlot* first;

  if (jitter->expected_ext_seq > jitter->highest_ext_seq) {
    jitter->gap_active = false;
    jitter->have_last_nack = false;
    return;
  }
  first = find_first_buffered(jitter);
  if (first == NULL || first->ext_seq == jitter->expected_ext_seq) {
    jitter->gap_active = false;
    jitter->have_last_nack = false;
    return;
  }
  jitter->gap_active = true;
  if (!had_gap || previous_expected != jitter->expected_ext_seq) {
    jitter->gap_since_ms = find_oldest_arrival(jitter, now_ms);
    jitter->have_last_nack = false;
    RTP_JITTER_LOG("[rtp_jitter] packet loss detected: media_ssrc=0x%08x "
                   "missing_seq=%u-%u highest_seq=%u\n",
                   (unsigned)jitter->media_ssrc,
                   (unsigned)(uint16_t)jitter->expected_ext_seq,
                   (unsigned)(uint16_t)(first->ext_seq - 1),
                   (unsigned)(uint16_t)jitter->highest_ext_seq);
  }
}

static void write_u16(uint8_t* destination, uint16_t value) {
  destination[0] = (uint8_t)(value >> 8);
  destination[1] = (uint8_t)value;
}

static void write_u32(uint8_t* destination, uint32_t value) {
  destination[0] = (uint8_t)(value >> 24);
  destination[1] = (uint8_t)(value >> 16);
  destination[2] = (uint8_t)(value >> 8);
  destination[3] = (uint8_t)value;
}

static int finish_nack_fci(uint8_t* packet, size_t capacity, size_t* position,
                           uint16_t pid, uint16_t blp) {
  if (*position + 4 > capacity) {
    return -1;
  }
  write_u16(packet + *position, pid);
  write_u16(packet + *position + 2, blp);
  *position += 4;
  return 0;
}

RtpJitter* rtp_jitter_create(const RtpJitterConfig* config,
                             uint32_t sender_ssrc,
                             RtpJitterOnPacket on_packet, void* user_data) {
  RtpJitter* jitter;
  size_t slot_count;
  size_t slots_size;
  size_t packet_size;
  size_t total_size;
  size_t index;

  if (config == NULL || config->max_packet_size < RTP_JITTER_MIN_PACKET_SIZE ||
      config->max_packet_size > UINT16_MAX || config->max_timeout_ms == 0 ||
      config->max_packets_per_sec == 0 || on_packet == NULL) {
    return NULL;
  }
  slot_count = calculate_slot_count(config);
  if (slot_count == 0 ||
      slot_count > SIZE_MAX / sizeof(RtpJitterSlot)) {
    return NULL;
  }
  slots_size = slot_count * sizeof(RtpJitterSlot);
  if (config->max_packet_size > SIZE_MAX / slot_count) {
    return NULL;
  }
  packet_size = config->max_packet_size * slot_count;
  if (sizeof(*jitter) > SIZE_MAX - slots_size ||
      sizeof(*jitter) + slots_size > SIZE_MAX - packet_size) {
    return NULL;
  }
  total_size = sizeof(*jitter) + slots_size + packet_size;
  jitter = calloc(1, total_size);
  if (jitter == NULL) {
    return NULL;
  }

  jitter->config = *config;
  jitter->sender_ssrc = sender_ssrc;
  jitter->on_packet = on_packet;
  jitter->user_data = user_data;
  jitter->slot_count = slot_count;
  jitter->slots = (RtpJitterSlot*)(jitter + 1);
  jitter->packet_storage = (uint8_t*)(jitter->slots + slot_count);
  for (index = 0; index < slot_count; index++) {
    jitter->slots[index].packet =
        jitter->packet_storage + index * config->max_packet_size;
  }
  return jitter;
}

void rtp_jitter_destroy(RtpJitter* jitter) {
  free(jitter);
}

void rtp_jitter_reset(RtpJitter* jitter) {
  if (jitter == NULL) {
    return;
  }
  clear_slots(jitter);
  jitter->initialized = false;
  jitter->gap_active = false;
  jitter->have_last_nack = false;
  jitter->media_ssrc = 0;
}

int rtp_jitter_add(RtpJitter* jitter, const uint8_t* packet, size_t size,
                   uint32_t now_ms) {
  uint16_t sequence;
  uint32_t media_ssrc;
  uint32_t ext_seq;
  uint32_t previous_expected;
  bool had_gap;
  RtpJitterSlot* slot;
  int result;

  if (jitter == NULL || packet == NULL || size < RTP_JITTER_MIN_PACKET_SIZE ||
      size > jitter->config.max_packet_size) {
    return -1;
  }
  sequence = (uint16_t)(((uint16_t)packet[2] << 8) | packet[3]);
  media_ssrc = ((uint32_t)packet[8] << 24) |
               ((uint32_t)packet[9] << 16) |
               ((uint32_t)packet[10] << 8) | packet[11];
  if (!jitter->initialized || jitter->media_ssrc != media_ssrc) {
    jitter->media_ssrc = media_ssrc;
    start_window(jitter, sequence);
    RTP_JITTER_LOG("[rtp_jitter] stream started: media_ssrc=0x%08x "
                   "first_seq=%u\n",
                   (unsigned)media_ssrc, (unsigned)sequence);
  }
  if (!extend_sequence(jitter, sequence, &ext_seq)) {
    return 1;
  }
  if (ext_seq < jitter->expected_ext_seq) {
    return 1;
  }
  if (ext_seq - jitter->expected_ext_seq > jitter->slot_count) {
    start_window(jitter, ext_seq);
  }

  previous_expected = jitter->expected_ext_seq;
  had_gap = jitter->gap_active;
  if (ext_seq == jitter->expected_ext_seq) {
    log_recovered_after_nack(jitter, ext_seq, now_ms);
    result = jitter->on_packet(packet, size, jitter->user_data);
    if (ext_seq > jitter->highest_ext_seq) {
      jitter->highest_ext_seq = ext_seq;
    }
    jitter->expected_ext_seq++;
    if (flush_contiguous(jitter) < 0) {
      result = -1;
    }
    update_gap_state(jitter, previous_expected, had_gap, now_ms);
    return result < 0 ? result : 0;
  }

  slot = &jitter->slots[ext_seq % jitter->slot_count];
  if (slot->valid) {
    if (slot->ext_seq == ext_seq) {
      return 1;
    }
    start_window(jitter, ext_seq);
    slot = &jitter->slots[ext_seq % jitter->slot_count];
  }
  log_recovered_after_nack(jitter, ext_seq, now_ms);
  memcpy(slot->packet, packet, size);
  slot->size = (uint16_t)size;
  slot->ext_seq = ext_seq;
  slot->arrival_ms = now_ms;
  slot->valid = true;
  if (ext_seq > jitter->highest_ext_seq) {
    jitter->highest_ext_seq = ext_seq;
  }
  update_gap_state(jitter, previous_expected, had_gap, now_ms);
  return 0;
}

int rtp_jitter_get_nack(RtpJitter* jitter, uint32_t now_ms, uint8_t* packet,
                        size_t capacity) {
  size_t position = 12;
  uint32_t ext_seq;
  bool have_fci = false;
  uint16_t pid = 0;
  uint16_t blp = 0;
  uint32_t pid_ext_seq = 0;
  size_t fci_count = 0;
  unsigned missing_count;

  if (jitter == NULL || packet == NULL || capacity < 16 ||
      jitter->config.nack_interval_ms == 0 ||
      !jitter->gap_active ||
      (jitter->have_last_nack &&
       !elapsed_at_least(now_ms, jitter->last_nack_ms,
                         jitter->config.nack_interval_ms))) {
    return 0;
  }
  for (ext_seq = jitter->expected_ext_seq;
       ext_seq <= jitter->highest_ext_seq; ext_seq++) {
    uint16_t sequence;
    uint16_t delta;

    if (find_slot(jitter, ext_seq) != NULL) {
      continue;
    }
    sequence = (uint16_t)ext_seq;
    if (!have_fci) {
      pid = sequence;
      pid_ext_seq = ext_seq;
      blp = 0;
      have_fci = true;
      continue;
    }
    delta = (uint16_t)(sequence - pid);
    if (delta >= 1 && delta <= 16) {
      blp |= (uint16_t)(1U << (delta - 1));
      continue;
    }
    if (finish_nack_fci(packet, capacity, &position, pid, blp) < 0) {
      have_fci = false;
      break;
    }
    missing_count = mark_nack_fci(jitter, pid_ext_seq, blp);
    RTP_JITTER_LOG("[rtp_jitter] NACK generated: media_ssrc=0x%08x pid=%u "
                   "blp=0x%04x missing=%u at_ms=%u\n",
                   (unsigned)jitter->media_ssrc, (unsigned)pid,
                   (unsigned)blp, missing_count, (unsigned)now_ms);
    fci_count++;
    pid = sequence;
    pid_ext_seq = ext_seq;
    blp = 0;
  }
  if (have_fci &&
      finish_nack_fci(packet, capacity, &position, pid, blp) == 0) {
    missing_count = mark_nack_fci(jitter, pid_ext_seq, blp);
    RTP_JITTER_LOG("[rtp_jitter] NACK generated: media_ssrc=0x%08x pid=%u "
                   "blp=0x%04x missing=%u at_ms=%u\n",
                   (unsigned)jitter->media_ssrc, (unsigned)pid,
                   (unsigned)blp, missing_count, (unsigned)now_ms);
    fci_count++;
  }
  if (fci_count == 0) {
    return 0;
  }

  packet[0] = 0x81;
  packet[1] = 205;
  write_u16(packet + 2, (uint16_t)(position / 4 - 1));
  write_u32(packet + 4, jitter->sender_ssrc);
  write_u32(packet + 8, jitter->media_ssrc);
  jitter->last_nack_ms = now_ms;
  jitter->have_last_nack = true;
  return (int)position;
}

int rtp_jitter_poll(RtpJitter* jitter, uint32_t now_ms) {
  uint32_t previous_expected;
  bool had_gap;
  int skipped = 0;

  if (jitter == NULL) {
    return 0;
  }
  while (jitter->gap_active &&
         elapsed_at_least(now_ms, jitter->gap_since_ms,
                          jitter->config.max_timeout_ms)) {
    RtpJitterSlot* first = find_first_buffered(jitter);
    unsigned nacked;

    if (first == NULL) {
      jitter->gap_active = false;
      break;
    }
    previous_expected = jitter->expected_ext_seq;
    had_gap = jitter->gap_active;
    nacked = clear_nack_range(jitter, jitter->expected_ext_seq,
                              first->ext_seq - 1);
    RTP_JITTER_LOG(
        "[rtp_jitter] recovery timeout, packet dropped: "
        "media_ssrc=0x%08x seq=%u-%u count=%u nacked=%u waited_ms=%u\n",
        (unsigned)jitter->media_ssrc,
        (unsigned)(uint16_t)jitter->expected_ext_seq,
        (unsigned)(uint16_t)(first->ext_seq - 1),
        (unsigned)(first->ext_seq - jitter->expected_ext_seq), nacked,
        (unsigned)(now_ms - jitter->gap_since_ms));
    skipped += (int)(first->ext_seq - jitter->expected_ext_seq);
    jitter->expected_ext_seq = first->ext_seq;
    flush_contiguous(jitter);
    update_gap_state(jitter, previous_expected, had_gap, now_ms);
  }
  return skipped;
}
