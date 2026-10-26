#ifndef LIBPEER_BOUFFALO_VIDEO_RX_H
#define LIBPEER_BOUFFALO_VIDEO_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct webrtc_video_rx;

struct webrtc_video_display_stats {
    uint32_t frames;
    bool active;
};

/* Call once after the panel/OSD port is initialized, before showing the UI. */
int webrtc_video_rx_display_init(void);
/* Snapshot only; the UI covers retained video when active is false. */
void webrtc_video_rx_get_display_stats(struct webrtc_video_display_stats *stats);

struct webrtc_video_rx *webrtc_video_rx_create(void);
/* These callbacks must be serialized by the peer mutex; input is borrowed. */
void webrtc_video_rx_set_open(struct webrtc_video_rx *rx, bool open);
void webrtc_video_rx_input(struct webrtc_video_rx *rx, const void *data, size_t size);
/* Raw JPEG B..E spans, delivered in order after SCTP verifies the complete message. */
void webrtc_video_rx_input_fragment(struct webrtc_video_rx *rx, const void *data,
                                    size_t size, uint16_t sid, uint32_t tsn,
                                    uint8_t flags);
/* Stop callbacks first. Joins the decode worker before freeing its buffers. */
void webrtc_video_rx_destroy(struct webrtc_video_rx *rx);

#endif
