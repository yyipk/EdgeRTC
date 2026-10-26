#!/usr/bin/env python3
"""Check OSD flushing and NV12 ownership with one-frame-late DSI switching."""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(os.environ["BL_SDK_BASE"])
base = pathlib.Path(__file__).resolve().parents[1]
platform = base.parents[1] / "platform"
app = (base / "app/app_ui.c").read_text()
video = (platform / "bouffalo/video_rx.c").read_text()
port = (root / "components/graphics/lvgl_v9/port/lv_port_disp_rgb.c").read_text()
dsi = (root / "bsp/common/lcd/mipi_dsi/mipi_dsi_v2.c").read_text()


def function(source, signature):
    start = source.index(signature + "\n{")
    return source[start:source.index("\n}\n", start) + 3]


harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "bouffalo/video_rx.h"
#define LCD_W 480
#define LCD_H 480
#define BFLB_CACHE_LINE_SIZE 64
#define configASSERT assert
#define CONFIG_FREERTOS 1
#define LCD_LVGL_RGB_TRIPLE_BUFF_MODE 0
#define LV_DISPLAY_RENDER_MODE_DIRECT 2
#define DPI_DATA_FORMAT_RGB565 1
#define DPI_DATA_FORMAT_Y_UV_PLANAR 2
#define K_MSEC(value) (value)
#define portMAX_DELAY 0
#define pdFALSE 0
#define portYIELD_FROM_ISR(value) ((void)(value))
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define lcd_backlight_toggle(on) assert(!"presenter must not change the backlight")
typedef uint32_t lcd_color_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef int *SemaphoreHandle_t;
struct bflb_device_s { int irq_num; };
struct net_buf { uint8_t *data, *__buf; size_t len, size; int pool_id, ref; };
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef struct _lv_display_t lv_display_t;
typedef void (*lv_display_flush_cb_t)(lv_display_t *, const lv_area_t *, uint8_t *);
struct _lv_display_t { bool last; int mode; lv_display_flush_cb_t flush_cb; };
typedef struct { int opacity; } lv_obj_t;
typedef struct { lv_obj_t *user_data; } lv_timer_t;
static lv_display_t test_display;
static lv_display_flush_cb_t ui_port_flush;
static lv_obj_t screen = { .opacity = LV_OPA_COVER }, fps_label;
static int opacity_updates, label_updates;
static unsigned int label_fps;
static uint32_t now_ms;
static uint8_t buffers[2][64], dma_frame[64];
static int ready_count, semaphore_takes, semaphore, clean_count, swap_count;
static SemaphoreHandle_t disp_sem = &semaphore;
static bool disp_flush_enabled = true;
static struct bflb_device_s dpi_device, osd_device = { .irq_num = 7 };
static void *dsi_v2_dpi = &dpi_device, *dsi_v2_osd = &osd_device;
static void *dsi_v2_screen_using = buffers[1], *dsi_v2_screen_pending = buffers[1];
static bool dsi_v2_screen_is_base;
static int dsi_v2_base_format = DPI_DATA_FORMAT_Y_UV_PLANAR;
static uint32_t dsi_v2_buf_size = sizeof(buffers[0]);
static void rgb_screen_frame_callback(void);
static void (*dsi_v2_swap_callback)(void) = rgb_screen_frame_callback;
static void (*dsi_v2_cycle_callback)(void);
'''
harness += video[video.index("#define VIDEO_WIDTH"):video.index("/* Native fixed storage")]
harness += video[video.index("static struct bflb_device_s *video_dpi;"):
                 video.index("static struct net_buf *video_frame_alloc(")]
harness += r'''
static int video_pool, planar_switches, controller_clears;
static uint32_t planar_y, planar_uv;
static struct video_buffer surfaces[3];
static struct net_buf frames[3], *dpi_scanning, *dpi_next;
static bool irq_locked, in_seof, peripheral_pending, controller_pending;
static void mipi_dsi_v2_osd0_isr(int irq, void *arg);
static void dispatch_seof(void) {
    assert(!irq_locked && !in_seof);
    irq_locked = in_seof = true; controller_pending = false;
    mipi_dsi_v2_osd0_isr(0, NULL);
    irq_locked = in_seof = false;
}
static uintptr_t bflb_irq_save(void) {
    assert(!irq_locked); irq_locked = true; return 0;
}
static void bflb_irq_restore(uintptr_t flags) {
    (void)flags; assert(irq_locked); irq_locked = false;
    if (peripheral_pending || controller_pending) {
        dispatch_seof();
    }
}
static unsigned int libpeer_net_buf_irq_lock(void) { return bflb_irq_save(); }
static void libpeer_net_buf_irq_unlock(unsigned int key) { bflb_irq_restore(key); }
static void bflb_osd_int_clear(void *osd) {
    assert(osd == dsi_v2_osd); peripheral_pending = false;
}
static void bflb_irq_clear_pending(int irq) {
    assert(irq_locked && irq == osd_device.irq_num && video_pending == NULL);
    controller_pending = false; controller_clears++;
}
static void bflb_dpi_framebuffer_planar_switch(void *dpi, uint32_t y, uint32_t uv) {
    assert(dpi == video_dpi && irq_locked && in_seof && video_pending != NULL);
    assert(video_current != video_pending && video_current->ref > 0 && video_pending->ref > 0);
    const struct webrtc_video_surface *frame = (const void *)video_pending->data;
    assert(y == (uint32_t)(uintptr_t)frame->y && uv == (uint32_t)(uintptr_t)frame->uv);
    /* The address written at this SEOF is consumed one frame later. */
    dpi_scanning = dpi_next; dpi_next = video_pending;
    assert(dpi_scanning->ref > 0 && dpi_next->ref > 0);
    planar_y = y; planar_uv = uv; planar_switches++;
}
static void *net_buf_pool_get(int id) { return id == 1 ? &video_pool : NULL; }
static void net_buf_unref(struct net_buf *buf) {
    assert(irq_locked && in_seof && video_current != buf && video_pending == NULL);
    assert(planar_switches == (int)video_displayed_frames && buf->ref > 0);
    assert(buf != dpi_scanning && buf != dpi_next);
    const struct webrtc_video_surface *frame = (const void *)dpi_next->data;
    assert(planar_y == (uint32_t)(uintptr_t)frame->y && planar_uv == (uint32_t)(uintptr_t)frame->uv);
    buf->ref--;
}
static struct net_buf *video_frame_alloc(uint32_t timeout) {
    (void)timeout;
    for (unsigned i = 0; i < 3; i++) {
        if (frames[i].ref == 0) {
            assert(&frames[i] != dpi_scanning && &frames[i] != dpi_next);
            frames[i].ref = 1;
            return &frames[i];
        }
    }
    return NULL;
}
static TickType_t xTaskGetTickCount(void) { return now_ms + 100; }
static TickType_t xTaskGetTickCountFromISR(void) { return now_ms + 110; }
static uint32_t lv_tick_get(void) { return now_ms; }
static void bflb_osd_blend_set_layer_buffer(void *osd, uint32_t address) {
    assert(osd == dsi_v2_osd); (void)address; swap_count++;
}
static void bflb_dpi_framebuffer_switch(void *dpi, uint32_t address) {
    assert(dpi == dsi_v2_dpi); (void)address;
}
void mipi_dsi_v2_osd0_base_layer_swap(void);
static void bflb_l1c_dcache_clean_range(void *address, uint32_t size) {
    assert(size == sizeof(dma_frame)); clean_count++;
    memcpy(dma_frame, address, size);
}
static bool lv_display_flush_is_last(lv_display_t *display) { return display->last; }
static void lv_display_flush_ready(lv_display_t *display) {
    assert(display == &test_display); ready_count++;
}
#define lv_disp_flush_ready lv_display_flush_ready
static void lv_display_set_flush_cb(lv_display_t *display, lv_display_flush_cb_t callback) {
    display->flush_cb = callback;
}
static void lv_display_set_render_mode(lv_display_t *display, int mode) { display->mode = mode; }
static lv_obj_t *lv_timer_get_user_data(lv_timer_t *timer) { return timer->user_data; }
static lv_obj_t *lv_obj_get_screen(lv_obj_t *label) {
    assert(label == &fps_label); return &screen;
}
static void lv_obj_set_style_bg_opa(lv_obj_t *object, int opacity, int selector) {
    assert(object == &screen && selector == 0);
    screen.opacity = opacity; opacity_updates++;
}
static void lv_label_set_text_fmt(lv_obj_t *label, const char *format, unsigned int fps) {
    assert(label == &fps_label && strcmp(format, "%u FPS") == 0);
    label_fps = fps; label_updates++;
}
'''
harness += function(video, "void mipi_dsi_v2_osd0_base_layer_swap(void)")
harness += function(video, "static struct net_buf *video_acquire(uint32_t timeout_ms)")
harness += function(video, "static int video_present(struct net_buf *buf)")
harness += function(video, "static void video_stop(void)")
harness += function(video, "void webrtc_video_rx_get_display_stats(struct webrtc_video_display_stats *stats)")
harness += function(dsi, "int mipi_dsi_v2_screen_switch(void *screen_buffer)")
harness += function(dsi, "static void mipi_dsi_v2_osd0_isr(int irq, void *arg)")
harness += r'''
static void *lcd_get_screen_using(void) { return dsi_v2_screen_using; }
static int lcd_screen_switch(lcd_color_t *buffer) { return mipi_dsi_v2_screen_switch(buffer); }
static void xSemaphoreTake(SemaphoreHandle_t sem, int timeout) {
    assert(sem == &semaphore && !*sem); (void)timeout; semaphore_takes++;
    assert(dsi_v2_screen_using != dsi_v2_screen_pending);
    int before = ready_count;
    peripheral_pending = controller_pending = true;
    dispatch_seof();
    assert(*sem && ready_count == before); *sem = 0;
}
static void xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken) {
    assert(sem == &semaphore && !*sem); *sem = 1; *woken = 0;
}
'''
harness += function(port, "static void rgb_screen_frame_callback(void)")
harness += function(port, "static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)")
harness += function(app, "static void ui_osd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)")
harness += function(app, "static void ui_video_timer(lv_timer_t *timer)")
harness += r'''
int main(void) {
    lv_display_t *display = &test_display;
    struct webrtc_video_display_stats stats;
    lv_timer_t timer = { .user_data = &fps_label };
    display->flush_cb = disp_flush;
'''
setup_start = app.index("    ui_port_flush = display->flush_cb;")
setup_end = app.index("\n", app.index("    lv_display_set_render_mode(", setup_start))
harness += app[setup_start:setup_end]
harness += r'''
    assert(ui_port_flush == disp_flush && display->flush_cb == ui_osd_flush);
    assert(display->mode == LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_area_t first = {1, 1, 2, 2}, last = {5, 5, 7, 5};
    buffers[0][4] = 0x11;
    display->last = false;
    display->flush_cb(display, &first, buffers[0]);
    assert(ready_count == 1 && !swap_count && !clean_count && !semaphore_takes);
    assert(dsi_v2_screen_pending == buffers[1]);
    buffers[0][40] = 0x22;
    display->last = true;
    display->flush_cb(display, &last, buffers[0]);
    assert(ready_count == 2 && swap_count == 1 && clean_count == 1 && semaphore_takes == 1);
    assert(dsi_v2_screen_using == buffers[0]);
    assert(dma_frame[4] == 0x11 && dma_frame[40] == 0x22);

    /* Next frame includes old pixels copied by LVGL plus a disjoint new update. */
    memcpy(buffers[1], buffers[0], sizeof(buffers[0]));
    buffers[1][60] = 0x33;
    display->flush_cb(display, &last, buffers[1]);
    assert(ready_count == 3 && swap_count == 2 && clean_count == 2 && semaphore_takes == 2);
    assert(dsi_v2_screen_using == buffers[1]);
    assert(dma_frame[4] == 0x11 && dma_frame[40] == 0x22 && dma_frame[60] == 0x33);

    display->flush_cb(display, &last, buffers[1]);
    assert(ready_count == 4 && swap_count == 2 && semaphore_takes == 2);
    disp_flush_enabled = false;
    display->flush_cb(display, &last, buffers[0]);
    assert(ready_count == 5 && swap_count == 2 && clean_count == 2 && semaphore_takes == 2);

    webrtc_video_rx_get_display_stats(&stats);
    assert(!stats.active && stats.frames == 0);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && !opacity_updates && !label_updates);
    assert(video_acquire(0) == NULL);

    for (unsigned i = 0; i < 3; i++) {
        surfaces[i].frame = (struct webrtc_video_surface){
            .y = surfaces[i].pixels, .uv = surfaces[i].pixels + VIDEO_Y_SIZE,
            .y_capacity = VIDEO_Y_SIZE, .uv_capacity = VIDEO_UV_SIZE,
            .width = LCD_W, .height = LCD_H,
        };
        frames[i] = (struct net_buf){
            .data = (void *)&surfaces[i], .__buf = (void *)&surfaces[i],
            .len = sizeof(surfaces[i]), .size = sizeof(surfaces[i]),
            .pool_id = 1, .ref = i < 2 ? 1 : 0,
        };
    }
    video_dpi = dsi_v2_dpi; video_osd = dsi_v2_osd;
    video_current = &frames[0];
    dpi_scanning = dpi_next = &frames[0]; /* Both banks initially contain black. */

    /* Rejection must leave the unrelated event available to the existing ISR. */
    peripheral_pending = controller_pending = true;
    assert(video_present(NULL) < 0);
    assert(peripheral_pending && controller_pending && !controller_clears);
    uint32_t before_seof = video_seof_count;
    assert(video_present(&frames[1]) < 0); /* Not ready. */
    assert(video_seof_count == before_seof + 1 && !controller_clears && !planar_switches);
    assert(video_current == &frames[0] && video_pending == NULL && frames[0].ref == 1);

    /* Neither an old peripheral status nor a latched CPU request confirms a new frame. */
    video_ready = true; video_blank = true;
    peripheral_pending = controller_pending = true;
    before_seof = video_seof_count;
    assert(video_present(&frames[1]) == 0);
    assert(video_current == &frames[0] && video_pending == &frames[1]);
    assert(frames[0].ref == 1 && frames[1].ref == 1 && video_blank);
    assert(planar_switches == 0); /* Task submission must not touch DPI. */
    assert(!peripheral_pending && !controller_pending && controller_clears == 1);
    assert(video_seof_count == before_seof && !video_displayed_frames);
    assert(video_present(&frames[1]) < 0); /* Cannot replace the pending entry. */
    assert(video_present(&frames[0]) < 0); /* Cannot submit current scanout. */
    assert(video_pending == &frames[1] && planar_switches == 0 && controller_clears == 1);
    webrtc_video_rx_get_display_stats(&stats);
    assert(!stats.active && stats.frames == 0);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && !opacity_updates && !label_updates);

    peripheral_pending = controller_pending = true; /* A NEW frame event. */
    bflb_irq_restore(bflb_irq_save());
    assert(video_current == &frames[0] && video_pending == NULL);
    assert(dpi_scanning == &frames[0] && dpi_next == &frames[1]);
    assert(frames[0].ref == 1 && frames[1].ref == 1 && !video_blank);
    assert(planar_switches == 1 && planar_y == (uint32_t)(uintptr_t)surfaces[1].frame.y);
    assert(planar_uv == (uint32_t)(uintptr_t)surfaces[1].frame.uv);
    assert(video_displayed_frames == 1 && video_handoff_delay == 10);

    /* Handoff reveals video immediately; FPS waits for a full one-second sample. */
    now_ms = 999;
    webrtc_video_rx_get_display_stats(&stats);
    assert(stats.active && stats.frames == 1);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_TRANSP && opacity_updates == 1 && !label_updates);
    now_ms = 1000;
    ui_video_timer(&timer);
    assert(label_fps == 1 && label_updates == 1 && opacity_updates == 1);
    now_ms = 1250;
    ui_video_timer(&timer);
    assert(label_updates == 1 && opacity_updates == 1);

    assert(video_present(&frames[0]) < 0);
    assert(video_present(&frames[1]) < 0); /* Latest bank is still in flight. */
    assert(video_acquire(0) == &frames[2]); /* Only the third surface is free. */
    assert(video_acquire(0) == NULL);
    peripheral_pending = controller_pending = true;
    bflb_irq_restore(bflb_irq_save()); /* Idle SEOF must not release either bank. */
    assert(frames[0].ref == 1 && frames[1].ref == 1 && video_displayed_frames == 1);

    assert(video_present(&frames[2]) == 0);
    assert(video_pending == &frames[2]);
    assert(planar_switches == 1);
    video_stop(); /* Stop keeps both in-flight references and pending. */
    assert(video_blank && !video_unblank_pending && video_pending == &frames[2]);
    assert(frames[0].ref == 1 && frames[1].ref == 1 && frames[2].ref == 1);
    assert(video_acquire(0) == NULL);
    webrtc_video_rx_get_display_stats(&stats);
    assert(!stats.active && stats.frames == 1);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && opacity_updates == 2);
    assert(label_fps == 0 && label_updates == 2);
    now_ms = 1500;
    ui_video_timer(&timer);
    assert(opacity_updates == 2 && label_updates == 2);
    peripheral_pending = controller_pending = true;
    bflb_irq_restore(bflb_irq_save());
    assert(video_current == &frames[1] && video_pending == NULL && video_blank);
    assert(dpi_scanning == &frames[1] && dpi_next == &frames[2]);
    assert(frames[0].ref == 0 && frames[1].ref == 1 && frames[2].ref == 1);
    assert(video_displayed_frames == 2 && planar_switches == 2);
    assert(planar_y == (uint32_t)(uintptr_t)surfaces[2].frame.y);
    assert(planar_uv == (uint32_t)(uintptr_t)surfaces[2].frame.uv);
    now_ms = 2000;
    webrtc_video_rx_get_display_stats(&stats);
    assert(!stats.active && stats.frames == 2);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && opacity_updates == 2 && label_updates == 2);

    assert(video_acquire(0) == &frames[0]); /* A retires only after C is submitted. */
    assert(video_present(&frames[0]) == 0 && video_blank);
    assert(video_acquire(0) == NULL);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && opacity_updates == 2 && label_updates == 2);
    peripheral_pending = controller_pending = true;
    bflb_irq_restore(bflb_irq_save());
    assert(video_current == &frames[2] && video_pending == NULL && !video_blank);
    assert(dpi_scanning == &frames[2] && dpi_next == &frames[0]);
    assert(frames[0].ref == 1 && frames[1].ref == 0 && frames[2].ref == 1);
    assert(video_displayed_frames == 3 && planar_switches == 3);
    now_ms = 2100;
    webrtc_video_rx_get_display_stats(&stats);
    assert(stats.active && stats.frames == 3);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_TRANSP && opacity_updates == 3);
    assert(label_fps == 0 && label_updates == 2);
    now_ms = 2999;
    ui_video_timer(&timer);
    assert(opacity_updates == 3 && label_updates == 2);
    /* A delayed sample uses elapsed time, rather than treating the frame delta as FPS. */
    now_ms = 4501;
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_TRANSP && opacity_updates == 3);
    assert(label_fps == 0 && label_updates == 2);
    ui_video_timer(&timer);
    assert(label_updates == 2 && opacity_updates == 3);

    /* Stop must also cancel unblanking when a first resumed frame is still pending. */
    video_stop();
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && opacity_updates == 4 && label_updates == 2);
    assert(video_acquire(0) == &frames[1]);
    assert(video_present(&frames[1]) == 0 && video_unblank_pending);
    video_stop();
    assert(video_blank && !video_unblank_pending && video_pending == &frames[1]);
    assert(frames[0].ref == 1 && frames[1].ref == 1 && frames[2].ref == 1);
    peripheral_pending = controller_pending = true;
    bflb_irq_restore(bflb_irq_save());
    assert(video_current == &frames[0] && video_next == &frames[1] && video_pending == NULL);
    assert(frames[0].ref == 1 && frames[1].ref == 1 && frames[2].ref == 0);
    webrtc_video_rx_get_display_stats(&stats);
    assert(!stats.active && stats.frames == 4 && video_displayed_frames == 4);
    ui_video_timer(&timer);
    assert(screen.opacity == LV_OPA_COVER && opacity_updates == 4 && label_updates == 2);
    assert(VIDEO_BUFFER_COUNT == 3);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webrtc-osd-direct-") as temp:
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-I", str(base / "app"),
                    "-I", str(platform),
                    "-x", "c", "-", "-o", str(binary)],
                   input=harness, text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("OSD flush/cache/SEOF wait, delayed DSI NV12 ownership, display counts and UI FPS/state: PASS")
