#include <stdio.h>
#include <string.h>

#include "audio_rx.h"
#include "media.h"
#include "mg_channel_convert.h"
#include "mg_pcma.h"
#include "mg_node.h"

#define AUDIO_PLAYBACK_QUEUE_FRAMES      8
#define AUDIO_PLAYBACK_TASK_STACK_SIZE   4096
#define AUDIO_PLAYBACK_TASK_PRIORITY     8
#define AUDIO_PLAYBACK_STOP_EVENT        (1U << 0)
#define AUDIO_PLAYBACK_EXITED_EVENT      (1U << 1)

struct audio_playback_packet {
    uint8_t data[EDGE_AUDIO_JITTER_FRAME_BYTES];
    uint16_t sequence_number;
    uint32_t ssrc;
};

static void rtc_audio_playback_task(void *argument)
{
    struct webrtc_media *media = argument;
    struct edge_audio_jitter_buffer *jitter = &media->playback_jitter;
    struct audio_playback_packet packet;
    uint8_t frame[EDGE_AUDIO_JITTER_FRAME_BYTES];
    uint32_t playout_due_ms = 0;
    bool playout_started = false;
    bool playback_error_logged = false;

    edge_audio_jitter_reset(jitter);
    for (;;) {
        enum edge_audio_jitter_result result;
        uint32_t timeout_ms = media->playback_frame_duration_ms;
        uint32_t now_ms;

        if (xEventGroupGetBits(media->playback_event) &
            AUDIO_PLAYBACK_STOP_EVENT) {
            break;
        }

        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (playout_started) {
            int32_t remaining_ms =
                (int32_t)(playout_due_ms - now_ms);

            timeout_ms = remaining_ms > 0 ?
                         (uint32_t)remaining_ms : 0;
        }
        if (xQueueReceive(media->playback_queue, &packet,
                          pdMS_TO_TICKS(timeout_ms)) == pdPASS) {
            do {
                edge_audio_jitter_push(jitter, packet.data,
                                       sizeof(packet.data),
                                       packet.sequence_number,
                                       packet.ssrc);
            } while (xQueueReceive(media->playback_queue,
                                   &packet, 0) == pdPASS);
        }

        if (xEventGroupGetBits(media->playback_event) &
            AUDIO_PLAYBACK_STOP_EVENT) {
            break;
        }

        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (playout_started &&
            (int32_t)(now_ms - playout_due_ms) < 0) {
            continue;
        }

        result = edge_audio_jitter_get(jitter, frame, sizeof(frame));
        if (result == EDGE_AUDIO_JITTER_WAIT) {
            playout_started = false;
            continue;
        }
        if (result == EDGE_AUDIO_JITTER_FRAME ||
            result == EDGE_AUDIO_JITTER_CONCEALED) {
            int ret = mg_pipeline_input(media->playback_pipeline,
                                         frame, sizeof(frame));
            if (ret < 0 && !playback_error_logged) {
                printf("audio_playback: first pipeline failure ret=%d seq=%u ssrc=%lu\r\n",
                       ret, (unsigned int)(uint16_t)(jitter->expected_sequence - 1),
                       (unsigned long)jitter->ssrc);
                playback_error_logged = true;
            }
        }

        if (!playout_started) {
            playout_started = true;
            playout_due_ms = now_ms + media->playback_frame_duration_ms;
        } else {
            playout_due_ms += media->playback_frame_duration_ms;
            if ((int32_t)(now_ms - playout_due_ms) >= 0) {
                playout_due_ms =
                    now_ms + media->playback_frame_duration_ms;
            }
        }
    }

    xEventGroupSetBits(media->playback_event, AUDIO_PLAYBACK_EXITED_EVENT);
    vTaskDelete(NULL);
}

int webrtc_media_audio_playback_start(struct webrtc_media *media,
                                      const webrtc_audio_cfg_t *config)
{
    struct mg_channel_convert_config channel_config;
    struct mg_audio_sink_config sink_config;
    size_t packet_size;

    if (media->playback_event != NULL) {
        return 0;
    }
    packet_size = webrtc_media_audio_packet_size(config);
    if (packet_size == 0) {
        return -1;
    }
    mg_audio_sink_config_init(&sink_config);
    channel_config.frame_samples = (uint16_t)packet_size;
    channel_config.input_channels = 1;
    channel_config.output_channels = sink_config.format.channels;
    channel_config.bits_per_sample = config->bits_per_sample;
    sink_config.format.sample_rate = config->sample_rate;
    sink_config.format.frame_duration_ms = config->frame_duration_ms;
    sink_config.format.bits_per_sample = config->bits_per_sample;
    media->playback_frame_duration_ms = config->frame_duration_ms;

    media->playback_pipeline = mg_pipeline_create(MG_MEDIA_AUDIO, packet_size * sizeof(int16_t) * sink_config.format.channels);
    if (media->playback_pipeline == NULL) {
        return -1;
    }
    if (mg_pipeline_register(media->playback_pipeline,
                             mg_pcma_decoder_node_create()) < 0) {
        goto fail;
    }

    if (channel_config.input_channels != channel_config.output_channels &&
        mg_pipeline_register(media->playback_pipeline,
                             mg_channel_convert_node_create(&channel_config)) < 0) {
        goto fail;
    }

    if (mg_pipeline_register(media->playback_pipeline,
                             mg_audio_sink_node_create(&sink_config)) < 0) {
        goto fail;
    }
    media->playback_queue = xQueueCreate(AUDIO_PLAYBACK_QUEUE_FRAMES,
                                         sizeof(struct audio_playback_packet));
    if (media->playback_queue == NULL) {
        goto fail;
    }
    media->playback_event = xEventGroupCreate();
    if (media->playback_event == NULL) {
        goto fail;
    }
    if (xTaskCreate(rtc_audio_playback_task, "rtc_audio_play",
                    AUDIO_PLAYBACK_TASK_STACK_SIZE / sizeof(StackType_t),
                    media, configMAX_PRIORITIES - AUDIO_PLAYBACK_TASK_PRIORITY,
                    NULL) != pdPASS) {
        goto fail;
    }
    return 0;

fail:
    if (media->playback_event != NULL) {
        vEventGroupDelete(media->playback_event);
        media->playback_event = NULL;
    }
    if (media->playback_queue != NULL) {
        vQueueDelete(media->playback_queue);
        media->playback_queue = NULL;
    }
    mg_pipeline_destroy(media->playback_pipeline);
    media->playback_pipeline = NULL;
    return -1;
}

int webrtc_media_audio_playback_enqueue(struct webrtc_media *media,
                                        const uint8_t *data, size_t size,
                                        uint16_t sequence_number,
                                        uint32_t ssrc)
{
    struct audio_playback_packet packet;

    if (media->playback_queue == NULL || data == NULL ||
        size != sizeof(packet.data)) {
        return -1;
    }
    memcpy(packet.data, data, sizeof(packet.data));
    packet.sequence_number = sequence_number;
    packet.ssrc = ssrc;
    return xQueueSend(media->playback_queue, &packet, 0) == pdPASS ? 0 : -1;
}

void webrtc_media_audio_playback_stop(struct webrtc_media *media)
{
    if (media->playback_event != NULL) {
        xEventGroupSetBits(media->playback_event, AUDIO_PLAYBACK_STOP_EVENT);
        xEventGroupWaitBits(media->playback_event, AUDIO_PLAYBACK_EXITED_EVENT,
                            pdFALSE, pdFALSE, portMAX_DELAY);
    }
    if (media->playback_event != NULL) {
        vEventGroupDelete(media->playback_event);
        media->playback_event = NULL;
    }
    if (media->playback_queue != NULL) {
        vQueueDelete(media->playback_queue);
        media->playback_queue = NULL;
    }

    mg_pipeline_destroy(media->playback_pipeline);
    media->playback_pipeline = NULL;
}
