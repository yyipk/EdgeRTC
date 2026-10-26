#!/usr/bin/env python3
"""Run the real audio capture setup with channel and allocation-failure stubs."""
import pathlib
import subprocess
import tempfile

libpeer = pathlib.Path(__file__).resolve().parents[3]
source = (libpeer / "platform/bouffalo/audio_tx.c").read_text()
start = source.index("int webrtc_media_audio_capture_start(")
function = source[start:source.index("\n}\n", start) + 3]
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define CONFIG_XWEBSOCKET 1
#define MG_MEDIA_AUDIO 1
#define AUDIO_STREAMBUFFER_FRAMES 8
#define AUDIO_AEC_FILTER_LENGTH_MS 80
typedef struct { uint32_t sample_rate; uint16_t frame_duration_ms;
                 uint8_t channels, bits_per_sample; } webrtc_audio_cfg_t;
struct mg_audio_format { uint32_t sample_rate; uint16_t frame_duration_ms;
                         uint8_t channels, bits_per_sample; };
struct mg_xrecord_source_config {
    struct mg_audio_format format;
    uint16_t period_count;
    int (*frame_callback)(void *, const void *, size_t);
    void *callback_context;
};
struct mg_channel_convert_config { uint16_t frame_samples;
    uint8_t input_channels, output_channels, bits_per_sample; };
struct mg_aec_config { uint32_t sample_rate; uint16_t frame_samples,
    tail_length_ms, reference_delay_ms;
    uint8_t input_channels, reference_channel, microphone_channel; };
struct mg_node { int kind; };
struct mg_pipeline { struct mg_node *nodes[4]; int count; };
struct mg_audio_source { int unused; };
struct webrtc_media { struct mg_pipeline *capture_pipeline;
    struct mg_audio_source *audio_source;
    struct mg_node *capture_aec_node, *capture_encoder_node; };
static int source_channels, allocation_failure, allocations, conversions;
static int recordings, pipeline_destroys, source_creates;
static struct webrtc_media *expected_media;
static size_t webrtc_media_audio_packet_size(const webrtc_audio_cfg_t *c) {
    assert(c->sample_rate == 8000 && c->frame_duration_ms == 20);
    assert(c->channels == 2 && c->bits_per_sample == 16);
    return 160;
}
static int audio_pipeline_input(void *context, const void *data, size_t size) {
    (void)context; (void)data; (void)size; return 0;
}
static void mg_xrecord_source_config_init(struct mg_xrecord_source_config *c) {
    *c = (struct mg_xrecord_source_config){
        .format.channels = source_channels, .period_count = 7 };
}
static struct mg_pipeline *mg_pipeline_create(int type, size_t workspace) {
    assert(type == MG_MEDIA_AUDIO && workspace == 160 * source_channels * sizeof(int16_t));
    struct mg_pipeline *p = calloc(1, sizeof(*p));
    assert(p); allocations++; return p;
}
static struct mg_node *node_create(int kind) {
    struct mg_node *n = malloc(sizeof(*n));
    assert(n); n->kind = kind; allocations++; return n;
}
static struct mg_node *mg_channel_convert_node_create(const struct mg_channel_convert_config *c) {
    /* Equal-channel input catches the old bug without reading uninitialized fields. */
    assert(source_channels != 2);
    conversions++;
    assert(c->frame_samples == 160 && c->input_channels == source_channels);
    assert(c->output_channels == 2 && c->bits_per_sample == 16);
    if (c->input_channels != 4 || allocation_failure) return NULL;
    return node_create(0);
}
static int mg_pipeline_register(struct mg_pipeline *p, struct mg_node *n) {
    assert(p);
    if (!n) return -1;
    assert(p->count < 4);
    assert(n->kind == p->count + (source_channels == 2));
    p->nodes[p->count++] = n;
    return 0;
}
static struct mg_node *mg_aec_node_create(const struct mg_aec_config *c) {
    assert(c->sample_rate == 8000 && c->frame_samples == 160);
    assert(c->tail_length_ms == 80 && c->reference_delay_ms == 0);
    assert(c->input_channels == 2 && c->reference_channel == 0 && c->microphone_channel == 1);
    return node_create(1);
}
static struct mg_node *mg_pcma_encoder_node_create(void) { return node_create(2); }
static struct mg_node *mg_streambuffer_node_create(int type, size_t capacity, size_t frame) {
    assert(type == MG_MEDIA_AUDIO && capacity == 1280 && frame == 160);
    return node_create(3);
}
static struct mg_audio_source *mg_xrecord_source_create(const struct mg_xrecord_source_config *c) {
    assert(c->format.sample_rate == 8000 && c->format.frame_duration_ms == 20);
    assert(c->format.channels == source_channels && c->format.bits_per_sample == 16);
    assert(c->period_count == 7 && c->frame_callback == audio_pipeline_input);
    assert(c->callback_context == expected_media);
    assert(expected_media->capture_pipeline->count == (source_channels == 2 ? 3 : 4));
    struct mg_audio_source *s = calloc(1, sizeof(*s));
    assert(s); allocations++; source_creates++; return s;
}
static int mg_audio_source_start(struct mg_audio_source *s) {
    assert(s); recordings++; return 0;
}
static void mg_audio_source_destroy(struct mg_audio_source *s) {
    if (s) { free(s); allocations--; }
}
static void mg_pipeline_destroy(struct mg_pipeline *p) {
    assert(p);
    for (int i = 0; i < p->count; i++) { free(p->nodes[i]); allocations--; }
    free(p); allocations--; pipeline_destroys++;
}
'''
harness += function
harness += r'''
int main(void) {
    const webrtc_audio_cfg_t config = { 8000, 20, 2, 16 };
    for (int scenario = 0; scenario < 4; scenario++) {
        struct webrtc_media media = {0};
        expected_media = &media;
        source_channels = scenario == 0 ? 2 : scenario == 2 ? 3 : 4;
        allocation_failure = scenario == 3;
        conversions = recordings = source_creates = pipeline_destroys = 0;
        int result = webrtc_media_audio_capture_start(&media, &config);
        assert(conversions == (source_channels != 2));
        if (scenario < 2) {
            assert(result == 0 && recordings == 1 && source_creates == 1);
            assert(media.audio_source && media.capture_pipeline && pipeline_destroys == 0);
            int first = source_channels != 2;
            assert(media.capture_aec_node == media.capture_pipeline->nodes[first]);
            assert(media.capture_encoder_node == media.capture_pipeline->nodes[first + 1]);
            mg_audio_source_destroy(media.audio_source);
            mg_pipeline_destroy(media.capture_pipeline);
        } else {
            assert(result < 0 && !recordings && !source_creates && pipeline_destroys == 1);
            assert(!media.audio_source && !media.capture_pipeline);
            assert(!media.capture_aec_node && !media.capture_encoder_node);
        }
        assert(allocations == 0);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webrtc-audio-capture-") as temp:
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-", "-o", str(binary)],
                   input=harness, text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("audio capture channel conversion and failure cleanup: PASS")
