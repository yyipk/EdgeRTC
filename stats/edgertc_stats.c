#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#include "edgertc_stats.h"

static struct edgertc_stat_sample samples[EDGERTC_STAT_COUNT];
static uint64_t stats_start;
static const char *const stat_names[EDGERTC_STAT_COUNT] = {
    "capture_pipeline",
    "playback_pipeline",
    "peer_connection_send_audio",
    "peer_connection_datachannel_send_inplace",
    "peer_connection_loop_elapsed",
    "rtp_encoder_encode",
    "rtp_decoder_decode_audio",
    "rtp_decoder_decode_video",
    "srtp_protect",
    "srtp_unprotect",
    "mbedtls_ssl_write",
    "mbedtls_ssl_read",
    "sctp_outgoing_data",
    "sctp_incoming_data",
    "sendto",
    "recvfrom",
    "mbedtls_ssl_encrypt_buf",
    "mbedtls_ssl_decrypt_buf",
    "mbedtls_cipher_auth_encrypt_ext",
    "mbedtls_cipher_auth_decrypt_ext",
    "mbedtls_cipher_crypt(encrypt)",
    "mbedtls_cipher_crypt(decrypt)",
    "mbedtls_md_hmac_*(encrypt,total)",
    "mbedtls_md_hmac_*(decrypt,total)",
    "peer_connection_dtls_srtp_send",
    "srtp_cipher_encrypt",
    "srtp_cipher_decrypt",
    "srtp_hmac_encrypt",
    "srtp_hmac_decrypt",
    "mbedtls_md_hmac_*(SHA-1,encrypt)",
    "mbedtls_md_hmac_*(SHA-1,decrypt)",
    "mbedtls_md_hmac_*(SHA-256,encrypt)",
    "mbedtls_md_hmac_*(SHA-256,decrypt)",
    "mbedtls_md_hmac_*(SHA-384,encrypt)",
    "mbedtls_md_hmac_*(SHA-384,decrypt)",
    "mbedtls_md_hmac_*(other,encrypt)",
    "mbedtls_md_hmac_*(other,decrypt)",
    "mbedtls_ct_hmac",
    "mbedtls_ct_hmac:mbedtls_md_setup",
    "mbedtls_ct_hmac:mbedtls_md_update",
    "mbedtls_ct_hmac:mbedtls_md_clone",
    "mbedtls_ct_hmac:mbedtls_md_finish",
    "mbedtls_ct_hmac:mbedtls_ct_memcpy_if_eq",
    "mbedtls_ct_hmac:mbedtls_md_starts",
    "mbedtls_ct_hmac:mbedtls_md_hmac_reset",
    "mbedtls_ct_hmac:mbedtls_md_free",
    "mbedtls_md_hmac_update(encrypt)",
    "mbedtls_md_hmac_finish(encrypt)",
    "mbedtls_md_hmac_reset(encrypt)",
    "mbedtls_aes_crypt_cbc_encrypt",
    "mbedtls_aes_crypt_cbc_decrypt",
    "bflb_sec_aes_mutex_take",
    "bflb_aes_link_crypt_cbc_chunk",
    "bflb_aes_link_update(CBC)",
    "agent_send->agent_socket_send",
    "agent_send->agent_turn_send",
    "mbedtls_gcm_hash_blocks",
    "bflb_sec_gmac_link_work",
    "mbedtls_gcm_aes_ctr",
    "media_audio_capture_read",
    "media_video_capture_read",
    "media_video_capture_release",
};

uint64_t edgertc_stats_now(void)
{
    return (uint64_t)portGET_RUN_TIME_COUNTER_VALUE();
}

void edgertc_stats_add(enum edgertc_stat_id id, uint64_t cycles)
{
    edgertc_stats_add_count(id, cycles, 1);
}

void edgertc_stats_add_count(enum edgertc_stat_id id, uint64_t cycles,
                             uint32_t calls)
{
    if ((unsigned int)id >= EDGERTC_STAT_COUNT) {
        return;
    }
    taskENTER_CRITICAL();
    samples[id].cycles += cycles;
    samples[id].calls += calls;
    taskEXIT_CRITICAL();
}

void edgertc_stats_reset(void)
{
    size_t index;

    taskENTER_CRITICAL();
    for (index = 0; index < EDGERTC_STAT_COUNT; index++) {
        samples[index].cycles = 0;
        samples[index].calls = 0;
    }
    stats_start = edgertc_stats_now();
    taskEXIT_CRITICAL();
}

void edgertc_stats_snapshot(struct edgertc_stats_snapshot *snapshot)
{
    size_t index;

    if (snapshot == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    snapshot->elapsed_cycles = edgertc_stats_now() - stats_start;
    for (index = 0; index < EDGERTC_STAT_COUNT; index++) {
        snapshot->sample[index] = samples[index];
    }
    taskEXIT_CRITICAL();
}

const char *edgertc_stats_name(enum edgertc_stat_id id)
{
    if ((unsigned int)id >= EDGERTC_STAT_COUNT) {
        return NULL;
    }
    return stat_names[id];
}
