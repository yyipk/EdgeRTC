#ifndef LIBPEER_BOUFFALO_AUDIO_RX_H
#define LIBPEER_BOUFFALO_AUDIO_RX_H

#include "media_config.h"

struct webrtc_media;

int webrtc_media_audio_playback_start(struct webrtc_media *media,
                                      const webrtc_audio_cfg_t *config);
int webrtc_media_audio_playback_enqueue(struct webrtc_media *media,
                                        const uint8_t *data, size_t size,
                                        uint16_t sequence_number,
                                        uint32_t ssrc);
void webrtc_media_audio_playback_stop(struct webrtc_media *media);

#endif
