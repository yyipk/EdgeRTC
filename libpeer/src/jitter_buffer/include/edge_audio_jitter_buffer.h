#ifndef EDGE_AUDIO_JITTER_BUFFER_H
#define EDGE_AUDIO_JITTER_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed 8 kHz, 20 ms, 160-byte PCMA receive jitter buffer with PLC. */
#define EDGE_AUDIO_JITTER_FRAME_BYTES  160
#define EDGE_AUDIO_JITTER_SLOT_COUNT    8
#define EDGE_AUDIO_JITTER_TARGET_DEPTH 5

enum edge_audio_jitter_result {
    EDGE_AUDIO_JITTER_ERROR = -1,
    EDGE_AUDIO_JITTER_WAIT = 0,
    EDGE_AUDIO_JITTER_FRAME = 1,
    EDGE_AUDIO_JITTER_CONCEALED = 2,
};

struct edge_audio_jitter_slot {
    uint8_t data[EDGE_AUDIO_JITTER_FRAME_BYTES];
    uint16_t sequence_number;
    uint8_t valid;
};

struct edge_audio_jitter_buffer {
    struct edge_audio_jitter_slot slots[EDGE_AUDIO_JITTER_SLOT_COUNT];
    uint8_t last_frame[EDGE_AUDIO_JITTER_FRAME_BYTES];
    uint16_t expected_sequence;
    uint16_t highest_sequence;
    uint32_t ssrc;
    uint32_t noise_state;
    uint16_t noise_level;
    int16_t noise_sample;
    uint8_t consecutive_losses;
    uint8_t initialized;
    uint8_t started;
    uint8_t have_last_frame;
};

void edge_audio_jitter_reset(struct edge_audio_jitter_buffer *jitter);

int edge_audio_jitter_push(struct edge_audio_jitter_buffer *jitter,
                           const uint8_t *frame, size_t size,
                           uint16_t sequence_number, uint32_t ssrc);

enum edge_audio_jitter_result edge_audio_jitter_get(
    struct edge_audio_jitter_buffer *jitter,
    uint8_t *frame, size_t size);

#ifdef __cplusplus
}
#endif

#endif
