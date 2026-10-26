#include "audio_tx.h"
#include "media.h"
#include "mg_aec.h"
#include "mg_channel_convert.h"
#ifdef CONFIG_MG_NS
#include "mg_ns.h"
#include "mg_resample.h"
#endif
#include "mg_pcma.h"
#include "mg_streambuffer.h"
#include "mg_node.h"

#define AUDIO_STREAMBUFFER_FRAMES        8
#define AUDIO_AEC_FILTER_LENGTH_MS       80
#ifdef CONFIG_XWEBSOCKET
#define AUDIO_WEBSOCKET_FRAMES        8
#endif

static int audio_pipeline_input(void *context,
                                const void *data, size_t size)
{
    struct webrtc_media *media = context;
    int ret;

    if (media->capture_mutex == NULL ||
        xSemaphoreTake(media->capture_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    if (media->capture_pipeline == NULL) {
        xSemaphoreGive(media->capture_mutex);
        return -1;
    }
    ret = mg_pipeline_input(media->capture_pipeline, data, size);
    xSemaphoreGive(media->capture_mutex);
    return ret;
}

int webrtc_media_audio_capture_start(struct webrtc_media *media,
                                     const webrtc_audio_cfg_t *config)
{
    struct mg_aec_config aec_config;
    struct mg_channel_convert_config channel_config;
#ifdef CONFIG_MG_NS
    struct mg_ns_config ns_config;
    struct mg_resample_config resample_config;
#endif
    struct mg_xrecord_source_config source_config;
    struct mg_node *node;
    size_t packet_size;
#ifdef CONFIG_MG_NS
    size_t ns_frame_samples;
#endif
    size_t workspace_size;

    if (media->audio_source != NULL) {
        return 0;
    }
    packet_size = webrtc_media_audio_packet_size(config);
    if (packet_size == 0) {
        return -1;
    }
    mg_xrecord_source_config_init(&source_config);
    source_config.format.sample_rate = config->sample_rate;
    source_config.format.frame_duration_ms = config->frame_duration_ms;
    source_config.format.bits_per_sample = config->bits_per_sample;
    source_config.frame_callback = audio_pipeline_input;
    source_config.callback_context = media;

    workspace_size = packet_size * source_config.format.channels * sizeof(int16_t);
#ifdef CONFIG_MG_NS
    ns_frame_samples = packet_size * MG_NS_SAMPLE_RATE / config->sample_rate;
    if (workspace_size < ns_frame_samples * sizeof(int16_t)) {
        workspace_size = ns_frame_samples * sizeof(int16_t);
    }
#endif
    aec_config.sample_rate = config->sample_rate;
    aec_config.frame_samples = (uint16_t)packet_size;
    aec_config.tail_length_ms = AUDIO_AEC_FILTER_LENGTH_MS;
    aec_config.reference_delay_ms = 0;
    aec_config.input_channels = config->channels;
    aec_config.reference_channel = 0;
    aec_config.microphone_channel = 1;

    media->capture_pipeline = mg_pipeline_create(MG_MEDIA_AUDIO,
                                                 workspace_size);
    if (media->capture_pipeline == NULL) {
        return -1;
    }

    if (source_config.format.channels != aec_config.input_channels) {
        channel_config.frame_samples = (uint16_t)packet_size;
        channel_config.input_channels = source_config.format.channels;
        channel_config.output_channels = aec_config.input_channels;
        channel_config.bits_per_sample = config->bits_per_sample;
        if (mg_pipeline_register(media->capture_pipeline,
                                 mg_channel_convert_node_create(&channel_config)) < 0) {
            goto fail;
        }
    }

    node = mg_aec_node_create(&aec_config);

    if (mg_pipeline_register(media->capture_pipeline, node) < 0) {
        goto fail;
    }
#ifdef CONFIG_XWEBSOCKET
    media->capture_aec_node = node;
#endif

#ifdef CONFIG_MG_NS
    if (MG_NS_SAMPLE_RATE != config->sample_rate) {

        resample_config.input_sample_rate = config->sample_rate;
        resample_config.output_sample_rate = MG_NS_SAMPLE_RATE;
        resample_config.frame_samples = (uint16_t)packet_size;

        if (mg_pipeline_register(media->capture_pipeline, mg_resample_node_create(&resample_config)) < 0) {
            goto fail;
        }
    }

    ns_config.sample_rate = MG_NS_SAMPLE_RATE;
    ns_config.frame_samples = (uint16_t)ns_frame_samples;

    if (mg_pipeline_register(media->capture_pipeline, mg_ns_node_create(&ns_config)) < 0) {
        goto fail;
    }

    if (MG_NS_SAMPLE_RATE != config->sample_rate) {

        resample_config.input_sample_rate = MG_NS_SAMPLE_RATE;
        resample_config.output_sample_rate = config->sample_rate;
        resample_config.frame_samples = (uint16_t)ns_frame_samples;

        if (mg_pipeline_register(media->capture_pipeline, mg_resample_node_create(&resample_config)) < 0) {
            goto fail;
        }
    }
#endif

    node = mg_pcma_encoder_node_create();
    if (mg_pipeline_register(media->capture_pipeline, node) < 0) {
        goto fail;
    }
#ifdef CONFIG_XWEBSOCKET
    media->capture_encoder_node = node;
#endif

    node = mg_streambuffer_node_create(MG_MEDIA_AUDIO, packet_size * AUDIO_STREAMBUFFER_FRAMES, packet_size);

    if (mg_pipeline_register(media->capture_pipeline, node) < 0) {
        goto fail;
    }

    media->audio_source = mg_xrecord_source_create(&source_config);
    if (media->audio_source == NULL ||
        mg_audio_source_start(media->audio_source) < 0) {
        goto fail;
    }
    return 0;

fail:
    mg_audio_source_destroy(media->audio_source);
    media->audio_source = NULL;
    mg_pipeline_destroy(media->capture_pipeline);
    media->capture_pipeline = NULL;
#ifdef CONFIG_XWEBSOCKET
    media->capture_aec_node = NULL;
    media->capture_encoder_node = NULL;
#endif
    return -1;
}

int webrtc_media_audio_capture_read(struct webrtc_media *media,
                                    uint8_t *data, size_t size,
                                    uint32_t timeout_ms)
{
    return mg_pipeline_output(media->capture_pipeline, data, size, timeout_ms);
}

void webrtc_media_audio_capture_stop(struct webrtc_media *media)
{
    struct mg_pipeline *pipeline;

    mg_audio_source_destroy(media->audio_source);
    media->audio_source = NULL;
    if (media->capture_mutex == NULL ||
        xSemaphoreTake(media->capture_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    pipeline = media->capture_pipeline;
    media->capture_pipeline = NULL;
#ifdef CONFIG_XWEBSOCKET
    media->capture_aec_node = NULL;
    media->capture_encoder_node = NULL;
#endif
    xSemaphoreGive(media->capture_mutex);
    mg_pipeline_destroy(pipeline);
}

#ifdef CONFIG_XWEBSOCKET
int webrtc_media_audio_websocket_register(struct webrtc_media *media,
                                          const webrtc_audio_cfg_t *audio_config,
                                          const char *before_url,
                                          const char *after_url)
{
    struct mg_audio_websocket_config config;
    struct mg_node *before_node;
    struct mg_node *after_node;
    size_t frame_size;

    if (media == NULL || audio_config == NULL) {
        return -1;
    }
    frame_size = webrtc_media_audio_packet_size(audio_config) *
                 sizeof(int16_t);

    config.url = before_url;
    config.frame_size = frame_size * 2U;
    config.ringbuffer_size = config.frame_size * AUDIO_WEBSOCKET_FRAMES;
    before_node = mg_audio_websocket_node_create(&config);
    if (before_node == NULL) {
        return -1;
    }

    config.url = after_url;
    config.frame_size = frame_size;
    config.ringbuffer_size = config.frame_size * AUDIO_WEBSOCKET_FRAMES;
    after_node = mg_audio_websocket_node_create(&config);
    if (after_node == NULL) {
        mg_node_destroy(before_node);
        return -1;
    }

    xSemaphoreTake(media->capture_mutex, portMAX_DELAY);
    if (media->capture_pipeline == NULL || media->capture_aec_node == NULL ||
        media->capture_encoder_node == NULL) {
        xSemaphoreGive(media->capture_mutex);
        mg_node_destroy(before_node);
        mg_node_destroy(after_node);
        return -1;
    }
    mg_pipeline_register_before(media->capture_pipeline,
                                media->capture_aec_node, before_node);
    mg_pipeline_register_before(media->capture_pipeline,
                                media->capture_encoder_node, after_node);
    xSemaphoreGive(media->capture_mutex);
    return 0;
}
#endif
