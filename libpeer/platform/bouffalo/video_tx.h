#ifndef LIBPEER_BOUFFALO_VIDEO_TX_H
#define LIBPEER_BOUFFALO_VIDEO_TX_H

#include "media_config.h"

struct net_buf;
struct webrtc_media;

struct webrtc_video_frame {
    uint8_t *data;
    size_t size;
    size_t headroom;
    size_t tailroom;
    struct net_buf *buffer;
};

int webrtc_media_video_capture_start(struct webrtc_media *media,
                                     const webrtc_video_cfg_t *config);
int webrtc_media_video_capture_read(struct webrtc_media *media,
                                    struct webrtc_video_frame *frame,
                                    uint32_t timeout_ms);
void webrtc_media_video_capture_release(struct webrtc_media *media,
                                        const struct webrtc_video_frame *frame);
void webrtc_media_video_capture_stop(struct webrtc_media *media);

#endif
