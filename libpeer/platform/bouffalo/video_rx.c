#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_JPEG_MAX 61440U

struct video_fragment {
    uint32_t received;
    uint32_t next_tsn;
    uint16_t sid;
    bool active;
};

struct video_rx_stats {
    uint32_t fragments;
    uint32_t fragment_reject;
    uint32_t frames;
    uint32_t incomplete_frames;
    uint32_t busy_frames;
    uint32_t invalid_frames;
    uint32_t complete_frames;
    uint32_t queue_drop;
    uint32_t surface_drop;
    uint32_t pipeline_fail;
    uint32_t submitted;
    uint32_t display_frames;
    uint32_t display_seof;
};

static struct video_rx_stats video_stats_interval(const struct video_rx_stats *current,
                                                  struct video_rx_stats *previous)
{
    struct video_rx_stats interval = {
        .fragments = current->fragments - previous->fragments,
        .fragment_reject = current->fragment_reject - previous->fragment_reject,
        .frames = current->frames - previous->frames,
        .incomplete_frames = current->incomplete_frames - previous->incomplete_frames,
        .busy_frames = current->busy_frames - previous->busy_frames,
        .invalid_frames = current->invalid_frames - previous->invalid_frames,
        .complete_frames = current->complete_frames - previous->complete_frames,
        .queue_drop = current->queue_drop - previous->queue_drop,
        .surface_drop = current->surface_drop - previous->surface_drop,
        .pipeline_fail = current->pipeline_fail - previous->pipeline_fail,
        .submitted = current->submitted - previous->submitted,
        .display_frames = current->display_frames - previous->display_frames,
        .display_seof = current->display_seof - previous->display_seof,
    };
    *previous = *current;
    return interval;
}

/* SCTP delivers these borrowed spans only after the whole B..E message is present. */
static int video_fragment_parse(struct video_fragment *fragment, const void *data,
                                size_t size, uint16_t sid, uint32_t tsn, uint8_t flags)
{
    if (flags & 0x02) {
        fragment->received = 0;
        fragment->sid = sid;
        fragment->next_tsn = tsn;
        fragment->active = true;
    }
    if (!fragment->active || fragment->sid != sid || fragment->next_tsn != tsn ||
        !data || !size || size > VIDEO_JPEG_MAX - fragment->received) {
        fragment->active = false;
        return -1;
    }
    fragment->received += size;
    fragment->next_tsn = tsn + 1;
    if (flags & 0x01) {
        fragment->active = false;
    }
    return 0;
}

static bool video_jpeg_valid(const uint8_t *data, size_t size)
{
    return size >= 4 && data[0] == 0xff && data[1] == 0xd8 &&
           data[size - 2] == 0xff && data[size - 1] == 0xd9;
}

#ifndef WEBRTC_VIDEO_RX_PROTOCOL_TEST
#include <stdio.h>
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "bflb_dpi.h"
#include "bflb_irq.h"
#include "bflb_l1c.h"
#include "bflb_osd.h"
#include "compiler/compiler_ld.h"
#include "mg_mjpeg_hw_decoder.h"
#include "mg_node.h"
#include "net_buf_port.h"
#include "video_rx.h"
#include "zephyr/net_buf.h"

#define VIDEO_WIDTH 480U
#define VIDEO_HEIGHT 480U
#define VIDEO_Y_SIZE (VIDEO_WIDTH * VIDEO_HEIGHT)
#define VIDEO_UV_SIZE (VIDEO_Y_SIZE / 2U)
#define VIDEO_BUFFER_COUNT 3U

struct webrtc_video_surface {
    uint8_t *y;
    uint8_t *uv;
    size_t y_capacity;
    size_t uv_capacity;
    uint16_t width;
    uint16_t height;
};

struct video_buffer {
    struct webrtc_video_surface frame;
    /* Isolate DMA-only pixels from CPU-written metadata at cache-line boundaries. */
    uint8_t pixels[VIDEO_Y_SIZE + VIDEO_UV_SIZE] __attribute__((aligned(BFLB_CACHE_LINE_SIZE)));
};

_Static_assert(VIDEO_Y_SIZE % BFLB_CACHE_LINE_SIZE == 0U &&
               VIDEO_UV_SIZE % BFLB_CACHE_LINE_SIZE == 0U, "NV12 must cover whole cache lines");

/* Native fixed storage keeps both the frame and its DMA pixels in PSRAM. */
_NET_BUF_ARRAY_DEFINE(video_pool, VIDEO_BUFFER_COUNT, 0);
static ATTR_NOINIT_PSRAM_SECTION struct video_buffer video_pool_data[VIDEO_BUFFER_COUNT];
static const struct net_buf_pool_fixed video_pool_fixed = {
    .data_pool = (uint8_t *)video_pool_data,
};
static const struct net_buf_data_alloc video_pool_alloc = {
    .cb = &net_buf_fixed_cb,
    .alloc_data = (void *)&video_pool_fixed,
    .max_alloc_size = sizeof(video_pool_data[0]),
};
static STRUCT_SECTION_ITERABLE(net_buf_pool, video_pool) =
    NET_BUF_POOL_INITIALIZER(video_pool, &video_pool_alloc, _net_buf_video_pool,
                             VIDEO_BUFFER_COUNT, 0, NULL);
static struct bflb_device_s *video_dpi;
static struct bflb_device_s *video_osd;
static struct net_buf *video_pending;
/* DSI switches one frame late; retain both the older and latest address sets. */
static struct net_buf *video_current;
static struct net_buf *video_next;
static volatile uint32_t video_displayed_frames;
static volatile uint32_t video_seof_count;
static volatile TickType_t video_submit_tick, video_last_handoff_tick, video_handoff_delay;
static bool video_ready;
static bool video_blank = true;
static bool video_unblank_pending;

static struct net_buf *video_frame_alloc(k_timeout_t timeout)
{
    struct net_buf *buf = net_buf_alloc_fixed(&video_pool, timeout);
    if (buf != NULL) {
        struct video_buffer *storage = net_buf_add(buf, sizeof(*storage));
        storage->frame = (struct webrtc_video_surface){
            .y = storage->pixels,
            .uv = storage->pixels + VIDEO_Y_SIZE,
            .y_capacity = VIDEO_Y_SIZE,
            .uv_capacity = VIDEO_UV_SIZE,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
        };
    }
    return buf;
}

/* Called by the existing OSD SEOF interrupt, including when no frame is queued. */
void mipi_dsi_v2_osd0_base_layer_swap(void)
{
    struct net_buf *retired = NULL;
    video_seof_count++;
    if (video_pending != NULL) {
        configASSERT(video_pending != video_current && video_pending != video_next);
        configASSERT(video_current == NULL || video_current->ref != 0U);
        configASSERT(video_next == NULL || video_next->ref != 0U);
        const struct webrtc_video_surface *frame = (const void *)video_pending->data;
        bflb_dpi_framebuffer_planar_switch(video_dpi, (uint32_t)(uintptr_t)frame->y,
                                          (uint32_t)(uintptr_t)frame->uv);
        if (video_next != NULL) {
            retired = video_current;
            video_current = video_next;
        }
        video_next = video_pending;
        video_pending = NULL;
        if (video_unblank_pending) {
            video_blank = false;
            video_unblank_pending = false;
        }
        video_displayed_frames++;
        video_last_handoff_tick = xTaskGetTickCountFromISR();
        video_handoff_delay = video_last_handoff_tick - video_submit_tick;
    }
    if (retired != NULL) net_buf_unref(retired);
}

static struct net_buf *video_acquire(uint32_t timeout_ms)
{
    unsigned int key = libpeer_net_buf_irq_lock();
    bool ready = video_ready;
    libpeer_net_buf_irq_unlock(key);
    return ready ? video_frame_alloc(K_MSEC(timeout_ms)) : NULL;
}

static int video_present(struct net_buf *buf)
{
    if (buf == NULL || net_buf_pool_get(buf->pool_id) != &video_pool ||
        buf->len != sizeof(struct video_buffer) || buf->size != sizeof(struct video_buffer) ||
        buf->data != buf->__buf) return -1;
    const struct video_buffer *storage = (const void *)buf->data;
    const struct webrtc_video_surface *frame = &storage->frame;
    if (frame->width != VIDEO_WIDTH || frame->height != VIDEO_HEIGHT ||
        frame->y != storage->pixels || frame->uv != storage->pixels + VIDEO_Y_SIZE ||
        frame->y_capacity != VIDEO_Y_SIZE || frame->uv_capacity != VIDEO_UV_SIZE) return -1;
    /* Pixels come directly from MJDEC DMA; the decoder owns cache maintenance. */
    unsigned int key = libpeer_net_buf_irq_lock();
    int result = -1;
    if (video_ready && video_pending == NULL && video_current != buf && video_next != buf) {
        /* Discard old SEOF before queuing the frame for the next boundary. */
        bflb_osd_int_clear(video_osd);
        bflb_irq_clear_pending(video_osd->irq_num);
        video_pending = buf;
        video_submit_tick = xTaskGetTickCount();
        if (video_blank) video_unblank_pending = true;
        result = 0;
    }
    libpeer_net_buf_irq_unlock(key);
    return result;
}

static void video_stop(void)
{
    unsigned int key = libpeer_net_buf_irq_lock();
    /* A queued frame can switch after stop; retain pending and both in-flight references. */
    video_blank = true;
    video_unblank_pending = false;
    libpeer_net_buf_irq_unlock(key);
}

int webrtc_video_rx_display_init(void)
{
    if (video_ready) return 0;
    video_dpi = bflb_device_get_by_name("dpi");
    video_osd = bflb_device_get_by_name("osd0");
    if (video_dpi == NULL || video_osd == NULL) return -1;
    libpeer_net_buf_init();
    video_current = video_frame_alloc(K_NO_WAIT);
    if (video_current == NULL) return -1;
    struct webrtc_video_surface *black = (void *)video_current->data;
    memset(black->y, 16, VIDEO_Y_SIZE);
    memset(black->uv, 128, VIDEO_UV_SIZE);
    /* CPU pixel access ends here; MJDEC and LCD own these surfaces thereafter. */
    for (unsigned int i = 0; i < VIDEO_BUFFER_COUNT; i++) {
        bflb_l1c_dcache_clean_invalidate_range(video_pool_data[i].pixels,
                                              VIDEO_Y_SIZE + VIDEO_UV_SIZE);
    }
    /* Initialize both address banks before the UI makes its overlay transparent. */
    unsigned int key = libpeer_net_buf_irq_lock();
    bflb_dpi_framebuffer_planar_switch(video_dpi, (uint32_t)(uintptr_t)black->y,
                                      (uint32_t)(uintptr_t)black->uv);
    bflb_dpi_framebuffer_planar_switch(video_dpi, (uint32_t)(uintptr_t)black->y,
                                      (uint32_t)(uintptr_t)black->uv);
    bflb_dpi_feature_control(video_dpi, DPI_CMD_SET_YUV420_UV_VALID_LINE,
                             DPI_YUV420_UV_VALID_LINE_EVEN);
    video_ready = true;
    libpeer_net_buf_irq_unlock(key);
    return 0;
}

void webrtc_video_rx_get_display_stats(struct webrtc_video_display_stats *stats)
{
    unsigned int key = libpeer_net_buf_irq_lock();
    stats->frames = video_displayed_frames;
    stats->active = video_ready && !video_blank;
    libpeer_net_buf_irq_unlock(key);
}

struct jpeg_owner {
    uint32_t generation;
    uint32_t enqueued_ms;
    size_t size;
};

#define RX_JPEG_BUFFER_COUNT 2U

/* Use native fixed allocation with the payload explicitly aligned in PSRAM. */
_NET_BUF_ARRAY_DEFINE(rx_jpeg_pool, RX_JPEG_BUFFER_COUNT, sizeof(struct jpeg_owner));
static ATTR_NOINIT_PSRAM_SECTION __attribute__((aligned(32)))
    uint8_t rx_jpeg_data[RX_JPEG_BUFFER_COUNT][VIDEO_JPEG_MAX + 32];
static const struct net_buf_pool_fixed rx_jpeg_fixed = {
    .data_pool = rx_jpeg_data[0],
};
static const struct net_buf_data_alloc rx_jpeg_alloc = {
    .cb = &net_buf_fixed_cb,
    .alloc_data = (void *)&rx_jpeg_fixed,
    .max_alloc_size = sizeof(rx_jpeg_data[0]),
};
static STRUCT_SECTION_ITERABLE(net_buf_pool, rx_jpeg_pool) =
    NET_BUF_POOL_INITIALIZER(rx_jpeg_pool, &rx_jpeg_alloc, _net_buf_rx_jpeg_pool,
                             RX_JPEG_BUFFER_COUNT, sizeof(struct jpeg_owner), NULL);

struct video_display_sink {
    struct mg_node node;
    struct webrtc_video_rx *rx;
};

struct webrtc_video_rx {
    struct mg_pipeline *pipeline;
    struct video_display_sink sink;
    QueueHandle_t ready;
    EventGroupHandle_t exited;
    struct net_buf *assembling;
    struct net_buf *worker_jpeg;
    struct net_buf *worker_output;
    struct video_fragment fragment;
    struct video_rx_stats stats;
    uint32_t generation;
    uint32_t worker_generation;
    bool open;
    uint32_t last_error_log;
};

static void video_stats_log(struct webrtc_video_rx *rx, struct video_rx_stats *previous,
                            uint32_t elapsed_ms)
{
    struct video_rx_stats stats;
    taskENTER_CRITICAL();
    bool open = rx->open;
    stats = rx->stats;
    stats.display_frames = video_displayed_frames;
    stats.display_seof = video_seof_count;
    TickType_t handoff_tick = video_last_handoff_tick, delay = video_handoff_delay;
    taskEXIT_CRITICAL();
    uint32_t handoffs = stats.display_frames;
    stats = video_stats_interval(&stats, previous);
    if (!open) {
        return;
    }
    unsigned long complete_fps_x10 =
        ((uint64_t)stats.complete_frames * 10000U + elapsed_ms / 2U) / elapsed_ms;
    unsigned long submit_fps_x10 =
        ((uint64_t)stats.submitted * 10000U + elapsed_ms / 2U) / elapsed_ms;
    printf("[video-rx] window=%lums messages=%lu complete=%lu submitted=%lu "
           "complete_fps=%lu.%lu submit_fps=%lu.%lu\r\n",
           (unsigned long)elapsed_ms, (unsigned long)stats.frames,
           (unsigned long)stats.complete_frames, (unsigned long)stats.submitted,
           complete_fps_x10 / 10U, complete_fps_x10 % 10U,
           submit_fps_x10 / 10U, submit_fps_x10 % 10U);
    printf("[video-rx] incomplete=%lu busy=%lu invalid=%lu queue_drop=%lu "
           "surface_drop=%lu pipeline_fail=%lu span_reject=%lu/%lu\r\n",
           (unsigned long)stats.incomplete_frames, (unsigned long)stats.busy_frames,
           (unsigned long)stats.invalid_frames, (unsigned long)stats.queue_drop,
           (unsigned long)stats.surface_drop, (unsigned long)stats.pipeline_fail,
           (unsigned long)stats.fragment_reject, (unsigned long)stats.fragments);
    unsigned long display_fps_x10 =
        ((uint64_t)stats.display_frames * 10000U + elapsed_ms / 2U) / elapsed_ms;
    printf("[video-lcd] fps=%lu.%lu handoffs=%lu seof_delta=%lu "
           "handoff_age_ms=%lu handoff_delay_ms=%lu\r\n",
           display_fps_x10 / 10U, display_fps_x10 % 10U,
           (unsigned long)handoffs, (unsigned long)stats.display_seof,
           (unsigned long)((xTaskGetTickCount() - handoff_tick) * portTICK_PERIOD_MS),
           (unsigned long)(delay * portTICK_PERIOD_MS));
}

static bool video_session_current(struct webrtc_video_rx *rx, uint32_t generation)
{
    bool current;
    taskENTER_CRITICAL();
    current = rx->open && rx->generation == generation;
    taskEXIT_CRITICAL();
    return current;
}

static void video_decoder_complete(struct mg_node *node, const void *data,
                                   size_t size, void *context)
{
    struct webrtc_video_rx *rx = context;
    struct net_buf *buffer = rx->worker_jpeg;
    (void)node;
    (void)data;
    (void)size;
    rx->worker_jpeg = NULL;
    net_buf_unref(buffer);
}

static int video_display_process(struct mg_node *node, const void *input,
                                 size_t input_size, void *output, size_t capacity)
{
    struct webrtc_video_rx *rx = ((struct video_display_sink *)node)->rx;
    int ret = -1;
    bool attempted = false;
    (void)input;
    (void)output;
    (void)capacity;
    if (input_size != sizeof(struct mg_mjpeg_hw_frame) || !rx->worker_output) {
        return -1;
    }
    /* The presenter is nonblocking; close and the final handoff must be atomic. */
    taskENTER_CRITICAL();
    if (rx->open && rx->generation == rx->worker_generation) {
        attempted = true;
        ret = video_present(rx->worker_output);
        if (ret == 0) {
            rx->stats.submitted++;
            rx->worker_output = NULL;
        }
    }
    taskEXIT_CRITICAL();
    uint32_t now = (uint32_t)bflb_mtimer_get_time_ms();
    if (attempted && ret < 0 &&
        (rx->stats.pipeline_fail < 3U || now - rx->last_error_log >= 5000U)) {
        const struct webrtc_video_surface *frame = (const void *)rx->worker_output->data;
        printf("[video-rx] t_ms=%lu stage=present ret=%d generation=%lu output=%p y=%p uv=%p size=%ux%u capacity=%lu/%lu\r\n",
               (unsigned long)now, ret, (unsigned long)rx->worker_generation,
               (void *)rx->worker_output, (void *)frame->y, (void *)frame->uv,
               (unsigned)frame->width, (unsigned)frame->height,
               (unsigned long)frame->y_capacity, (unsigned long)frame->uv_capacity);
    }
    return ret;
}

static const struct mg_node_ops video_display_ops = {
    .process = video_display_process,
};

static void video_decode_worker(void *argument)
{
    struct webrtc_video_rx *rx = argument;
    struct net_buf *jpeg;
    struct video_rx_stats previous_stats = {0};
    uint32_t last_log = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    for (;;) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now - last_log >= 5000U) {
            video_stats_log(rx, &previous_stats, now - last_log);
            last_log = now;
        }
        if (xQueueReceive(rx->ready, &jpeg, pdMS_TO_TICKS(1000)) != pdPASS) {
            continue;
        }
        if (!jpeg) {
            break;
        }
        struct jpeg_owner owner = *(struct jpeg_owner *)net_buf_user_data(jpeg);
        uint32_t dequeued = (uint32_t)bflb_mtimer_get_time_ms(), acquired = dequeued;
        rx->worker_jpeg = jpeg;
        rx->worker_generation = owner.generation;
        if (video_session_current(rx, owner.generation)) {
            rx->worker_output = video_acquire(20);
            acquired = (uint32_t)bflb_mtimer_get_time_ms();
            if (!rx->worker_output) {
                rx->stats.surface_drop++;
                if (rx->stats.surface_drop <= 3U || acquired - rx->last_error_log >= 5000U) {
                    printf("[video-rx] t_ms=%lu stage=acquire generation=%lu jpeg=%p size=%lu queue_ms=%lu acquire_ms=%lu\r\n",
                           (unsigned long)acquired, (unsigned long)owner.generation, (void *)jpeg->data,
                           (unsigned long)owner.size, (unsigned long)(dequeued - owner.enqueued_ms),
                           (unsigned long)(acquired - dequeued));
                    rx->last_error_log = acquired;
                }
            }
        }
        if (rx->worker_output && video_session_current(rx, owner.generation)) {
            const struct webrtc_video_surface *surface = (const void *)rx->worker_output->data;
            struct mg_mjpeg_hw_frame frame = {
                .jpeg = jpeg->data,
                .jpeg_size = owner.size,
                .y = surface->y,
                .uv = surface->uv,
                .y_capacity = surface->y_capacity,
                .uv_capacity = surface->uv_capacity,
            };
            int ret = surface->width != 480 || surface->height != 480 ? -1 :
                      mg_pipeline_input(rx->pipeline, &frame, sizeof(frame));
            uint32_t done = (uint32_t)bflb_mtimer_get_time_ms();
            if (ret < 0) {
                rx->stats.pipeline_fail++;
            }
            if ((ret < 0 && (rx->stats.pipeline_fail <= 3U || done - rx->last_error_log >= 5000U)) ||
                done - owner.enqueued_ms > 100U) {
                printf("[video-rx] t_ms=%lu stage=pipeline ret=%d generation=%lu jpeg=%p size=%lu y=%p uv=%p queue_ms=%lu acquire_ms=%lu pipeline_ms=%lu\r\n",
                       (unsigned long)done, ret, (unsigned long)owner.generation,
                       (void *)frame.jpeg, (unsigned long)owner.size, (void *)frame.y, (void *)frame.uv,
                       (unsigned long)(dequeued - owner.enqueued_ms), (unsigned long)(acquired - dequeued),
                       (unsigned long)(done - acquired));
                if (ret < 0) rx->last_error_log = done;
            }
        }
        if (rx->worker_jpeg) {
            net_buf_unref(rx->worker_jpeg);
            rx->worker_jpeg = NULL;
        }
        if (rx->worker_output) {
            net_buf_unref(rx->worker_output);
            rx->worker_output = NULL;
        }
    }
    xEventGroupSetBits(rx->exited, 1);
    vTaskDelete(NULL);
}

void webrtc_video_rx_set_open(struct webrtc_video_rx *rx, bool open)
{
    if (!rx) {
        return;
    }
    taskENTER_CRITICAL();
    rx->open = open;
    rx->generation++;
    if (rx->fragment.active) {
        rx->stats.incomplete_frames++;
    }
    taskEXIT_CRITICAL();
    if (!open) {
        video_stop();
    }
    if (rx->assembling) {
        net_buf_unref(rx->assembling);
        rx->assembling = NULL;
    }
    rx->fragment.active = false;
}

void webrtc_video_rx_input_fragment(struct webrtc_video_rx *rx, const void *data,
                                    size_t size, uint16_t sid, uint32_t tsn,
                                    uint8_t flags)
{
    struct net_buf *buffer;
    if (!rx || !rx->open) {
        return;
    }
    rx->stats.fragments++;
    if (flags & 0x02) {
        rx->stats.frames++;
        if (rx->fragment.active) {
            rx->stats.incomplete_frames++;
        }
        if (rx->assembling) {
            net_buf_unref(rx->assembling);
            rx->assembling = NULL;
        }
    }
    bool message_active = rx->fragment.active || (flags & 0x02);
    if (video_fragment_parse(&rx->fragment, data, size, sid, tsn, flags) < 0) {
        rx->stats.fragment_reject++;
        if (message_active) {
            rx->stats.invalid_frames++;
        }
        if (rx->assembling) {
            net_buf_unref(rx->assembling);
            rx->assembling = NULL;
        }
        return;
    }
    if (flags & 0x02) {
        rx->assembling = net_buf_alloc(&rx_jpeg_pool, K_MSEC(10));
        if (!rx->assembling) {
            rx->stats.busy_frames++;
        }
    }
    /* A message first seen while busy stays dropped until its E span. */
    if (!rx->assembling) {
        return;
    }
    memcpy(rx->assembling->data + rx->fragment.received - size, data, size);
    if (!(flags & 0x01)) {
        return;
    }
    buffer = rx->assembling;
    rx->assembling = NULL;
    buffer->len = rx->fragment.received;
    if (!video_jpeg_valid(buffer->data, buffer->len)) {
        net_buf_unref(buffer);
        rx->stats.invalid_frames++;
        return;
    }
    rx->stats.complete_frames++;
    struct jpeg_owner *owner = net_buf_user_data(buffer);
    owner->generation = rx->generation;
    owner->size = buffer->len;
    memset(buffer->data + buffer->len, 0, 32);
    owner->enqueued_ms = (uint32_t)bflb_mtimer_get_time_ms();
    if (xQueueSend(rx->ready, &buffer, 0) != pdPASS) {
        net_buf_unref(buffer);
        rx->stats.queue_drop++;
    }
}

void webrtc_video_rx_input(struct webrtc_video_rx *rx, const void *data, size_t size)
{
    webrtc_video_rx_input_fragment(rx, data, size, 0, 0, 0x03);
}

struct webrtc_video_rx *webrtc_video_rx_create(void)
{
    struct webrtc_video_rx *rx;
    struct mg_node *decoder = NULL;
    rx = calloc(1, sizeof(*rx));
    if (!rx) {
        return NULL;
    }
    rx->ready = xQueueCreate(1, sizeof(struct net_buf *));
    if (!rx->ready) {
        goto fail;
    }
    rx->exited = xEventGroupCreate();
    if (!rx->exited) {
        goto fail;
    }
    rx->pipeline = mg_pipeline_create(MG_MEDIA_VIDEO, sizeof(struct mg_mjpeg_hw_frame));
    decoder = mg_mjpeg_hw_decoder_node_create(480, 480);
    if (!rx->pipeline || !decoder) {
        goto fail;
    }
    if (mg_pipeline_register(rx->pipeline, decoder) < 0) {
        goto fail;
    }
    mg_node_set_process_complete_callback(decoder, video_decoder_complete, rx);
    decoder = NULL;
    rx->sink.node.ops = &video_display_ops;
    rx->sink.node.media_type = MG_MEDIA_VIDEO;
    rx->sink.rx = rx;
    if (mg_pipeline_register(rx->pipeline, &rx->sink.node) < 0 ||
        configMAX_PRIORITIES <= 8 ||
        xTaskCreate(video_decode_worker, "video_rx",
                    (2048 + sizeof(StackType_t) - 1) / sizeof(StackType_t),
                    rx, configMAX_PRIORITIES - 8, NULL) != pdPASS) {
        goto fail;
    }
    return rx;
fail:
    mg_node_destroy(decoder);
    mg_pipeline_destroy(rx->pipeline);
    if (rx->exited) {
        vEventGroupDelete(rx->exited);
    }
    if (rx->ready) {
        vQueueDelete(rx->ready);
    }
    free(rx);
    return NULL;
}

void webrtc_video_rx_destroy(struct webrtc_video_rx *rx)
{
    struct net_buf *stop = NULL;
    if (!rx) {
        return;
    }
    webrtc_video_rx_set_open(rx, false);
    xQueueSend(rx->ready, &stop, portMAX_DELAY);
    /* Decoder timeout first quiesces DMA; never free a worker's live frame. */
    xEventGroupWaitBits(rx->exited, 1, pdTRUE, pdFALSE, portMAX_DELAY);
    mg_pipeline_destroy(rx->pipeline);
    vEventGroupDelete(rx->exited);
    vQueueDelete(rx->ready);
    free(rx);
}
#else
#include <assert.h>

/* cc -DWEBRTC_VIDEO_RX_PROTOCOL_TEST -fsanitize=address,undefined this_file.c -o /tmp/video-rx-test */
int main(void)
{
    uint8_t message[VIDEO_JPEG_MAX] = {0xff, 0xd8};
    message[sizeof(message) - 2] = 0xff;
    message[sizeof(message) - 1] = 0xd9;
    assert(video_jpeg_valid(message, sizeof(message)));
    assert(!video_jpeg_valid(message, 3));
    assert(!video_jpeg_valid(message, sizeof(message) - 1));
    for (size_t split = 1; split < sizeof(message); split++) {
        struct video_fragment fragment = {0};
        assert(video_fragment_parse(&fragment, message, split, 3, UINT32_MAX, 2) == 0);
        assert(fragment.active && fragment.received == split);
        assert(video_fragment_parse(&fragment, message + split, sizeof(message) - split,
                                    3, 0, 1) == 0);
        assert(!fragment.active && fragment.received == sizeof(message));
    }
    struct video_fragment fragment = {0};
    for (unsigned invalid = 0; invalid < 6; invalid++) {
        assert(video_fragment_parse(&fragment, message, 100, 3, 10, 2) == 0);
        assert(video_fragment_parse(&fragment, invalid == 4 ? NULL : message + 100,
                                    invalid == 5 ? VIDEO_JPEG_MAX : 1,
                                    invalid == 3 ? 4 : 3,
                                    invalid == 0 ? 12 : invalid == 1 ? 9 :
                                    invalid == 2 ? 10 : 11, 1) < 0);
        assert(!fragment.active);
    }
    assert(video_fragment_parse(&fragment, message, sizeof(message), 3, 1, 3) == 0);
    assert(video_fragment_parse(&fragment, message, sizeof(message) + 1, 3, 2, 3) < 0);
    assert(video_fragment_parse(&fragment, message, 0, 3, 3, 3) < 0);
    struct video_rx_stats previous = {0}, totals = {
        .frames = 10, .submitted = 8, .display_frames = 7, .display_seof = 300,
    };
    struct video_rx_stats interval = video_stats_interval(&totals, &previous);
    assert(interval.frames == 10 && interval.submitted == 8);
    assert(interval.display_frames == 7 && interval.display_seof == 300);
    totals.frames++;
    interval = video_stats_interval(&totals, &previous);
    assert(interval.frames == 1 && interval.submitted == 0);
    assert(interval.display_frames == 0 && interval.display_seof == 0);
    return 0;
}
#endif
