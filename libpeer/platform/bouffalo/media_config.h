#ifndef LIBPEER_BOUFFALO_MEDIA_CONFIG_H
#define LIBPEER_BOUFFALO_MEDIA_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t frame_duration_ms;
    uint8_t channels;
    uint8_t bits_per_sample;
    bool enabled;
} webrtc_audio_cfg_t;

typedef struct {
    uint32_t id;
    int32_t value;
} webrtc_video_control_t;

typedef struct {
    const char *device_name;
    const webrtc_video_control_t *controls;
    size_t control_count;
    uint32_t pixel_format;
    uint16_t width;
    uint16_t height;
    uint32_t max_frame_size;
    uint8_t frame_rate;
    uint8_t buffer_count;
    uint32_t task_priority;
    bool enabled;
} webrtc_video_cfg_t;

#endif
