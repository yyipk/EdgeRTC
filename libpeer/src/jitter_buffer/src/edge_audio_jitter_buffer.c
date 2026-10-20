#include "edge_audio_jitter_buffer.h"

#include <string.h>

static int16_t sequence_delta(uint16_t first, uint16_t second)
{
    return (int16_t)(first - second);
}

static int16_t pcma_decode(uint8_t value)
{
    int sample;
    int segment;

    value ^= 0x55;
    sample = (value & 0x0f) << 4;
    segment = (value & 0x70) >> 4;
    if (segment == 0) {
        sample += 8;
    } else if (segment == 1) {
        sample += 0x108;
    } else {
        sample += 0x108;
        sample <<= segment - 1;
    }
    return (value & 0x80) ? (int16_t)sample : (int16_t)-sample;
}

static uint8_t pcma_encode(int16_t sample)
{
    static const int16_t segment_end[] = {
        0x00ff, 0x01ff, 0x03ff, 0x07ff,
        0x0fff, 0x1fff, 0x3fff, 0x7fff,
    };
    int magnitude = sample;
    int segment;
    uint8_t mask;
    uint8_t value;

    if (magnitude >= 0) {
        mask = 0xd5;
    } else {
        mask = 0x55;
        magnitude = -magnitude - 1;
    }
    if (magnitude > 0x7fff) {
        magnitude = 0x7fff;
    }
    for (segment = 0; segment < 8; segment++) {
        if (magnitude <= segment_end[segment]) {
            break;
        }
    }
    if (segment >= 8) {
        return (uint8_t)(0x7f ^ mask);
    }
    value = (uint8_t)(segment << 4);
    value |= (uint8_t)((magnitude >> (segment == 0 ? 4 : segment + 3)) & 0x0f);
    return (uint8_t)(value ^ mask);
}

static void attenuate_pcma(uint8_t *output, const uint8_t *input,
                           int numerator)
{
    size_t index;

    for (index = 0; index < EDGE_AUDIO_JITTER_FRAME_BYTES; index++) {
        int32_t sample = pcma_decode(input[index]);
        output[index] = pcma_encode((int16_t)((sample * numerator) / 4));
    }
}

static void update_noise_level(struct edge_audio_jitter_buffer *jitter,
                               const uint8_t *frame)
{
    uint32_t level = 0;
    size_t index;

    for (index = 0; index < EDGE_AUDIO_JITTER_FRAME_BYTES; index++) {
        int32_t sample = pcma_decode(frame[index]);
        level += (uint32_t)(sample < 0 ? -sample : sample);
    }
    level /= EDGE_AUDIO_JITTER_FRAME_BYTES;
    level /= 8;
    if (level < 16) {
        level = 16;
    } else if (level > 512) {
        level = 512;
    }
    jitter->noise_level = (uint16_t)level;
}

static void generate_comfort_noise(struct edge_audio_jitter_buffer *jitter,
                                   uint8_t *frame)
{
    size_t index;

    for (index = 0; index < EDGE_AUDIO_JITTER_FRAME_BYTES; index++) {
        int32_t target;

        jitter->noise_state ^= jitter->noise_state << 13;
        jitter->noise_state ^= jitter->noise_state >> 17;
        jitter->noise_state ^= jitter->noise_state << 5;
        target = ((int32_t)(jitter->noise_state & 0xff) - 128) *
                 jitter->noise_level / 128;
        jitter->noise_sample = (int16_t)((3 * jitter->noise_sample + target) / 4);
        frame[index] = pcma_encode(jitter->noise_sample);
    }
}

void edge_audio_jitter_reset(struct edge_audio_jitter_buffer *jitter)
{
    if (jitter != NULL) {
        memset(jitter, 0, sizeof(*jitter));
    }
}

static void jitter_initialize(struct edge_audio_jitter_buffer *jitter,
                              uint16_t sequence_number, uint32_t ssrc)
{
    edge_audio_jitter_reset(jitter);
    jitter->expected_sequence = sequence_number;
    jitter->highest_sequence = sequence_number;
    jitter->ssrc = ssrc;
    jitter->noise_state = ((uint32_t)sequence_number << 16) ^ ssrc ^ 0x9e3779b9;
    jitter->initialized = 1;
}

int edge_audio_jitter_push(struct edge_audio_jitter_buffer *jitter,
                           const uint8_t *frame, size_t size,
                           uint16_t sequence_number, uint32_t ssrc)
{
    struct edge_audio_jitter_slot *slot;
    int16_t distance;

    if (jitter == NULL || frame == NULL ||
        size != EDGE_AUDIO_JITTER_FRAME_BYTES) {
        return -1;
    }
    if (!jitter->initialized || jitter->ssrc != ssrc) {
        jitter_initialize(jitter, sequence_number, ssrc);
    }

    distance = sequence_delta(sequence_number, jitter->expected_sequence);
    if (distance < 0) {
        /* A recovered stream can stay slightly behind playout indefinitely;
         * rebuffer after sustained concealment even within the reorder window. */
        if (distance <= -(int16_t)EDGE_AUDIO_JITTER_SLOT_COUNT ||
            jitter->consecutive_losses >= EDGE_AUDIO_JITTER_SLOT_COUNT) {
            jitter_initialize(jitter, sequence_number, ssrc);
        } else {
            return 0;
        }
    }
    if (distance >= EDGE_AUDIO_JITTER_SLOT_COUNT) {
        jitter_initialize(jitter, sequence_number, ssrc);
    }

    slot = &jitter->slots[sequence_number % EDGE_AUDIO_JITTER_SLOT_COUNT];
    if (slot->valid && slot->sequence_number == sequence_number) {
        return 0;
    }
    memcpy(slot->data, frame, EDGE_AUDIO_JITTER_FRAME_BYTES);
    slot->sequence_number = sequence_number;
    slot->valid = 1;
    if (sequence_delta(sequence_number, jitter->highest_sequence) > 0) {
        jitter->highest_sequence = sequence_number;
    }
    return 1;
}

enum edge_audio_jitter_result edge_audio_jitter_get(
    struct edge_audio_jitter_buffer *jitter,
    uint8_t *frame, size_t size)
{
    struct edge_audio_jitter_slot *slot;

    if (jitter == NULL || frame == NULL ||
        size < EDGE_AUDIO_JITTER_FRAME_BYTES) {
        return EDGE_AUDIO_JITTER_ERROR;
    }
    if (!jitter->initialized) {
        return EDGE_AUDIO_JITTER_WAIT;
    }
    if (!jitter->started) {
        if (sequence_delta(jitter->highest_sequence,
                           jitter->expected_sequence) <
            EDGE_AUDIO_JITTER_TARGET_DEPTH) {
            return EDGE_AUDIO_JITTER_WAIT;
        }
        jitter->started = 1;
    }

    slot = &jitter->slots[jitter->expected_sequence %
                          EDGE_AUDIO_JITTER_SLOT_COUNT];
    if (slot->valid && slot->sequence_number == jitter->expected_sequence) {
        memcpy(frame, slot->data, EDGE_AUDIO_JITTER_FRAME_BYTES);
        memcpy(jitter->last_frame, frame, EDGE_AUDIO_JITTER_FRAME_BYTES);
        update_noise_level(jitter, frame);
        jitter->have_last_frame = 1;
        jitter->consecutive_losses = 0;
        slot->valid = 0;
        jitter->expected_sequence++;
        return EDGE_AUDIO_JITTER_FRAME;
    }

    if (jitter->consecutive_losses >= EDGE_AUDIO_JITTER_SLOT_COUNT) {
        /* A stopped sender must not keep the speaker fed with comfort noise. */
        edge_audio_jitter_reset(jitter);
        return EDGE_AUDIO_JITTER_WAIT;
    }

    if (!jitter->have_last_frame || jitter->consecutive_losses >= 3) {
        generate_comfort_noise(jitter, frame);
    } else if (jitter->consecutive_losses == 0) {
        memcpy(frame, jitter->last_frame, EDGE_AUDIO_JITTER_FRAME_BYTES);
    } else {
        attenuate_pcma(frame, jitter->last_frame,
                       jitter->consecutive_losses == 1 ? 3 : 2);
    }
    if (jitter->consecutive_losses < UINT8_MAX) {
        jitter->consecutive_losses++;
    }
    jitter->expected_sequence++;
    return EDGE_AUDIO_JITTER_CONCEALED;
}
