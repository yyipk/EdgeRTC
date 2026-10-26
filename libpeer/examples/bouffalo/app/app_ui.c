#include "app_ui.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "lcd.h"
#include "lvgl.h"
#include "lv_port_disp.h"

#ifdef CONFIG_WEBRTC_VIDEO_RX
#include "bouffalo/video_rx.h"
#include "lv_display_private.h"

#if !defined(CONFIG_LVGL9_OSD_LAYER) || LV_COLOR_DEPTH != 32 || LCD_W != 480 || LCD_H != 480 || LCD_LVGL_RGB_TRIPLE_BUFF_MODE
#error "WebRTC NV12 display requires the double-buffered 480x480 ARGB8888 OSD panel"
#endif

static lv_display_flush_cb_t ui_port_flush;

static void ui_osd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    /* DIRECT mode draws all dirty areas before handing the OSD buffer to DMA. */
    if (lv_display_flush_is_last(display)) {
        ui_port_flush(display, area, pixels);
    } else {
        lv_display_flush_ready(display);
    }
}

static void ui_video_timer(lv_timer_t *timer)
{
    static unsigned int shown_fps;
    static bool video_active;
    static uint32_t previous_ms;
    static uint32_t previous_frames;
    struct webrtc_video_display_stats stats;
    lv_obj_t *label = lv_timer_get_user_data(timer);

    webrtc_video_rx_get_display_stats(&stats);
    if (stats.active != video_active) {
        lv_obj_set_style_bg_opa(lv_obj_get_screen(label),
                                 stats.active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        video_active = stats.active;
    }
    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - previous_ms;
    unsigned int fps = shown_fps;
    if (elapsed >= 1000U) {
        fps = (unsigned int)(((uint64_t)(stats.frames - previous_frames) * 1000U + elapsed / 2U) / elapsed);
        previous_ms = now;
        previous_frames = stats.frames;
    }
    if (!stats.active) fps = 0;
    if (fps != shown_fps) {
        lv_label_set_text_fmt(label, "%u FPS", fps);
        shown_fps = fps;
    }
}
#endif

#define DBG_TAG "APP_UI"
#include "log.h"

static TaskHandle_t display_handle;

static uint32_t display_tick_ms(void)
{
    return (uint32_t)bflb_mtimer_get_time_ms();
}

static bool ui_display_init(void)
{
    lv_init();
    lv_tick_set_cb(display_tick_ms);
    lv_display_t *display = lv_port_disp_init();
    if (display == NULL) {
        LOG_E("Failed to create LVGL display\r\n");
        return false;
    }
#ifdef CONFIG_WEBRTC_VIDEO_RX
    if (webrtc_video_rx_display_init() < 0) {
        LOG_E("Failed to initialize video display\r\n");
        return false;
    }
    ui_port_flush = display->flush_cb;
    lv_display_set_flush_cb(display, ui_osd_flush);
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_DIRECT);
#endif
#if (LCD_INTERFACE_TYPE == LCD_INTERFACE_DBI) || (LCD_INTERFACE_TYPE == LCD_INTERFACE_SPI)
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
#endif
    return true;
}

static void ui_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x102030), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_white(), 0);
#ifdef CONFIG_WEBRTC_VIDEO_RX
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, 0);
#endif
    lv_obj_t *fps_label = lv_label_create(screen);
    lv_label_set_text(fps_label, "0 FPS");
    lv_obj_align(fps_label, LV_ALIGN_TOP_RIGHT, -12, 12);
#ifdef CONFIG_WEBRTC_VIDEO_RX
    lv_timer_create(ui_video_timer, 250, fps_label);
#endif
}

static void display_task(void *parameter)
{
    (void)parameter;
    if (!ui_display_init()) {
        vTaskDelete(NULL);
        return;
    }
    ui_create();
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(40));
    lcd_backlight_toggle(true);
    LOG_I("LVGL 9 display started: %u x %u\r\n", (unsigned)LCD_W, (unsigned)LCD_H);

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_ui_init(void)
{
    if (display_handle != NULL) return;
    if (xTaskCreate(display_task, "display", 4096, NULL,
                    tskIDLE_PRIORITY + 1, &display_handle) != pdPASS) {
        LOG_E("Failed to create LVGL display task\r\n");
    }
}
