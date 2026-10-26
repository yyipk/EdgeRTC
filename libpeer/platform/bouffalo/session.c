#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#include "peer.h"
#include "utils.h"
#include "session.h"
#include "media.h"
#include "audio_tx.h"
#include "audio_rx.h"
#include "video_tx.h"
#ifdef CONFIG_WEBRTC_VIDEO_RX
#include "video_rx.h"
#endif

#define SIGNALING_VALUE_SIZE        256
#define VIDEO_DEVICE_NAME_SIZE       64
#define POLL_INTERVAL_MS             15
#define SIGNALING_RETRY_POLLS        \
    ((1000 + POLL_INTERVAL_MS - 1) / POLL_INTERVAL_MS)

#define SCTP_SEND_CACHE_MAX_PACKETS     32U
#define SCTP_RECEIVE_CACHE_MAX_BYTES    (192U * 1024U)

#define NETWORK_RX_TASK_STACK_SIZE    (8 * 512)
#define MEDIA_TX_TASK_STACK_SIZE      (8 * 512)
#define SIGNALING_TASK_STACK_SIZE     (10 * 512)
#define NETWORK_RX_TASK_PRIORITY      9
#define MEDIA_TX_TASK_PRIORITY       5
#define SIGNALING_TASK_PRIORITY      17
#define SIGNALING_EXITED_EVENT       (1U << 0)
#define NETWORK_RX_EXITED_EVENT      (1U << 1)
#define MEDIA_TX_EXITED_EVENT        (1U << 2)
#define VIDEO_DEBUG_INTERVAL_MS      10000U

enum webrtc_state {
    WEBRTC_STATE_OPEN,
    WEBRTC_STATE_RUNNING,
    WEBRTC_STATE_STOPPING,
};

enum call_control_action {
    CALL_CONTROL_NONE,
    CALL_CONTROL_CALL,
    CALL_CONTROL_HANGUP,
};

struct webrtc {
    struct webrtc_media media;
    webrtc_audio_cfg_t audio_config;
    webrtc_video_cfg_t video_config;
    bool video_receive_enabled;
#ifdef CONFIG_WEBRTC_VIDEO_RX
    struct webrtc_video_rx *video_rx;
#endif
    webrtc_video_control_t *owned_video_controls;
    SemaphoreHandle_t peer_mutex;
    EventGroupHandle_t worker_event;
    PeerConnection *peer_connection;
    uint8_t active_peer_users;
    uint8_t worker_mask;
    enum webrtc_state state;
    enum call_control_action call_action;
    bool network_ready;
    bool datachannel_open;
    bool audio_streams_running;
    uint32_t audio_rtp_invalid_packets;
    uint32_t audio_playback_queue_drops;
#if CONFIG_EDGERTC_SCTP_DEBUG
    uint32_t video_debug_last_ms;
    uint32_t video_read_frames;
    uint32_t video_sent_frames;
    uint32_t video_send_failures;
    uint32_t video_sent_bytes;
#endif
    char signaling_url[SIGNALING_VALUE_SIZE];
    char signaling_token[SIGNALING_VALUE_SIZE];
    char ice_server_url[SIGNALING_VALUE_SIZE];
    char ice_server_username[SIGNALING_VALUE_SIZE];
    char ice_server_credential[SIGNALING_VALUE_SIZE];
    char video_device_name[VIDEO_DEVICE_NAME_SIZE];
    char pending_call_device_id[WEBRTC_DEVICE_ID_SIZE];
};

static uintptr_t webrtc_lock(struct webrtc *rtc)
{
    (void)rtc;
    if (xPortIsInsideInterrupt()) {
        return (uintptr_t)taskENTER_CRITICAL_FROM_ISR();
    }
    taskENTER_CRITICAL();
    return 0;
}

static void webrtc_unlock(struct webrtc *rtc, uintptr_t key)
{
    (void)rtc;
    if (xPortIsInsideInterrupt()) {
        taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)key);
    } else {
        taskEXIT_CRITICAL();
    }
}

static void peer_lock(struct webrtc *rtc)
{
    xSemaphoreTake(rtc->peer_mutex, portMAX_DELAY);
}

static void peer_unlock(struct webrtc *rtc)
{
    xSemaphoreGive(rtc->peer_mutex);
}

static PeerConnection *peer_acquire(struct webrtc *rtc)
{
    PeerConnection *peer_connection = NULL;
    uintptr_t key;

    key = webrtc_lock(rtc);
    if (rtc->peer_connection != NULL) {
        peer_connection = rtc->peer_connection;
        rtc->active_peer_users++;
    }
    webrtc_unlock(rtc, key);
    return peer_connection;
}

static void peer_release(struct webrtc *rtc)
{
    uintptr_t key;

    key = webrtc_lock(rtc);
    if (rtc->active_peer_users > 0) {
        rtc->active_peer_users--;
    }
    webrtc_unlock(rtc, key);
}

static bool peer_is_present(struct webrtc *rtc)
{
    bool present;
    uintptr_t key;

    key = webrtc_lock(rtc);
    present = rtc->peer_connection != NULL;
    webrtc_unlock(rtc, key);
    return present;
}

static bool webrtc_is_running(struct webrtc *rtc)
{
    bool running;
    uintptr_t key;

    key = webrtc_lock(rtc);
    running = rtc->state == WEBRTC_STATE_RUNNING;
    webrtc_unlock(rtc, key);
    return running;
}

static void webrtc_set_state(struct webrtc *rtc, enum webrtc_state state)
{
    uintptr_t key;

    key = webrtc_lock(rtc);
    rtc->state = state;
    webrtc_unlock(rtc, key);
}

static int copy_config_string(char *destination, size_t capacity,
                              const char *source)
{
    size_t length;

    if (source == NULL) {
        destination[0] = '\0';
        return 0;
    }

    length = strlen(source);
    if (length >= capacity) {
        return -1;
    }
    memcpy(destination, source, length + 1);
    return 0;
}

static bool device_id_is_valid(const char *device_id)
{
    size_t index;

    if (device_id == NULL || strlen(device_id) != WEBRTC_DEVICE_ID_LENGTH) {
        return false;
    }
    for (index = 0; index < WEBRTC_DEVICE_ID_LENGTH; index++) {
        if (!isxdigit((unsigned char)device_id[index])) {
            return false;
        }
    }
    return true;
}

static void connection_state_callback(PeerConnectionState state,
                                      void *user_data)
{
    struct webrtc *rtc = user_data;

    LOGI("[webrtc] WebRTC state: %s",
         peer_connection_state_to_string(state));
    if (state != PEER_CONNECTION_COMPLETED) {
        rtc->datachannel_open = false;
#ifdef CONFIG_WEBRTC_VIDEO_RX
        webrtc_video_rx_set_open(rtc->video_rx, false);
#endif
    }
}

static void datachannel_open_callback(void *user_data)
{
    struct webrtc *rtc = user_data;

    rtc->datachannel_open = true;
#ifdef CONFIG_WEBRTC_VIDEO_RX
    webrtc_video_rx_set_open(rtc->video_rx, true);
#endif
    LOGI("[webrtc] MJPEG data channel open");
}

static void datachannel_close_callback(void *user_data)
{
    struct webrtc *rtc = user_data;

    rtc->datachannel_open = false;
#ifdef CONFIG_WEBRTC_VIDEO_RX
    webrtc_video_rx_set_open(rtc->video_rx, false);
#endif
}

#ifdef CONFIG_WEBRTC_VIDEO_RX
static void datachannel_message_callback(char *data, size_t size, void *user_data,
                                         uint16_t stream_id)
{
    struct webrtc *rtc = user_data;

    (void)stream_id;
    webrtc_video_rx_input(rtc->video_rx, data, size);
}

static void datachannel_fragment_callback(char *data, size_t size, void *user_data,
                                          uint16_t stream_id, uint32_t tsn, uint8_t flags)
{
    struct webrtc *rtc = user_data;

    webrtc_video_rx_input_fragment(rtc->video_rx, data, size, stream_id, tsn, flags);
}
#endif

static void audio_rtp_callback(uint8_t *data, size_t size,
                               const RtpPacketMetadata *metadata,
                               void *user_data)
{
    struct webrtc *rtc = user_data;

    if (data == NULL || metadata == NULL ||
        size != webrtc_media_audio_packet_size(&rtc->audio_config)) {
        rtc->audio_rtp_invalid_packets++;
        if (rtc->audio_rtp_invalid_packets == 1 ||
            (rtc->audio_rtp_invalid_packets % 50U) == 0) {
            LOGW("[webrtc] Discarded audio RTP payload: size=%u",
                 (unsigned int)size);
        }
        return;
    }
    if (webrtc_media_audio_playback_enqueue(
            &rtc->media, data, size, metadata->sequence_number,
            metadata->ssrc) < 0) {
        rtc->audio_playback_queue_drops++;
        if (rtc->audio_playback_queue_drops == 1 ||
            (rtc->audio_playback_queue_drops % 50U) == 0) {
            LOGW("[webrtc] Audio playback queue unavailable or full");
        }
    }
}

static int start_peer_connection(struct webrtc *rtc)
{
    PeerConnection *peer_connection;
    uintptr_t key;
    PeerConfiguration config = {
        .ice_servers = {
            {
                .urls = rtc->ice_server_url,
                .username = rtc->ice_server_username,
                .credential = rtc->ice_server_credential,
            },
        },
        .audio_codec = rtc->audio_config.enabled ?
                       CODEC_PCMA : CODEC_NONE,
        .video_codec = CODEC_NONE,
        .datachannel = (rtc->video_config.enabled || rtc->video_receive_enabled) ?
                       DATA_CHANNEL_BINARY : DATA_CHANNEL_NONE,
        .sctp_send_cache_max_packets = SCTP_SEND_CACHE_MAX_PACKETS,
        .sctp_receive_cache_max_bytes = SCTP_RECEIVE_CACHE_MAX_BYTES,
        .onaudiotrack = rtc->audio_config.enabled ?
                        audio_rtp_callback : NULL,
        .user_data = rtc,
    };

    peer_connection = peer_connection_create(&config);
    if (peer_connection == NULL) {
        LOGE("[webrtc] Failed to create WebRTC peer connection");
        return -1;
    }

    peer_connection_oniceconnectionstatechange(peer_connection,
                                               connection_state_callback);
    if (rtc->video_config.enabled || rtc->video_receive_enabled) {
        peer_connection_ondatachannel(peer_connection,
#ifdef CONFIG_WEBRTC_VIDEO_RX
                                      datachannel_message_callback,
#else
                                      NULL,
#endif
                                      datachannel_open_callback,
                                      datachannel_close_callback);
#ifdef CONFIG_WEBRTC_VIDEO_RX
        if (rtc->video_receive_enabled) {
            peer_connection_ondatachannelfragment(peer_connection,
                                                  datachannel_fragment_callback);
        }
#endif
    }
    if (peer_signaling_connect(rtc->signaling_url, rtc->signaling_token,
                               peer_connection) < 0) {
        LOGW("[webrtc] Signaling connection failed; retrying in 1 second");
        peer_signaling_disconnect();
        peer_connection_destroy(peer_connection);
        return -1;
    }

    key = webrtc_lock(rtc);
    if (!rtc->network_ready || rtc->peer_connection != NULL) {
        webrtc_unlock(rtc, key);
        peer_signaling_disconnect();
        peer_connection_destroy(peer_connection);
        return -1;
    }
    rtc->peer_connection = peer_connection;
    webrtc_unlock(rtc, key);
    return 0;
}

static void stop_audio_streams(struct webrtc *rtc)
{
    if (!rtc->audio_streams_running) {
        return;
    }

    rtc->audio_streams_running = false;
    webrtc_media_audio_capture_stop(&rtc->media);
    peer_lock(rtc);
    webrtc_media_audio_playback_stop(&rtc->media);
    peer_unlock(rtc);
}

static void stop_peer_connection(struct webrtc *rtc)
{
    PeerConnection *peer_connection;
    uintptr_t key;
    uint8_t active_peer_users;

    key = webrtc_lock(rtc);
    peer_connection = rtc->peer_connection;
    rtc->peer_connection = NULL;
    webrtc_unlock(rtc, key);

    do {
        key = webrtc_lock(rtc);
        active_peer_users = rtc->active_peer_users;
        webrtc_unlock(rtc, key);
        if (active_peer_users != 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } while (active_peer_users != 0);

    rtc->datachannel_open = false;
#ifdef CONFIG_WEBRTC_VIDEO_RX
    webrtc_video_rx_set_open(rtc->video_rx, false);
#endif
    stop_audio_streams(rtc);
    if (peer_connection != NULL) {
        peer_signaling_disconnect();
        peer_lock(rtc);
        peer_connection_close(peer_connection);
        peer_connection_destroy(peer_connection);
        peer_unlock(rtc);
    }
}

static void process_call_control(struct webrtc *rtc)
{
    char device_id[WEBRTC_DEVICE_ID_SIZE];
    enum call_control_action action;
    uintptr_t key;

    key = webrtc_lock(rtc);
    action = rtc->call_action;
    memcpy(device_id, rtc->pending_call_device_id, sizeof(device_id));
    rtc->call_action = CALL_CONTROL_NONE;
    rtc->pending_call_device_id[0] = '\0';
    webrtc_unlock(rtc, key);

    if (action == CALL_CONTROL_HANGUP) {
        if (peer_signaling_hangup() < 0) {
            LOGW("[webrtc] No active device call to end");
        }
    } else if (action == CALL_CONTROL_CALL &&
               peer_signaling_call(device_id) < 0) {
        LOGW("[webrtc] Unable to call device %s", device_id);
    }
}

static void signaling_poll(struct webrtc *rtc, uint16_t *retry_polls)
{
    uintptr_t key;
    bool network_ready;
    int ret;

    key = webrtc_lock(rtc);
    network_ready = rtc->network_ready;
    webrtc_unlock(rtc, key);

    if (!network_ready) {
        stop_peer_connection(rtc);
        *retry_polls = 0;
        goto wait;
    }

    if (*retry_polls > 0) {
        (*retry_polls)--;
        goto wait;
    }

    if (!peer_is_present(rtc) && start_peer_connection(rtc) < 0) {
        *retry_polls = SIGNALING_RETRY_POLLS;
        goto wait;
    }

    peer_lock(rtc);
    ret = peer_signaling_loop();
    if (ret == 0) {
        process_call_control(rtc);
    }
    peer_unlock(rtc);
    if (ret < 0) {
        LOGW("[webrtc] Signaling connection lost; retrying in 1 second");
        stop_peer_connection(rtc);
        *retry_polls = SIGNALING_RETRY_POLLS;
    }

wait:
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
}

static void peer_poll(struct webrtc *rtc)
{
    PeerConnection *peer_connection;

    peer_connection = peer_acquire(rtc);
    if (peer_connection == NULL) {
        return;
    }

    peer_lock(rtc);
    peer_connection_loop(peer_connection);
    /* Signaling can close the peer without a state-change callback. */
    if (rtc->datachannel_open &&
        peer_connection_get_state(peer_connection) != PEER_CONNECTION_COMPLETED) {
        datachannel_close_callback(rtc);
    }
    peer_unlock(rtc);
    peer_release(rtc);
}

static bool start_audio_streams(struct webrtc *rtc)
{
    int ret;

    if (!rtc->audio_config.enabled) {
        return false;
    }
    if (rtc->audio_streams_running) {
        return true;
    }

    ret = webrtc_media_audio_capture_start(&rtc->media,
                                           &rtc->audio_config);
    if (ret < 0) {
        LOGE("[webrtc] Failed to start audio capture");
        return false;
    }
    peer_lock(rtc);
    ret = webrtc_media_audio_playback_start(&rtc->media,
                                            &rtc->audio_config);
    peer_unlock(rtc);
    if (ret < 0) {
        LOGE("[webrtc] Failed to start audio playback");
        webrtc_media_audio_capture_stop(&rtc->media);
        return false;
    }
    rtc->audio_streams_running = true;
    return true;
}

static void send_audio_frame(struct webrtc *rtc,
                             PeerConnection *peer_connection,
                             uint8_t *packet, size_t capacity)
{
    int size;

    size = webrtc_media_audio_capture_read(&rtc->media, packet,
                                           capacity, POLL_INTERVAL_MS);
    if (size < 0) {
        LOGE("[webrtc] Failed to capture audio frame");
        return;
    }
    if (size == 0) {
        return;
    }
    if ((size_t)size != capacity) {
        LOGE("[webrtc] Invalid audio frame: got=%d expected=%u", size,
             (unsigned int)capacity);
        return;
    }
    peer_lock(rtc);
    size = peer_connection_send_audio(peer_connection, packet, capacity);
    peer_unlock(rtc);
    if (size < 0) {
        LOGE("[webrtc] Failed to send audio frame");
    }
}

static void send_video_frame(struct webrtc *rtc,
                             PeerConnection *peer_connection)
{
    struct webrtc_video_frame frame;
    int ret;

    ret = webrtc_media_video_capture_read(&rtc->media, &frame, 0);
    if (ret <= 0) {
        return;
    }
#if CONFIG_EDGERTC_SCTP_DEBUG
    rtc->video_read_frames++;
#endif

    peer_lock(rtc);
    if (rtc->datachannel_open) {
        ret = peer_connection_datachannel_send_inplace(
            peer_connection, frame.data, frame.size,
            frame.headroom, frame.tailroom);
    } else {
        ret = 0;
    }
    peer_unlock(rtc);

#if CONFIG_EDGERTC_SCTP_DEBUG
    if (ret == (int)frame.size) {
        rtc->video_sent_frames++;
        rtc->video_sent_bytes += frame.size;
    } else if (ret < 0) {
        rtc->video_send_failures++;
    }
#endif
    if (ret < 0) {
        LOGD("[webrtc] Failed to send video frame");
    }
    webrtc_media_video_capture_release(&rtc->media, &frame);
}

static void discard_video_frame(struct webrtc *rtc)
{
    struct webrtc_video_frame frame;

    if (rtc->video_config.enabled &&
        webrtc_media_video_capture_read(&rtc->media, &frame, 0) > 0) {
        webrtc_media_video_capture_release(&rtc->media, &frame);
    }
}

#if CONFIG_EDGERTC_SCTP_DEBUG
static void video_debug_log(struct webrtc *rtc)
{
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    if (now_ms - rtc->video_debug_last_ms < VIDEO_DEBUG_INTERVAL_MS) {
        return;
    }
    LOGI("video_diag: read=%lu sent=%lu fail=%lu bytes=%lu dc=%d",
         (unsigned long)rtc->video_read_frames,
         (unsigned long)rtc->video_sent_frames,
         (unsigned long)rtc->video_send_failures,
         (unsigned long)rtc->video_sent_bytes,
         rtc->datachannel_open);
    rtc->video_debug_last_ms = now_ms;
    rtc->video_read_frames = 0;
    rtc->video_sent_frames = 0;
    rtc->video_send_failures = 0;
    rtc->video_sent_bytes = 0;
}
#endif

static void media_send_poll(struct webrtc *rtc, uint8_t *audio_packet,
                            size_t packet_size)
{
    PeerConnection *peer_connection;
    bool audio_streams_running;

    peer_connection = peer_acquire(rtc);
    if (peer_connection == NULL) {
        discard_video_frame(rtc);
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        return;
    }
    if (peer_connection_get_state(peer_connection) != PEER_CONNECTION_COMPLETED) {
        stop_audio_streams(rtc);
        peer_release(rtc);
        discard_video_frame(rtc);
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        return;
    }

    audio_streams_running = start_audio_streams(rtc);
    if (audio_streams_running) {
        send_audio_frame(rtc, peer_connection, audio_packet, packet_size);
    }
    if (rtc->video_config.enabled) {
        send_video_frame(rtc, peer_connection);
#if CONFIG_EDGERTC_SCTP_DEBUG
        video_debug_log(rtc);
#endif
    }
    peer_release(rtc);
    if (!audio_streams_running) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void rtc_signaling_task(void *argument)
{
    struct webrtc *rtc = argument;
    uint16_t retry_polls = 0;

    while (webrtc_is_running(rtc)) {
        signaling_poll(rtc, &retry_polls);
    }
    xEventGroupSetBits(rtc->worker_event, SIGNALING_EXITED_EVENT);
    vTaskDelete(NULL);
}

static void rtc_network_receive_task(void *argument)
{
    struct webrtc *rtc = argument;

    while (webrtc_is_running(rtc)) {
        peer_poll(rtc);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    xEventGroupSetBits(rtc->worker_event, NETWORK_RX_EXITED_EVENT);
    vTaskDelete(NULL);
}

static void rtc_media_send_task(void *argument)
{
    struct webrtc *rtc = argument;
    size_t packet_size = rtc->audio_config.enabled ?
                         webrtc_media_audio_packet_size(
                             &rtc->audio_config) : 0;
    uint8_t audio_packet[packet_size != 0 ? packet_size : 1];

    while (webrtc_is_running(rtc)) {
        media_send_poll(rtc, audio_packet, packet_size);
    }
    xEventGroupSetBits(rtc->worker_event, MEDIA_TX_EXITED_EVENT);
    vTaskDelete(NULL);
}

static int start_worker(struct webrtc *rtc, uint8_t worker_bit,
                        const char *name, void (*entry)(void *),
                        size_t stack_size, uint32_t priority)
{
    configSTACK_DEPTH_TYPE stack_depth;

    if (stack_size == 0 || priority == 0 ||
        priority >= configMAX_PRIORITIES) {
        return -1;
    }
    stack_depth = (configSTACK_DEPTH_TYPE)
        ((stack_size + sizeof(StackType_t) - 1) / sizeof(StackType_t));

    if (xTaskCreate(entry, name, stack_depth, rtc,
                    configMAX_PRIORITIES - priority, NULL) != pdPASS) {
        return -1;
    }
    rtc->worker_mask |= worker_bit;
    return 0;
}

static void wait_worker_exit(struct webrtc *rtc, uint8_t worker_bit)
{
    if ((rtc->worker_mask & worker_bit) != 0) {
        xEventGroupWaitBits(rtc->worker_event, worker_bit, pdFALSE,
                            pdFALSE, portMAX_DELAY);
        rtc->worker_mask &= (uint8_t)~worker_bit;
    }
}

int webrtc_open(const webrtc_cfg_t *config, webrtc_handle_t *handle)
{
    struct webrtc *rtc;

    if (config == NULL || handle == NULL) {
        return -1;
    }
    *handle = NULL;

    rtc = calloc(1, sizeof(*rtc));
    if (rtc == NULL) {
        return -1;
    }
    rtc->audio_config = config->audio;
    rtc->video_config = config->video;
    rtc->video_receive_enabled = config->video_receive_enabled;
#ifndef CONFIG_WEBRTC_VIDEO_RX
    if (rtc->video_receive_enabled) {
        LOGE("[webrtc] Video receive support is disabled in this build");
        goto fail;
    }
#endif
    if (rtc->audio_config.enabled) {
        if (webrtc_media_audio_packet_size(&rtc->audio_config) == 0) {
            LOGE("[webrtc] Audio requires 8 kHz, 20 ms, 2-channel, "
                 "16-bit capture");
            goto fail;
        }
    }

    if (copy_config_string(rtc->signaling_url,
                           sizeof(rtc->signaling_url),
                           config->signaling_url) < 0 ||
        copy_config_string(rtc->signaling_token,
                           sizeof(rtc->signaling_token),
                           config->signaling_token) < 0 ||
        copy_config_string(rtc->ice_server_url,
                           sizeof(rtc->ice_server_url),
                           config->ice_server_url) < 0 ||
        copy_config_string(rtc->ice_server_username,
                           sizeof(rtc->ice_server_username),
                           config->ice_server_username) < 0 ||
        copy_config_string(rtc->ice_server_credential,
                           sizeof(rtc->ice_server_credential),
                           config->ice_server_credential) < 0 ||
        copy_config_string(rtc->video_device_name,
                           sizeof(rtc->video_device_name),
                           config->video.device_name) < 0) {
        goto fail;
    }
    if (strncmp(rtc->ice_server_url, "turn:", strlen("turn:")) == 0 &&
        (rtc->ice_server_username[0] == '\0' ||
         rtc->ice_server_credential[0] == '\0')) {
        LOGE("[webrtc] TURN requires username and credential");
        goto fail;
    }
    rtc->video_config.device_name = rtc->video_device_name;

    if (config->video.control_count > 0) {
        if (config->video.controls == NULL) {
            goto fail;
        }
        rtc->owned_video_controls = calloc(
            config->video.control_count,
            sizeof(*rtc->owned_video_controls));
        if (rtc->owned_video_controls == NULL) {
            goto fail;
        }
        memcpy(rtc->owned_video_controls, config->video.controls,
               config->video.control_count *
                   sizeof(*rtc->owned_video_controls));
        rtc->video_config.controls = rtc->owned_video_controls;
    }

    if (webrtc_media_init(&rtc->media) < 0) {
        goto fail;
    }
    rtc->peer_mutex = xSemaphoreCreateMutex();
    if (rtc->peer_mutex == NULL) {
        webrtc_media_deinit(&rtc->media);
        goto fail;
    }
    if (peer_init() < 0) {
        vSemaphoreDelete(rtc->peer_mutex);
        webrtc_media_deinit(&rtc->media);
        goto fail;
    }

    *handle = rtc;
    return 0;

fail:
    free(rtc->owned_video_controls);
    free(rtc);
    return -1;
}

int webrtc_start(webrtc_handle_t handle)
{
    struct webrtc *rtc = handle;

    if (rtc == NULL || rtc->state != WEBRTC_STATE_OPEN) {
        LOGE("[webrtc] start rejected: handle=%p state=%d", rtc,
             rtc != NULL ? rtc->state : -1);
        return -1;
    }
    rtc->worker_event = xEventGroupCreate();
    if (rtc->worker_event == NULL) {
        LOGE("[webrtc] start failed: worker event create");
        return -1;
    }
#ifdef CONFIG_WEBRTC_VIDEO_RX
    if (rtc->video_receive_enabled) {
        rtc->video_rx = webrtc_video_rx_create();
        if (rtc->video_rx == NULL) {
            LOGE("[webrtc] start failed: video receive pipeline");
            vEventGroupDelete(rtc->worker_event);
            rtc->worker_event = NULL;
            return -1;
        }
    }
#endif
    if (rtc->video_config.enabled &&
        webrtc_media_video_capture_start(&rtc->media,
                                         &rtc->video_config) < 0) {
        LOGE("[webrtc] start failed: video capture start");
#ifdef CONFIG_WEBRTC_VIDEO_RX
        webrtc_video_rx_destroy(rtc->video_rx);
        rtc->video_rx = NULL;
#endif
        vEventGroupDelete(rtc->worker_event);
        rtc->worker_event = NULL;
        LOGE("[webrtc] start failed: worker task creation");
        return -1;
    }
    webrtc_set_state(rtc, WEBRTC_STATE_RUNNING);

    if (start_worker(rtc, NETWORK_RX_EXITED_EVENT, "rtc_network_rx",
                     rtc_network_receive_task,
                     NETWORK_RX_TASK_STACK_SIZE,
                     NETWORK_RX_TASK_PRIORITY) < 0 ||
        start_worker(rtc, MEDIA_TX_EXITED_EVENT, "rtc_media_tx",
                     rtc_media_send_task, MEDIA_TX_TASK_STACK_SIZE,
                     MEDIA_TX_TASK_PRIORITY) < 0 ||
        start_worker(rtc, SIGNALING_EXITED_EVENT, "rtc_signaling",
                     rtc_signaling_task, SIGNALING_TASK_STACK_SIZE,
                     SIGNALING_TASK_PRIORITY) < 0) {
        webrtc_set_state(rtc, WEBRTC_STATE_STOPPING);
        wait_worker_exit(rtc, NETWORK_RX_EXITED_EVENT);
        wait_worker_exit(rtc, MEDIA_TX_EXITED_EVENT);
        wait_worker_exit(rtc, SIGNALING_EXITED_EVENT);
        stop_peer_connection(rtc);
#ifdef CONFIG_WEBRTC_VIDEO_RX
        webrtc_video_rx_destroy(rtc->video_rx);
        rtc->video_rx = NULL;
#endif
        if (rtc->video_config.enabled) {
            webrtc_media_video_capture_stop(&rtc->media);
        }
        vEventGroupDelete(rtc->worker_event);
        rtc->worker_event = NULL;
        webrtc_set_state(rtc, WEBRTC_STATE_OPEN);
        return -1;
    }
    return 0;
}

int webrtc_stop(webrtc_handle_t handle)
{
    struct webrtc *rtc = handle;

    if (rtc == NULL) {
        return -1;
    }
    if (rtc->state == WEBRTC_STATE_OPEN) {
        return 0;
    }

    webrtc_set_state(rtc, WEBRTC_STATE_STOPPING);
    wait_worker_exit(rtc, SIGNALING_EXITED_EVENT);
    wait_worker_exit(rtc, NETWORK_RX_EXITED_EVENT);
    wait_worker_exit(rtc, MEDIA_TX_EXITED_EVENT);

    stop_peer_connection(rtc);
#ifdef CONFIG_WEBRTC_VIDEO_RX
    webrtc_video_rx_destroy(rtc->video_rx);
    rtc->video_rx = NULL;
#endif
    if (rtc->video_config.enabled) {
        webrtc_media_video_capture_stop(&rtc->media);
    }
    vEventGroupDelete(rtc->worker_event);
    rtc->worker_event = NULL;
    webrtc_set_state(rtc, WEBRTC_STATE_OPEN);
    return 0;
}

int webrtc_close(webrtc_handle_t handle)
{
    struct webrtc *rtc = handle;

    if (rtc == NULL) {
        return -1;
    }
    webrtc_stop(rtc);
    peer_deinit();
    vSemaphoreDelete(rtc->peer_mutex);
    webrtc_media_deinit(&rtc->media);
    free(rtc->owned_video_controls);
    free(rtc);
    return 0;
}

int webrtc_set_network_ready(webrtc_handle_t handle, bool ready)
{
    struct webrtc *rtc = handle;
    uintptr_t key;

    if (rtc == NULL) {
        return -1;
    }
    key = webrtc_lock(rtc);
    rtc->network_ready = ready;
    webrtc_unlock(rtc, key);
    return 0;
}

int webrtc_call(webrtc_handle_t handle, const char *device_id)
{
    struct webrtc *rtc = handle;
    uintptr_t key;
    size_t index;

    if (rtc == NULL || !device_id_is_valid(device_id)) {
        return -1;
    }

    key = webrtc_lock(rtc);
    if (rtc->call_action != CALL_CONTROL_NONE) {
        webrtc_unlock(rtc, key);
        return -1;
    }
    for (index = 0; index < WEBRTC_DEVICE_ID_LENGTH; index++) {
        rtc->pending_call_device_id[index] =
            (char)tolower((unsigned char)device_id[index]);
    }
    rtc->pending_call_device_id[WEBRTC_DEVICE_ID_LENGTH] = '\0';
    rtc->call_action = CALL_CONTROL_CALL;
    webrtc_unlock(rtc, key);
    return 0;
}

int webrtc_hangup(webrtc_handle_t handle)
{
    struct webrtc *rtc = handle;
    uintptr_t key;

    if (rtc == NULL) {
        return -1;
    }

    key = webrtc_lock(rtc);
    rtc->pending_call_device_id[0] = '\0';
    rtc->call_action = CALL_CONTROL_HANGUP;
    webrtc_unlock(rtc, key);
    return 0;
}

#ifdef CONFIG_XWEBSOCKET
int webrtc_audio_websocket_register(webrtc_handle_t handle,
                                    const char *before_url,
                                    const char *after_url)
{
    struct webrtc *rtc = handle;

    if (rtc == NULL) {
        return -1;
    }
    return webrtc_media_audio_websocket_register(&rtc->media, &rtc->audio_config,
                                                before_url, after_url);
}
#endif

int webrtc_get_device_id(webrtc_handle_t handle,
                         char *device_id, size_t size)
{
    struct webrtc *rtc = handle;
    int ret;

    if (rtc == NULL) {
        return -1;
    }
    peer_lock(rtc);
    ret = peer_signaling_get_device_id(device_id, size);
    peer_unlock(rtc);
    return ret;
}
