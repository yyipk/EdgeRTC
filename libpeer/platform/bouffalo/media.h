#ifndef LIBPEER_BOUFFALO_MEDIA_H
#define LIBPEER_BOUFFALO_MEDIA_H

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "media_config.h"
#include "edge_audio_jitter_buffer.h"
#include "mg_audio_io.h"
#include "mg_pipeline.h"
#include "mg_video_io.h"

struct webrtc_media {
    struct mg_pipeline *capture_pipeline;
    struct mg_pipeline *playback_pipeline;
    struct edge_audio_jitter_buffer playback_jitter;
#ifdef CONFIG_XWEBSOCKET
    struct mg_node *capture_aec_node;
    struct mg_node *capture_encoder_node;
#endif
    struct mg_audio_source *audio_source;
    struct mg_video_source *video_source;
    struct mg_pipeline *video_webrtc_pipeline;
    struct mg_node *video_webrtc_sink;
    SemaphoreHandle_t capture_mutex;
    QueueHandle_t playback_queue;
    EventGroupHandle_t playback_event;
    QueueHandle_t video_webrtc_queue;
    QueueHandle_t video_output_queue;
    EventGroupHandle_t video_event;
    uint32_t video_workers;
    uint16_t playback_frame_duration_ms;
};

size_t webrtc_media_audio_packet_size(const webrtc_audio_cfg_t *config);
int webrtc_media_init(struct webrtc_media *media);
void webrtc_media_deinit(struct webrtc_media *media);

#endif
