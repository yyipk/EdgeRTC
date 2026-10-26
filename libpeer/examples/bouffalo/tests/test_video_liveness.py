#!/usr/bin/env python3
"""Exercise real video capture/release and offline send paths with host stubs."""
import pathlib
import subprocess
import tempfile
base = pathlib.Path(__file__).resolve().parent.parent


def extract(path, signature):
    source = path.read_text()
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


platform = base.parents[1] / "platform/bouffalo"
media = platform / "video_tx.c"
core = platform / "session.c"
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define VIDEO_STOP_EVENT (1U << 0)
#define VIDEO_CAPTURE_EXITED_EVENT (1U << 1)
#define VIDEO_FRAME_RELEASED_EVENT (1U << 4)
#define K_NO_WAIT 0
#define POLL_INTERVAL_MS 15
#define PEER_CONNECTION_COMPLETED 2
#define CONFIG_EDGERTC_SCTP_DEBUG 0
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((ms) / 2U)
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef unsigned EventBits_t;
typedef unsigned *EventGroupHandle_t;
static int inside_interrupt, isr_events, isr_yields;
#define xPortIsInsideInterrupt() inside_interrupt
#define portYIELD_FROM_ISR(woken) do { assert(woken); isr_yields++; } while (0)
struct mg_video_source { int unused; };
struct mg_video_frame { uint8_t *data; size_t size, headroom, tailroom; };
struct video_buffer_owner { struct mg_video_source *source;
                            struct mg_video_frame frame; EventGroupHandle_t release_event; };
struct net_buf { uint8_t *data; size_t len; int refs;
                 struct video_buffer_owner *owner; };
struct webrtc_video_frame { uint8_t *data; size_t size; };
struct webrtc_media { struct mg_video_source *video_source;
                      EventGroupHandle_t video_event; void *video_webrtc_queue;
                      int queued; };
typedef struct { int state; } PeerConnection;
struct webrtc { struct webrtc_media media; struct { bool enabled; } video_config;
                PeerConnection *peer_connection; bool datachannel_open; };
static int video_buffer_pool, source_reads, source_releases, destroyed;
static int release_waits, queue_sends, frame_reads, frame_releases;
static int peer_releases, audio_stops, delays;
static struct net_buf *held;
static uint8_t pixels[8];
static unsigned event_bits;
static struct mg_video_source source;
static struct video_buffer_owner *net_buf_user_data(struct net_buf *b) { return b->owner; }
static void mg_video_source_release(struct mg_video_source *s,
                                    struct mg_video_frame *f) {
    assert(s == &source && f->data == pixels && !destroyed);
    source_releases++;
}
static void net_buf_destroy(struct net_buf *b) {
    assert(source_releases == 1 && !destroyed);
    destroyed++; free(b->owner); free(b);
}
static EventBits_t xEventGroupSetBits(EventGroupHandle_t e, EventBits_t bits) {
    assert(e == &event_bits);
    if (bits == VIDEO_FRAME_RELEASED_EVENT) assert(destroyed == 1);
    *e |= bits;
    return *e;
}
static BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t e, EventBits_t bits,
                                           BaseType_t *woken) {
    assert(inside_interrupt && woken && *woken == pdFALSE);
    isr_events++; *woken = pdTRUE;
    xEventGroupSetBits(e, bits);
    return pdPASS;
}
static EventBits_t xEventGroupGetBits(EventGroupHandle_t e) {
    assert(e == &event_bits);
    return *e;
}
'''
harness += extract(media, "static void video_buffer_destroy(")
harness += r'''
static struct net_buf *net_buf_ref(struct net_buf *b) { b->refs++; return b; }
static void net_buf_unref(struct net_buf *b) {
    assert(b->refs > 0);
    if (--b->refs == 0) video_buffer_destroy(b);
}
static EventBits_t xEventGroupWaitBits(EventGroupHandle_t e, EventBits_t mask,
                                      BaseType_t clear, BaseType_t all,
                                      TickType_t timeout) {
    assert(e == &event_bits);
    assert(mask == VIDEO_FRAME_RELEASED_EVENT && clear && !all &&
           timeout == pdMS_TO_TICKS(20));
    assert(held && source_reads == 3);
    release_waits++;
    *e |= VIDEO_STOP_EVENT;
    net_buf_unref(held); held = NULL;
    assert((*e & (VIDEO_STOP_EVENT | mask)) == (VIDEO_STOP_EVENT | mask));
    EventBits_t bits = *e;
    *e &= ~mask;
    return bits;
}
static int mg_video_source_read(struct mg_video_source *s,
                                struct mg_video_frame *f, unsigned timeout) {
    assert(s == &source && timeout == 20 && source_reads < 3);
    source_reads++;
    if (source_reads == 1) return 0;
    if (source_reads == 3) return -1;
    *f = (struct mg_video_frame){ .data = pixels, .size = sizeof(pixels) };
    return 1;
}
static struct net_buf *net_buf_alloc_with_data(int *pool, uint8_t *data,
                                               size_t capacity, int timeout) {
    assert(pool == &video_buffer_pool && data == pixels &&
           capacity == sizeof(pixels) && timeout == K_NO_WAIT);
    struct net_buf *b = calloc(1, sizeof(*b));
    assert(b);
    b->owner = calloc(1, sizeof(*b->owner));
    assert(b->owner); b->refs = 1;
    return b;
}
static BaseType_t xQueueSend(void *queue, struct net_buf **b, TickType_t timeout) {
    assert(queue && !timeout && (*b)->owner->release_event == &event_bits);
    assert((*b)->owner->source == &source);
    queue_sends++; held = *b;
    return pdPASS;
}
static void vTaskDelete(void *task) { assert(task == NULL); }
'''
harness += extract(media, "static void video_capture_task(")
harness += r'''
static int webrtc_media_video_capture_read(struct webrtc_media *m,
                                           struct webrtc_video_frame *f,
                                           unsigned timeout) {
    assert(!timeout);
    frame_reads++;
    if (!m->queued) return 0;
    *f = (struct webrtc_video_frame){ .data = pixels, .size = sizeof(pixels) };
    return 1;
}
static void webrtc_media_video_capture_release(struct webrtc_media *m,
                                               struct webrtc_video_frame *f) {
    assert(m->queued && f->data == pixels);
    m->queued = 0; frame_releases++;
}
static PeerConnection *peer_acquire(struct webrtc *rtc) { return rtc->peer_connection; }
static int peer_connection_get_state(PeerConnection *p) { return p->state; }
static void peer_release(struct webrtc *rtc) { assert(rtc->peer_connection); peer_releases++; }
static void stop_audio_streams(struct webrtc *rtc) { (void)rtc; audio_stops++; }
static void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(POLL_INTERVAL_MS)); delays++;
}
static bool start_audio_streams(struct webrtc *rtc) { (void)rtc; return false; }
static void send_audio_frame(struct webrtc *rtc, PeerConnection *p,
                             uint8_t *packet, size_t size) {
    (void)rtc; (void)p; (void)packet; (void)size; assert(0);
}
static void send_video_frame(struct webrtc *rtc, PeerConnection *p) {
    (void)rtc; (void)p; assert(0);
}
'''
harness += extract(core, "static void discard_video_frame(")
harness += extract(core, "static void media_send_poll(")
harness += r'''
int main(void) {
    struct webrtc_media media = { .video_source = &source, .video_event = &event_bits,
                                  .video_webrtc_queue = &source };
    video_capture_task(&media);
    assert(source_reads == 3 && source_releases == 1 && destroyed == 1);
    assert(queue_sends == 1 && release_waits == 1 && held == NULL);
    assert(event_bits == (VIDEO_STOP_EVENT | VIDEO_CAPTURE_EXITED_EVENT));
    assert(!isr_events && !isr_yields);
    source_releases = destroyed = 0;
    struct net_buf *buffer = net_buf_alloc_with_data(&video_buffer_pool, pixels,
                                                     sizeof(pixels), K_NO_WAIT);
    buffer->owner->source = &source;
    buffer->owner->frame.data = pixels;
    buffer->owner->release_event = &event_bits;
    inside_interrupt = 1;
    net_buf_unref(buffer);
    inside_interrupt = 0;
    assert(source_releases == 1 && destroyed == 1 && isr_events == 1 && isr_yields == 1);
    assert(event_bits & VIDEO_FRAME_RELEASED_EVENT);
    PeerConnection connecting = { .state = 1 };
    struct webrtc rtc = { .video_config = { .enabled = true } };
    uint8_t packet = 0;
    rtc.media.queued = 1;
    media_send_poll(&rtc, &packet, 1);
    assert(!rtc.media.queued && frame_reads == 1 && frame_releases == 1);
    rtc.peer_connection = &connecting;
    rtc.datachannel_open = true;
    rtc.media.queued = 1;
    media_send_poll(&rtc, &packet, 1);
    assert(!rtc.media.queued && frame_reads == 2 && frame_releases == 2);
    assert(rtc.datachannel_open && audio_stops == 1);
    assert(peer_releases == 1 && delays == 2);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webrtc-video-liveness-") as temp:
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-", "-o", str(binary)],
                   input=harness, text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("video capture retry, task/ISR release, and offline release: PASS")
