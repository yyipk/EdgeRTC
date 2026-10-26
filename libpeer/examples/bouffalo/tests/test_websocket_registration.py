#!/usr/bin/env python3
"""Exercise platform websocket registration with real pipeline ownership."""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(os.environ["BL_SDK_BASE"])
libpeer = pathlib.Path(__file__).resolve().parents[3]
platform = libpeer / "platform"
graph = root / "components/multimedia/media_graph"
source = (platform / "bouffalo/audio_tx.c").read_text()
start = source.index("int webrtc_media_audio_websocket_register(")
function = source[start:source.index("\n}\n", start) + 3]
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_XWEBSOCKET 1
#include "bouffalo/media.h"
#include "bouffalo/audio_tx.h"
#include "mg_node.h"
#define AUDIO_WEBSOCKET_FRAMES 8
struct test_node { struct mg_node node; int id; };
static int allocations, creates, fail_at, locked, locks, scenario, visited;
static struct mg_pipeline *pipeline;
static int process(struct mg_node *node, const void *input, size_t size,
                   void *output, size_t capacity) {
    static const int success[] = { 0, 1, 2, 3 }, unchanged[] = { 1, 3 };
    assert(locked && size == 1 && capacity >= size);
    assert(visited < (scenario == 0 ? 4 : 2));
    assert(((struct test_node *)node)->id ==
           (scenario == 0 ? success : unchanged)[visited++]);
    memcpy(output, input, size);
    return (int)size;
}
static void destroy(struct mg_node *node) {
    assert(!locked && allocations > 0);
    allocations--; free(node);
}
static const struct mg_node_ops ops = { .process = process, .destroy = destroy };
static struct mg_node *node_create(int id) {
    struct test_node *node = calloc(1, sizeof(*node));
    assert(node); allocations++;
    node->node.ops = &ops;
    node->node.media_type = MG_MEDIA_AUDIO;
    node->id = id;
    return &node->node;
}
size_t webrtc_media_audio_packet_size(const webrtc_audio_cfg_t *config) {
    assert(config->sample_rate == 8000 && config->frame_duration_ms == 20);
    return 160;
}
struct mg_node *mg_audio_websocket_node_create(const struct mg_audio_websocket_config *config) {
    assert(!locked && creates < 2);
    creates++;
    assert(strcmp(config->url, creates == 1 ? "before" : "after") == 0);
    assert(config->frame_size == (creates == 1 ? 640U : 320U));
    assert(config->ringbuffer_size == config->frame_size * AUDIO_WEBSOCKET_FRAMES);
    return creates == fail_at ? NULL : node_create(creates == 1 ? 0 : 2);
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
    assert(mutex == &locked && timeout == portMAX_DELAY && !locked);
    locked = 1; locks++; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    uint8_t input = 0;
    assert(mutex == &locked && locked);
    visited = 0;
    assert(mg_pipeline_input(pipeline, &input, sizeof(input)) == 0);
    assert(visited == (scenario == 0 ? 4 : 2));
    locked = 0;
    return pdTRUE;
}
'''
harness += function
harness += r'''
int main(void) {
    const webrtc_audio_cfg_t config = { .sample_rate = 8000, .frame_duration_ms = 20 };
    struct webrtc_media empty = {0};
    assert(webrtc_media_audio_websocket_register(NULL, &config, "before", "after") < 0);
    assert(webrtc_media_audio_websocket_register(&empty, NULL, "before", "after") < 0);
    assert(!creates && !allocations && !locks);
    for (scenario = 0; scenario < 6; scenario++) {
        pipeline = mg_pipeline_create(MG_MEDIA_AUDIO, 1);
        assert(pipeline);
        struct webrtc_media media = {
            .capture_pipeline = pipeline, .capture_mutex = &locked,
            .capture_aec_node = node_create(1), .capture_encoder_node = node_create(3),
        };
        assert(mg_pipeline_register(pipeline, media.capture_aec_node) == 0);
        assert(mg_pipeline_register(pipeline, media.capture_encoder_node) == 0);
        if (scenario == 3) media.capture_pipeline = NULL;
        if (scenario == 4) media.capture_aec_node = NULL;
        if (scenario == 5) media.capture_encoder_node = NULL;
        creates = locks = 0;
        fail_at = scenario < 3 ? scenario : 0;
        int result = webrtc_media_audio_websocket_register(&media, &config, "before", "after");
        assert(!locked && creates == (scenario == 1 ? 1 : 2));
        assert(locks == (scenario == 0 || scenario >= 3));
        assert(result == (scenario == 0 ? 0 : -1));
        assert(allocations == (scenario == 0 ? 4 : 2));
        mg_pipeline_destroy(pipeline);
        assert(allocations == 0);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webrtc-registration-") as temp:
    binary = pathlib.Path(temp) / "test"
    (pathlib.Path(temp) / "FreeRTOS.h").write_text('''
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
''')
    for header in ("event_groups.h", "queue.h", "semphr.h", "task.h"):
        (pathlib.Path(temp) / header).write_text("")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", temp,
                    "-I", str(platform), "-I", str(graph / "pipeline/include"),
                    "-I", str(graph / "io/include"), "-I", str(graph / "platform/include"),
                    "-I", str(libpeer / "src/jitter_buffer/include"),
                    "-x", "c", "-", str(graph / "pipeline/mg_pipeline.c"), "-o", str(binary)],
                   input=harness, text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("websocket registration allocation/stopped capture/locked insertion and cleanup: PASS")
