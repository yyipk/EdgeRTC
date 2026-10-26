#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "video_tx.h"
#include "media.h"
#include "mg_node.h"
#include "zephyr/net_buf.h"
#include "net_buf_port.h"
#include "v4l2.h"

#define VIDEO_STOP_EVENT                 (1U << 0)
#define VIDEO_CAPTURE_EXITED_EVENT       (1U << 1)
#define VIDEO_WEBRTC_EXITED_EVENT        (1U << 2)
#define VIDEO_FRAME_RELEASED_EVENT       (1U << 4)
#define VIDEO_QUEUE_DEPTH                1U
#define VIDEO_TASK_STACK_SIZE            2048U
#define VIDEO_CAPTURE_TASK_PRIORITY      9U
#define VIDEO_PIPELINE_TASK_PRIORITY     8U
#define VIDEO_WIDTH                      640U
#define VIDEO_HEIGHT                     480U

struct video_buffer_owner {
    struct mg_video_source *source;
    struct mg_video_frame frame;
    EventGroupHandle_t release_event;
};

struct video_webrtc_sink {
    struct mg_node node;
    struct webrtc_media *media;
    struct net_buf *buffer;
};

static int video_webrtc_process(struct mg_node *node,
                                const void *input, size_t input_size,
                                void *output, size_t output_capacity)
{
    struct video_webrtc_sink *sink = (struct video_webrtc_sink *)node;
    struct net_buf *buffer = sink->buffer;

    (void)output;
    (void)output_capacity;
    if (buffer == NULL || input != buffer->data ||
        input_size != buffer->len) {
        return -1;
    }
    buffer = net_buf_ref(buffer);
    if (xQueueSend(sink->media->video_output_queue, &buffer, 0) != pdPASS) {
        net_buf_unref(buffer);
    }
    return 0;
}

static void video_webrtc_destroy(struct mg_node *node)
{
    free(node);
}

static const struct mg_node_ops video_webrtc_ops = {
    .process = video_webrtc_process,
    .destroy = video_webrtc_destroy,
};

static void video_buffer_destroy(struct net_buf *buffer)
{
    struct video_buffer_owner *owner = net_buf_user_data(buffer);
    EventGroupHandle_t release_event = owner->release_event;

    mg_video_source_release(owner->source, &owner->frame);
    net_buf_destroy(buffer);
    if (xPortIsInsideInterrupt()) {
        BaseType_t higher_priority_task_woken = pdFALSE;

        if (xEventGroupSetBitsFromISR(release_event, VIDEO_FRAME_RELEASED_EVENT,
                                      &higher_priority_task_woken) == pdPASS) {
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    } else {
        xEventGroupSetBits(release_event, VIDEO_FRAME_RELEASED_EVENT);
    }
}

NET_BUF_POOL_FIXED_DEFINE(video_buffer_pool, 2, 1,
                          sizeof(struct video_buffer_owner),
                          video_buffer_destroy);

static void video_capture_task(void *argument)
{
    struct webrtc_media *media = argument;
    struct mg_video_frame frame;
    struct video_buffer_owner *owner;
    struct net_buf *buffer;
    size_t capacity;
    int ret;

    while ((xEventGroupGetBits(media->video_event) & VIDEO_STOP_EVENT) == 0) {
        ret = mg_video_source_read(media->video_source, &frame, 20);
        if (ret < 0) {
            xEventGroupWaitBits(media->video_event, VIDEO_FRAME_RELEASED_EVENT,
                                pdTRUE, pdFALSE, pdMS_TO_TICKS(20));
            continue;
        }
        if (ret == 0) {
            continue;
        }
        capacity = frame.headroom + frame.size + frame.tailroom;
        if (capacity > UINT16_MAX) {
            mg_video_source_release(media->video_source, &frame);
            continue;
        }
        buffer = net_buf_alloc_with_data(&video_buffer_pool,
                                         frame.data - frame.headroom,
                                         capacity, K_NO_WAIT);
        if (buffer == NULL) {
            mg_video_source_release(media->video_source, &frame);
            continue;
        }
        buffer->data = frame.data;
        buffer->len = frame.size;
        owner = net_buf_user_data(buffer);
        owner->source = media->video_source;
        owner->frame = frame;
        owner->release_event = media->video_event;

        buffer = net_buf_ref(buffer);
        if (xQueueSend(media->video_webrtc_queue, &buffer, 0) != pdPASS) {
            net_buf_unref(buffer);
        }
        net_buf_unref(buffer);
    }
    xEventGroupSetBits(media->video_event, VIDEO_CAPTURE_EXITED_EVENT);
    vTaskDelete(NULL);
}

static void video_webrtc_task(void *argument)
{
    struct webrtc_media *media = argument;
    struct video_webrtc_sink *sink =
        (struct video_webrtc_sink *)media->video_webrtc_sink;
    struct net_buf *buffer;

    while ((xEventGroupGetBits(media->video_event) & VIDEO_STOP_EVENT) == 0) {
        if (xQueueReceive(media->video_webrtc_queue, &buffer,
                          pdMS_TO_TICKS(20)) != pdPASS) {
            continue;
        }
        sink->buffer = buffer;
        mg_pipeline_input(media->video_webrtc_pipeline,
                          buffer->data, buffer->len);
        sink->buffer = NULL;
        net_buf_unref(buffer);
    }
    xEventGroupSetBits(media->video_event, VIDEO_WEBRTC_EXITED_EVENT);
    vTaskDelete(NULL);
}

static void video_queue_drain(QueueHandle_t queue)
{
    struct net_buf *buffer;

    if (queue == NULL) {
        return;
    }
    while (xQueueReceive(queue, &buffer, 0) == pdPASS) {
        net_buf_unref(buffer);
    }
}

int webrtc_media_video_capture_start(struct webrtc_media *media,
                                     const webrtc_video_cfg_t *config)
{
    struct mg_video_source_config source_config;
    struct mg_video_control *controls = NULL;
    struct video_webrtc_sink *sink;
    size_t index;
    int ret;

    if (media->video_event != NULL) {
        printf("[webrtc_media] video start skipped: already running\r\n");
        return 0;
    }
    if (config->pixel_format != V4L2_PIX_FMT_MJPEG ||
        config->width != VIDEO_WIDTH || config->height != VIDEO_HEIGHT) {
        printf("[webrtc_media] video config rejected: fmt=0x%lx size=%lux%lu\r\n",
             (unsigned long)config->pixel_format,
             (unsigned long)config->width, (unsigned long)config->height);
        return -1;
    }

    if (config->control_count != 0) {
        controls = calloc(config->control_count, sizeof(*controls));
        if (controls == NULL) {
            printf("[webrtc_media] video start failed: controls alloc\r\n");
            return -1;
        }
        for (index = 0; index < config->control_count; index++) {
            controls[index].id = config->controls[index].id;
            controls[index].value = config->controls[index].value;
        }
    }

    memset(&source_config, 0, sizeof(source_config));
    source_config.device_name = config->device_name;
    source_config.controls = controls;
    source_config.control_count = config->control_count;
    source_config.pixel_format = config->pixel_format;
    source_config.width = config->width;
    source_config.height = config->height;
    source_config.max_frame_size = config->max_frame_size;
    source_config.frame_rate = config->frame_rate;
    source_config.buffer_count = config->buffer_count;
    source_config.task_priority = config->task_priority;
    ret = mg_video_source_start(media->video_source, &source_config);
    free(controls);
    if (ret < 0) {
        printf("[webrtc_media] video source start failed: ret=%d\r\n", ret);
        return -1;
    }
    media->video_webrtc_pipeline = mg_pipeline_create(MG_MEDIA_VIDEO, 1);
    sink = calloc(1, sizeof(*sink));
    if (media->video_webrtc_pipeline == NULL ||
        sink == NULL) {
        printf("[webrtc_media] video start failed: pipeline/node alloc\r\n");
        free(sink);
        goto fail;
    }
    sink->node.ops = &video_webrtc_ops;
    sink->node.media_type = MG_MEDIA_VIDEO;
    sink->media = media;
    if (mg_pipeline_register(media->video_webrtc_pipeline,
                             &sink->node) < 0) {
        printf("[webrtc_media] video start failed: WebRTC sink register\r\n");
        free(sink);
        goto fail;
    }
    media->video_webrtc_sink = &sink->node;
    media->video_webrtc_queue = xQueueCreate(VIDEO_QUEUE_DEPTH,
                                             sizeof(struct net_buf *));
    if (media->video_webrtc_queue == NULL) {
        printf("[webrtc_media] video start failed: queue/event create\r\n");
        goto fail;
    }
    media->video_output_queue = xQueueCreate(VIDEO_QUEUE_DEPTH,
                                             sizeof(struct net_buf *));
    if (media->video_output_queue == NULL) {
        printf("[webrtc_media] video start failed: queue/event create\r\n");
        goto fail;
    }
    media->video_event = xEventGroupCreate();
    if (media->video_event == NULL) {
        printf("[webrtc_media] video start failed: queue/event create\r\n");
        goto fail;
    }
    if (xTaskCreate(video_webrtc_task, "video_webrtc",
                    VIDEO_TASK_STACK_SIZE / sizeof(StackType_t), media,
                    configMAX_PRIORITIES - VIDEO_PIPELINE_TASK_PRIORITY,
                    NULL) != pdPASS) {
        printf("[webrtc_media] video start failed: video_webrtc task\r\n");
        goto fail;
    }
    media->video_workers |= VIDEO_WEBRTC_EXITED_EVENT;
    if (xTaskCreate(video_capture_task, "video_capture",
                    VIDEO_TASK_STACK_SIZE / sizeof(StackType_t), media,
                    configMAX_PRIORITIES - VIDEO_CAPTURE_TASK_PRIORITY,
                    NULL) != pdPASS) {
        printf("[webrtc_media] video start failed: video_capture task\r\n");
        goto fail;
    }
    media->video_workers |= VIDEO_CAPTURE_EXITED_EVENT;
    return 0;

fail:
    webrtc_media_video_capture_stop(media);
    return -1;
}

int webrtc_media_video_capture_read(struct webrtc_media *media,
                                    struct webrtc_video_frame *frame,
                                    uint32_t timeout_ms)
{
    struct net_buf *buffer;

    if (media->video_output_queue == NULL ||
        xQueueReceive(media->video_output_queue, &buffer,
                      timeout_ms == UINT32_MAX ? portMAX_DELAY :
                      pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return 0;
    }
    frame->data = buffer->data;
    frame->size = buffer->len;
    frame->headroom = net_buf_headroom(buffer);
    frame->tailroom = net_buf_tailroom(buffer);
    frame->buffer = buffer;
    return 1;
}

void webrtc_media_video_capture_release(struct webrtc_media *media,
                                        const struct webrtc_video_frame *frame)
{
    (void)media;
    net_buf_unref(frame->buffer);
}

void webrtc_media_video_capture_stop(struct webrtc_media *media)
{
    if (media->video_event != NULL && media->video_workers != 0) {
        xEventGroupSetBits(media->video_event, VIDEO_STOP_EVENT);
        xEventGroupWaitBits(media->video_event, media->video_workers,
                            pdFALSE, pdTRUE, portMAX_DELAY);
    }
    video_queue_drain(media->video_webrtc_queue);
    video_queue_drain(media->video_output_queue);
    if (media->video_event != NULL) {
        vEventGroupDelete(media->video_event);
        media->video_event = NULL;
    }
    if (media->video_webrtc_queue != NULL) {
        vQueueDelete(media->video_webrtc_queue);
        media->video_webrtc_queue = NULL;
    }
    if (media->video_output_queue != NULL) {
        vQueueDelete(media->video_output_queue);
        media->video_output_queue = NULL;
    }
    mg_pipeline_destroy(media->video_webrtc_pipeline);
    media->video_webrtc_pipeline = NULL;
    media->video_webrtc_sink = NULL;
    media->video_workers = 0;
    mg_video_source_stop(media->video_source);
}
