#include "edge_audio_jitter_buffer.h"

#include <assert.h>
#include <string.h>

static void fill(uint8_t *frame, uint8_t value)
{
    memset(frame, value, EDGE_AUDIO_JITTER_FRAME_BYTES);
}

static int is_silence(const uint8_t *frame)
{
    size_t index;

    for (index = 0; index < EDGE_AUDIO_JITTER_FRAME_BYTES; index++) {
        if (frame[index] != 0xd5) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    struct edge_audio_jitter_buffer jitter;
    uint8_t input[EDGE_AUDIO_JITTER_FRAME_BYTES];
    uint8_t output[EDGE_AUDIO_JITTER_FRAME_BYTES];

    edge_audio_jitter_reset(&jitter);
    fill(input, 0xd5);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_WAIT);

    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 100, 1) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 102, 1) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 101, 1) == 1);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_WAIT);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 103, 1) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 104, 1) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 105, 1) == 1);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);

    edge_audio_jitter_reset(&jitter);
    fill(input, 0xaa);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 200, 2) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 202, 2) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 203, 2) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 204, 2) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 205, 2) == 1);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(memcmp(input, output, sizeof(output)) == 0);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);

    edge_audio_jitter_reset(&jitter);
    fill(input, 0xaa);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 300, 3) == 1);
    assert(edge_audio_jitter_push(&jitter, input, sizeof(input), 305, 3) == 1);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(memcmp(input, output, sizeof(output)) != 0);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(!is_silence(output));
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_FRAME);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(edge_audio_jitter_get(&jitter, output, sizeof(output)) ==
           EDGE_AUDIO_JITTER_CONCEALED);
    assert(!is_silence(output));

    return 0;
}
