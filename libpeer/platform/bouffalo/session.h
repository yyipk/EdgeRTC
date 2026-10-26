#ifndef LIBPEER_BOUFFALO_SESSION_H
#define LIBPEER_BOUFFALO_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "media_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEBRTC_DEVICE_ID_LENGTH 12
#define WEBRTC_DEVICE_ID_SIZE    \
    (WEBRTC_DEVICE_ID_LENGTH + 1)

typedef void *webrtc_handle_t;

typedef struct {
    const char *signaling_url;
    const char *signaling_token;
    const char *ice_server_url;
    const char *ice_server_username;
    const char *ice_server_credential;
    webrtc_audio_cfg_t audio;
    webrtc_video_cfg_t video;
    bool video_receive_enabled;
} webrtc_cfg_t;

int webrtc_open(const webrtc_cfg_t *config, webrtc_handle_t *handle);
int webrtc_start(webrtc_handle_t handle);
int webrtc_stop(webrtc_handle_t handle);
int webrtc_close(webrtc_handle_t handle);

int webrtc_set_network_ready(webrtc_handle_t handle, bool ready);
int webrtc_call(webrtc_handle_t handle, const char *device_id);
int webrtc_hangup(webrtc_handle_t handle);
#ifdef CONFIG_XWEBSOCKET
int webrtc_audio_websocket_register(webrtc_handle_t handle,
                                    const char *before_url,
                                    const char *after_url);
#endif
int webrtc_get_device_id(webrtc_handle_t handle,
                         char *device_id, size_t size);

#ifdef __cplusplus
}
#endif

#endif
