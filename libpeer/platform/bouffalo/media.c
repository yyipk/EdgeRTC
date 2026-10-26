#include <string.h>

#include "media.h"
#include "audio_tx.h"
#include "audio_rx.h"
#include "video_tx.h"
#include "net_buf_port.h"

enum {
    PCMA_SAMPLE_RATE = 8000,
    PCMA_FRAME_DURATION_MS = 20,
    AEC_INPUT_CHANNELS = 2,
    AUDIO_BITS_PER_SAMPLE = 16,
};

size_t webrtc_media_audio_packet_size(const webrtc_audio_cfg_t *config)
{
    size_t samples;

    if (config == NULL || config->sample_rate != PCMA_SAMPLE_RATE ||
        config->frame_duration_ms != PCMA_FRAME_DURATION_MS ||
        config->channels != AEC_INPUT_CHANNELS ||
        config->bits_per_sample != AUDIO_BITS_PER_SAMPLE) {
        return 0;
    }

    samples = (size_t)config->sample_rate * config->frame_duration_ms / 1000;
    if (samples != EDGE_AUDIO_JITTER_FRAME_BYTES) {
        return 0;
    }
    return samples;
}

int webrtc_media_init(struct webrtc_media *media)
{
    memset(media, 0, sizeof(*media));

    libpeer_net_buf_init();

    media->capture_mutex = xSemaphoreCreateMutex();
    if (media->capture_mutex == NULL) {
        return -1;
    }
    media->video_source = mg_v4l2_video_source_create();
    if (media->video_source == NULL) {
        vSemaphoreDelete(media->capture_mutex);
        media->capture_mutex = NULL;
        return -1;
    }
    return 0;
}

void webrtc_media_deinit(struct webrtc_media *media)
{
    webrtc_media_audio_capture_stop(media);
    webrtc_media_audio_playback_stop(media);
    webrtc_media_video_capture_stop(media);
    mg_video_source_destroy(media->video_source);
    vSemaphoreDelete(media->capture_mutex);
    memset(media, 0, sizeof(*media));
}
