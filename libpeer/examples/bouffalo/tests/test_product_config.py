#!/usr/bin/env python3
"""Exercise the product's actual Wi-Fi credential configuration functions."""
import pathlib
import subprocess
import tempfile

source = (pathlib.Path(__file__).parent / "../app/main.c").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


start = source.index("#define WIFI_SSID_KEY")
declarations = source[start:source.index("\nstatic webrtc_handle_t", start)]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MGMR_SSID_LEN 32
#define MGMR_KEY_LEN 64
#define LFS_KV_OK 0
#define LOG_W(...) ((void)0)
'''
harness += declarations
harness += r'''
static wifi_credentials_t wifi_credentials;
typedef struct { char ssid[33], passphr[65]; } wifi_mgmr_connect_ind_stat_info_t;
static wifi_mgmr_connect_ind_stat_info_t connection;
static int connection_result, writes, write_error;
static bool short_read;
static struct { const char *key; unsigned char value[512]; size_t size; } settings[3] = {
    { .key = WIFI_CREDENTIALS_KEY }, { .key = WIFI_SSID_KEY },
    { .key = WIFI_PASSWORD_KEY },
};
static size_t lfs_kv_get_blob(const char *key, void *value, size_t capacity, size_t *size) {
    for (size_t i = 0; i < 3; i++) if (!strcmp(key, settings[i].key)) {
        *size = settings[i].size;
        size_t copied = *size < capacity ? *size : capacity;
        if (short_read && copied) copied--;
        memcpy(value, settings[i].value, copied);
        return copied;
    }
    assert(false); return 0;
}
static int lfs_kv_set_blob(const char *key, const void *value, size_t size) {
    writes++;
    if (write_error) return -1;
    for (size_t i = 0; i < 3; i++) if (!strcmp(key, settings[i].key)) {
        assert(size <= sizeof(settings[i].value));
        memcpy(settings[i].value, value, size); settings[i].size = size;
        return LFS_KV_OK;
    }
    assert(false); return -1;
}
static int wifi_mgmr_sta_connect_ind_stat_get(wifi_mgmr_connect_ind_stat_info_t *info) {
    *info = connection; return connection_result;
}
'''
for signature in ("static bool read_config_string(",
                  "static void read_wifi_credentials(",
                  "static void save_wifi_credentials("):
    harness += function(signature)
harness += r'''
int main(void) {
    /* Legacy keys restore, malformed/truncated records cannot become credentials. */
    lfs_kv_set_blob(WIFI_SSID_KEY, "test-network", sizeof("test-network"));
    lfs_kv_set_blob(WIFI_PASSWORD_KEY, "test-password", sizeof("test-password"));
    read_wifi_credentials();
    assert(!strcmp(wifi_credentials.ssid, "test-network"));
    assert(!strcmp(wifi_credentials.password, "test-password"));
    settings[1].size = 34; memset(settings[1].value, 'x', 34);
    read_wifi_credentials(); assert(!wifi_credentials.ssid[0]);
    settings[1].size = settings[2].size = 0;
    memset(settings[0].value, 'x', sizeof(wifi_credentials));
    settings[0].size = sizeof(wifi_credentials);
    read_wifi_credentials(); assert(!wifi_credentials.ssid[0]);

    /* The maximum-sized successful connection is one write; repeats do not wear flash. */
    memset(connection.ssid, 's', sizeof(connection.ssid));
    memset(connection.passphr, 'p', sizeof(connection.passphr));
    writes = 0;
    save_wifi_credentials(NULL, 0); assert(writes == 0);
    connection.ssid[32] = '\0';
    save_wifi_credentials(NULL, 0); assert(writes == 0);
    connection.passphr[64] = '\0';
    memset(connection.ssid, 's', 32); memset(connection.passphr, 'p', 64);
    writes = 0;
    save_wifi_credentials(NULL, 0); assert(writes == 1);
    assert(strlen(wifi_credentials.ssid) == 32 && strlen(wifi_credentials.password) == 64);
    memset(&wifi_credentials, 0, sizeof(wifi_credentials));
    read_wifi_credentials(); assert(strlen(wifi_credentials.ssid) == 32);
    save_wifi_credentials(NULL, 0); assert(writes == 1);
    short_read = true;
    read_wifi_credentials(); assert(!wifi_credentials.ssid[0]);
    short_read = false;
    read_wifi_credentials();
    strcpy(connection.ssid, "new-network");
    connection_result = -1;
    save_wifi_credentials(NULL, 0); assert(writes == 1);
    connection_result = 0; write_error = 1;
    save_wifi_credentials(NULL, 0); assert(writes == 2);
    assert(strlen(wifi_credentials.ssid) == 32);
    write_error = 0;
    save_wifi_credentials(NULL, 0); assert(writes == 3);
    assert(!strcmp(wifi_credentials.ssid, "new-network"));
    connection.passphr[0] = '\0';
    save_wifi_credentials(NULL, 0);
    read_wifi_credentials(); assert(!wifi_credentials.password[0]);

    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webrtc-product-config-") as temp:
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-", "-o", str(binary)],
                   input=harness, text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("product Wi-Fi configuration: PASS")
