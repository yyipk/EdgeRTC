#ifndef LIBPEER_BOUFFALO_AUDIO_TX_H
#define LIBPEER_BOUFFALO_AUDIO_TX_H

#include "media_config.h"

struct webrtc_media;

int webrtc_media_audio_capture_start(struct webrtc_media *media,
                                     const webrtc_audio_cfg_t *config);
int webrtc_media_audio_capture_read(struct webrtc_media *media,
                                    uint8_t *data, size_t size,
                                    uint32_t timeout_ms);
void webrtc_media_audio_capture_stop(struct webrtc_media *media);

#ifdef CONFIG_XWEBSOCKET
int webrtc_media_audio_websocket_register(struct webrtc_media *media,
                                          const webrtc_audio_cfg_t *audio_config,
                                          const char *before_url,
                                          const char *after_url);
#endif

#endif
