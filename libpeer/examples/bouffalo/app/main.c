#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include <lwip/tcpip.h>

#include "async_event.h"
#include "bflb_core.h"
#include "bflb_mtd.h"
#include "bflb_rtc.h"
#include "bflb_uart.h"
#include "bl_fw_api.h"
#include "bl_sys.h"
#include "board.h"
#ifdef CONFIG_BSP_LCD
#include "app_ui.h"
#endif
#include "devices/drv_snd.h"
#include "easyflash.h"
#include "lfs_kv.h"
#include "fhost_api.h"
#include "rfparam_adapter.h"
#include "shell.h"
#include "msp_port.h"
#ifdef BL618DG
#include "mm.h"
#endif
#include "wifi_mgmr.h"
#include "wifi_mgmr_ext.h"

#include "v4l2.h"
#include "bouffalo/session.h"
#include "edgertc_stats.h"

#define DBG_TAG "WEBRTC"
#include "log.h"

#define SIGNALING_VALUE_SIZE 256
#define SIGNALING_URL_KEY    "WEBRTC_SIGNALING_URL"
#define SIGNALING_TOKEN_KEY  "WEBRTC_SIGNALING_TOKEN"
#define ICE_SERVER_URL_KEY   "WEBRTC_ICE_SERVER_URL"
#define ICE_USERNAME_KEY     "WEBRTC_ICE_USERNAME"
#define ICE_CREDENTIAL_KEY   "WEBRTC_ICE_CREDENTIAL"
#define WIFI_SSID_KEY        "WEBRTC_WIFI_SSID"
#define WIFI_PASSWORD_KEY    "WEBRTC_WIFI_PASSWORD"
#define WIFI_CREDENTIALS_KEY "WEBRTC_WIFI_CREDENTIALS"

typedef struct {
    char ssid[MGMR_SSID_LEN + 1];
    char password[MGMR_KEY_LEN + 1];
} wifi_credentials_t;

static webrtc_handle_t webrtc;
static volatile uint32_t wifi_state;
static wifi_credentials_t wifi_credentials;
static const webrtc_video_control_t video_controls[] = {
    {
        .id = V4L2_CID_JPEG_COMPRESSION_QUALITY,
        .value = 50,
    },
};

static bool read_config_string(const char *key, char *value, size_t capacity)
{
    size_t stored_size = 0;
    size_t read_size;

    memset(value, 0, capacity);
    read_size = lfs_kv_get_blob(key, value, capacity, &stored_size);
    if (stored_size == 0 || stored_size > capacity ||
        read_size != stored_size || memchr(value, '\0', read_size) == NULL) {
        memset(value, 0, capacity);
        return false;
    }
    return true;
}

static void read_wifi_credentials(void)
{
    size_t stored_size = 0;
    size_t read_size;

    read_size = lfs_kv_get_blob(WIFI_CREDENTIALS_KEY, &wifi_credentials,
                                sizeof(wifi_credentials), &stored_size);
    if (read_size == sizeof(wifi_credentials) &&
        stored_size == sizeof(wifi_credentials) &&
        memchr(wifi_credentials.ssid, '\0', sizeof(wifi_credentials.ssid)) != NULL &&
        memchr(wifi_credentials.password, '\0', sizeof(wifi_credentials.password)) != NULL) {
        return;
    }

    memset(&wifi_credentials, 0, sizeof(wifi_credentials));
    read_config_string(WIFI_SSID_KEY, wifi_credentials.ssid,
                       sizeof(wifi_credentials.ssid));
    if (!read_config_string(WIFI_PASSWORD_KEY, wifi_credentials.password,
                            sizeof(wifi_credentials.password))) {
        wifi_credentials.ssid[0] = '\0';
    }
}

static void save_wifi_credentials(void *parameter, uint32_t value)
{
    wifi_mgmr_connect_ind_stat_info_t wifi_info = {0};
    wifi_credentials_t credentials = {0};

    (void)parameter;
    (void)value;
    if (wifi_mgmr_sta_connect_ind_stat_get(&wifi_info) != 0 ||
        memchr(wifi_info.ssid, '\0', sizeof(wifi_info.ssid)) == NULL ||
        memchr(wifi_info.passphr, '\0', sizeof(wifi_info.passphr)) == NULL ||
        wifi_info.ssid[0] == '\0' ||
        (strcmp(wifi_credentials.ssid, wifi_info.ssid) == 0 &&
         strcmp(wifi_credentials.password, wifi_info.passphr) == 0)) {
        return;
    }
    memcpy(credentials.ssid, wifi_info.ssid, sizeof(credentials.ssid));
    memcpy(credentials.password, wifi_info.passphr, sizeof(credentials.password));
    if (lfs_kv_set_blob(WIFI_CREDENTIALS_KEY, &credentials,
                        sizeof(credentials)) != LFS_KV_OK) {
        LOG_W("Failed to save Wi-Fi credentials\r\n");
        return;
    }
    wifi_credentials = credentials;
}

static int cmd_psm_set(int argc, char **argv)
{
    EfErrCode error;

    if (argc != 3) {
        printf("Usage: psm_set <key> <value>\r\n");
        return -1;
    }

    error = ef_set_env(argv[1], argv[2]);
    if (error != EF_NO_ERR) {
        printf("psm_set failed: %d\r\n", error);
        return -1;
    }

    printf("Stored %s\r\n", argv[1]);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_psm_set, psm_set, set persistent key value);

#if defined(CONFIG_EDGERTC_STATS) && CONFIG_EDGERTC_STATS
static int cmd_webrtc_cpu(int argc, char **argv)
{
    struct edgertc_stats_snapshot snapshot;
    size_t index;

    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        edgertc_stats_reset();
        printf("webrtc_cpu: reset\r\n");
        return 0;
    }
    if (argc != 1) {
        printf("Usage: webrtc_cpu [reset]\r\n");
        return -1;
    }
    edgertc_stats_snapshot(&snapshot);
    if (snapshot.elapsed_cycles == 0) {
        printf("webrtc_cpu: run webrtc_cpu reset first\r\n");
        return 0;
    }
    printf("webrtc_cpu: interval=%llu cycles\r\n",
           (unsigned long long)snapshot.elapsed_cycles);
    for (index = 0; index < EDGERTC_STAT_COUNT; index++) {
        uint64_t average = snapshot.sample[index].calls == 0 ? 0 :
                           snapshot.sample[index].cycles /
                           snapshot.sample[index].calls;

        printf("  %s cycles=%llu calls=%u avg=%llu\r\n",
               edgertc_stats_name((enum edgertc_stat_id)index),
               (unsigned long long)snapshot.sample[index].cycles,
               (unsigned int)snapshot.sample[index].calls,
               (unsigned long long)average);
    }
    printf("  rx_loop_elapsed includes select wait; use ps_extend for task CPU\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_cpu, webrtc_cpu,
                       show WebRTC module CPU statistics);
#endif

static int cmd_webrtc_server_set(int argc, char **argv)
{
    EfErrCode error;
    bool use_turn;

    if (argc != 4 && argc != 6) {
        printf("Usage: webrtc_server_set <mqtt-url> <token> <ice-url> "
               "[<turn-username> <turn-credential>]\r\n");
        return -1;
    }
    use_turn = strncmp(argv[3], "turn:", strlen("turn:")) == 0;
    if (strncmp(argv[1], "mqtts://", strlen("mqtts://")) != 0 ||
        (!use_turn &&
         strncmp(argv[3], "stun:", strlen("stun:")) != 0) ||
        (use_turn && argc != 6) || (!use_turn && argc != 4) ||
        argv[2][0] == '\0' ||
        strlen(argv[1]) >= SIGNALING_VALUE_SIZE ||
        strlen(argv[2]) >= SIGNALING_VALUE_SIZE ||
        strlen(argv[3]) >= SIGNALING_VALUE_SIZE ||
        (use_turn && (argv[4][0] == '\0' || argv[5][0] == '\0' ||
                      strlen(argv[4]) >= SIGNALING_VALUE_SIZE ||
                      strlen(argv[5]) >= SIGNALING_VALUE_SIZE))) {
        printf("Invalid WebRTC server configuration\r\n");
        return -1;
    }

    error = ef_set_env(SIGNALING_URL_KEY, argv[1]);
    if (error == EF_NO_ERR) {
        error = ef_set_env(SIGNALING_TOKEN_KEY, argv[2]);
    }
    if (error == EF_NO_ERR) {
        error = ef_set_env(ICE_SERVER_URL_KEY, argv[3]);
    }
    if (error == EF_NO_ERR) {
        error = ef_set_env(ICE_USERNAME_KEY, use_turn ? argv[4] : "");
    }
    if (error == EF_NO_ERR) {
        error = ef_set_env(ICE_CREDENTIAL_KEY, use_turn ? argv[5] : "");
    }
    if (error != EF_NO_ERR) {
        printf("Failed to store WebRTC server configuration: %d\r\n", error);
        return -1;
    }

    printf("WebRTC server updated; restarting\r\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    bl_sys_reset_system();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_server_set, webrtc_server_set,
                       set WebRTC server and restart);

static int cmd_webrtc_id(int argc, char **argv)
{
    char device_id[WEBRTC_DEVICE_ID_SIZE];

    (void)argv;
    if (argc != 1) {
        printf("Usage: webrtc_id\r\n");
        return -1;
    }
    if (webrtc == NULL ||
        webrtc_get_device_id(webrtc, device_id, sizeof(device_id)) < 0) {
        printf("Device signaling is not connected\r\n");
        return -1;
    }

    printf("WebRTC device ID: %s\r\n", device_id);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_id, webrtc_id, show WebRTC device ID);

static int cmd_webrtc_call(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: webrtc_call <12-digit-device-id>\r\n");
        return -1;
    }
    if (webrtc == NULL || webrtc_call(webrtc, argv[1]) < 0) {
        printf("Unable to queue WebRTC call\r\n");
        return -1;
    }

    printf("Calling WebRTC device %s\r\n", argv[1]);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_call, webrtc_call, call WebRTC device);

static int cmd_webrtc_hangup(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: webrtc_hangup\r\n");
        return -1;
    }
    if (webrtc == NULL || webrtc_hangup(webrtc) < 0) {
        printf("Unable to queue WebRTC hangup\r\n");
        return -1;
    }

    printf("Ending WebRTC device call\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_hangup, webrtc_hangup,
                       end WebRTC device call);

#ifdef CONFIG_XWEBSOCKET
static int cmd_webrtc_audio_ws(int argc, char **argv)
{
    char before_url[SIGNALING_VALUE_SIZE];
    char after_url[SIGNALING_VALUE_SIZE];

    if (argc != 5) {
        printf("Usage: webrtc_audio_ws <host> <before-port> <after-port> <name>\r\n");
        return -1;
    }
    if (webrtc == NULL) {
        printf("WebRTC is not running\r\n");
        return -1;
    }
    snprintf(before_url, sizeof(before_url), "ws://%s:%s/%s_before.pcm",
             argv[1], argv[2], argv[4]);
    snprintf(after_url, sizeof(after_url), "ws://%s:%s/%s_after.pcm",
             argv[1], argv[3], argv[4]);
    if (webrtc_audio_websocket_register(webrtc, before_url, after_url) < 0) {
        printf("Unable to register AEC audio websockets\r\n");
        return -1;
    }
    printf("AEC audio websockets registered: %s, %s\r\n",
           before_url, after_url);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_webrtc_audio_ws, webrtc_audio_ws,
                       dump AEC audio to websockets);
#endif

static void wifi_event_handler(async_input_event_t event, void *private_data)
{
    webrtc_handle_t current_webrtc;

    (void)private_data;
    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            wifi_mgmr_task_start();
            break;
        case CODE_WIFI_ON_MGMR_DONE:
            LOG_I("Wi-Fi manager ready\r\n");
            if (wifi_credentials.ssid[0] != '\0') {
                wifi_sta_connect(wifi_credentials.ssid,
                                 wifi_credentials.password[0] != '\0' ?
                                     wifi_credentials.password : NULL,
                                 NULL, NULL, 0, 0, 0, 1);
            }
            break;
        case CODE_WIFI_ON_CONNECTED:
            LOG_I("Wi-Fi connected\r\n");
            break;
        case CODE_WIFI_ON_GOT_IP:
            wifi_state = 1;
            if (xTimerPendFunctionCall(save_wifi_credentials, NULL, 0, 0) != pdPASS) {
                LOG_W("Unable to queue Wi-Fi credential save\r\n");
            }
            current_webrtc = webrtc;
            if (current_webrtc != NULL) {
                webrtc_set_network_ready(current_webrtc, true);
            }
            LOG_I("Wi-Fi got IP\r\n");
            break;
        case CODE_WIFI_ON_DISCONNECT:
            wifi_state = 0;
            current_webrtc = webrtc;
            if (current_webrtc != NULL) {
                webrtc_set_network_ready(current_webrtc, false);
            }
            LOG_I("Wi-Fi disconnected\r\n");
            break;
        default:
            break;
    }
}

static void wifi_start_task(void *argument)
{
    (void)argument;
    read_wifi_credentials();
    tcpip_init(NULL, NULL);
    async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);
    fhost_init();
    vTaskDelete(NULL);
}

static void read_signaling_config(char *url, char *token,
                                  char *ice_server_url, char *ice_username,
                                  char *ice_credential)
{
    while (1) {
        memset(url, 0, SIGNALING_VALUE_SIZE);
        ef_get_env_blob(SIGNALING_URL_KEY, url,
                        SIGNALING_VALUE_SIZE - 1, NULL);
        if (url[0] != '\0') {
            break;
        }
        LOG_W("Set the WebRTC server with webrtc_server_set\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    memset(token, 0, SIGNALING_VALUE_SIZE);
    ef_get_env_blob(SIGNALING_TOKEN_KEY, token,
                    SIGNALING_VALUE_SIZE - 1, NULL);

    while (1) {
        memset(ice_server_url, 0, SIGNALING_VALUE_SIZE);
        ef_get_env_blob(ICE_SERVER_URL_KEY, ice_server_url,
                        SIGNALING_VALUE_SIZE - 1, NULL);
        if (ice_server_url[0] != '\0') {
            break;
        }
        LOG_W("Set the WebRTC server with webrtc_server_set\r\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    memset(ice_username, 0, SIGNALING_VALUE_SIZE);
    ef_get_env_blob(ICE_USERNAME_KEY, ice_username,
                    SIGNALING_VALUE_SIZE - 1, NULL);
    memset(ice_credential, 0, SIGNALING_VALUE_SIZE);
    ef_get_env_blob(ICE_CREDENTIAL_KEY, ice_credential,
                    SIGNALING_VALUE_SIZE - 1, NULL);
}

static void system_init_task(void *argument)
{
    webrtc_cfg_t config;
    webrtc_handle_t failed_webrtc;
    struct bflb_device_s *uart;
    char signaling_url[SIGNALING_VALUE_SIZE];
    char signaling_token[SIGNALING_VALUE_SIZE];
    char ice_server_url[SIGNALING_VALUE_SIZE];
    char ice_server_username[SIGNALING_VALUE_SIZE];
    char ice_server_credential[SIGNALING_VALUE_SIZE];

    (void)argument;
    uart = bflb_device_get_by_name("uart0");
    shell_init_with_task(uart);
    while (wifi_state == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    snd_card_register(NULL);
    msp_i2s_port_init();

    read_signaling_config(signaling_url, signaling_token, ice_server_url,
                          ice_server_username, ice_server_credential);

    memset(&config, 0, sizeof(config));
    config.signaling_url = signaling_url;
    config.signaling_token = signaling_token;
    config.ice_server_url = ice_server_url;
    config.ice_server_username = ice_server_username;
    config.ice_server_credential = ice_server_credential;
    config.audio.enabled = true;
    config.audio.sample_rate = 8000;
    config.audio.frame_duration_ms = 20;
    config.audio.channels = 2;
    config.audio.bits_per_sample = 16;
    config.video.enabled = true;
    config.video.device_name = "/dev/video/dvp0";
    config.video.pixel_format = V4L2_PIX_FMT_MJPEG;
    config.video.width = 640;
    config.video.height = 480;
    config.video.max_frame_size = 48U * 1024U;
    config.video.frame_rate = 20;
    config.video.buffer_count = 2;
    config.video.task_priority = 4;
    config.video.controls = video_controls;
    config.video.control_count =
        sizeof(video_controls) / sizeof(video_controls[0]);
#ifdef CONFIG_WEBRTC_VIDEO_RX
    config.video_receive_enabled = true;
#endif

    if (webrtc_open(&config, &webrtc) < 0) {
        LOG_E("Failed to open WebRTC\r\n");
        vTaskDelete(NULL);
        return;
    }
    webrtc_set_network_ready(webrtc, wifi_state != 0);
    if (webrtc_start(webrtc) < 0) {
        LOG_E("Failed to start WebRTC\r\n");
        failed_webrtc = webrtc;
        webrtc = NULL;
        webrtc_close(failed_webrtc);
        vTaskDelete(NULL);
        return;
    }

    LOG_I("PCMA two-way audio and DVP MJPEG video enabled\r\n");
    vTaskDelete(NULL);
}

int main(void)
{
    struct bflb_device_s *rtc;
#ifdef BL618DG
    const uint32_t alloc_order[] = { MM_HEAP_PSRAM_0, MM_HEAP_OCRAM_0, MM_HEAP_WRAM_0 };
#endif

    board_init();
#ifdef BL618DG
    if (mm_heap_set_any_alloc_order(alloc_order, sizeof(alloc_order) / sizeof(alloc_order[0])) != 0) {
        LOG_E("Failed to configure heap allocation order\r\n");
        return -1;
    }
#endif
    rtc = bflb_device_get_by_name("rtc");
    bflb_rtc_set_time(rtc, 0);
    bflb_mtd_init();
    easyflash_init();

    /* Prepare the MAC task and RF configuration before either task can run. */
    wifi_task_create();
    if (rfparam_init(0, NULL, 0) != 0) {
        LOG_E("PHY RF init failed\r\n");
        return -1;
    }

#ifdef CONFIG_BSP_LCD
    app_ui_init();
#endif

    xTaskCreate(system_init_task, "system_init", 2048, NULL,
                configMAX_PRIORITIES - 2, NULL);
    if (xTaskCreate(wifi_start_task, "wifi_init", 1024, NULL,
                    10, NULL) != pdPASS) {
        LOG_E("Failed to create Wi-Fi init task\r\n");
        return -1;
    }
    vTaskStartScheduler();

    LOG_E("Scheduler start failed\r\n");
    return -1;
}
