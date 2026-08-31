#ifndef EDGERTC_STATS_H
#define EDGERTC_STATS_H

#include <stddef.h>
#include <stdint.h>

enum edgertc_stat_id {
    EDGERTC_STAT_CAPTURE_PIPELINE,
    EDGERTC_STAT_PLAYBACK_PIPELINE,
    EDGERTC_STAT_TX_AUDIO,
    EDGERTC_STAT_TX_VIDEO,
    EDGERTC_STAT_RX_LOOP_ELAPSED,
    EDGERTC_STAT_RTP_SEND,
    EDGERTC_STAT_RTP_AUDIO_DECODE,
    EDGERTC_STAT_RTP_VIDEO_DECODE,
    EDGERTC_STAT_SRTP_ENCRYPT,
    EDGERTC_STAT_SRTP_DECRYPT,
    EDGERTC_STAT_DTLS_WRITE,
    EDGERTC_STAT_DTLS_READ,
    EDGERTC_STAT_SCTP_SEND,
    EDGERTC_STAT_SCTP_RECV,
    EDGERTC_STAT_UDP_SEND,
    EDGERTC_STAT_UDP_RECV,
    EDGERTC_STAT_DTLS_RECORD_ENCRYPT,
    EDGERTC_STAT_DTLS_RECORD_DECRYPT,
    EDGERTC_STAT_DTLS_AEAD_ENCRYPT,
    EDGERTC_STAT_DTLS_AEAD_DECRYPT,
    EDGERTC_STAT_DTLS_CIPHER_ENCRYPT,
    EDGERTC_STAT_DTLS_CIPHER_DECRYPT,
    EDGERTC_STAT_DTLS_HMAC_ENCRYPT,
    EDGERTC_STAT_DTLS_HMAC_DECRYPT,
    EDGERTC_STAT_DTLS_SEND_CALLBACK,
    EDGERTC_STAT_SRTP_CIPHER_ENCRYPT,
    EDGERTC_STAT_SRTP_CIPHER_DECRYPT,
    EDGERTC_STAT_SRTP_HMAC_ENCRYPT,
    EDGERTC_STAT_SRTP_HMAC_DECRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA1_ENCRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA1_DECRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA256_ENCRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA256_DECRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA384_ENCRYPT,
    EDGERTC_STAT_DTLS_HMAC_SHA384_DECRYPT,
    EDGERTC_STAT_DTLS_HMAC_OTHER_ENCRYPT,
    EDGERTC_STAT_DTLS_HMAC_OTHER_DECRYPT,
    EDGERTC_STAT_DTLS_CT_HMAC,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_SETUP,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_UPDATE,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_CLONE,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_FINISH,
    EDGERTC_STAT_DTLS_CT_HMAC_SELECT,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_STARTS,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_HMAC_RESET,
    EDGERTC_STAT_DTLS_CT_HMAC_MD_FREE,
    EDGERTC_STAT_DTLS_HMAC_ENCRYPT_MD_UPDATE,
    EDGERTC_STAT_DTLS_HMAC_ENCRYPT_MD_FINISH,
    EDGERTC_STAT_DTLS_HMAC_ENCRYPT_MD_RESET,
    EDGERTC_STAT_MBEDTLS_AES_CBC_ENCRYPT,
    EDGERTC_STAT_MBEDTLS_AES_CBC_DECRYPT,
    EDGERTC_STAT_BFLB_SEC_AES_MUTEX_TAKE,
    EDGERTC_STAT_BFLB_AES_LINK_CRYPT_CBC_CHUNK,
    EDGERTC_STAT_BFLB_AES_LINK_UPDATE_CBC,
    EDGERTC_STAT_AGENT_SEND_SOCKET,
    EDGERTC_STAT_AGENT_SEND_TURN,
    EDGERTC_STAT_MBEDTLS_GCM_HASH_BLOCKS,
    EDGERTC_STAT_BFLB_GMAC_LINK_WORK,
    EDGERTC_STAT_MBEDTLS_GCM_AES_CTR,
    EDGERTC_STAT_MEDIA_AUDIO_CAPTURE_READ,
    EDGERTC_STAT_MEDIA_VIDEO_CAPTURE_READ,
    EDGERTC_STAT_MEDIA_VIDEO_CAPTURE_RELEASE,
    EDGERTC_STAT_COUNT,
};

struct edgertc_stat_sample {
    /* Samples may be nested; do not sum their cycle counts. */
    uint64_t cycles;
    uint32_t calls;
};

struct edgertc_stats_snapshot {
    uint64_t elapsed_cycles;
    struct edgertc_stat_sample sample[EDGERTC_STAT_COUNT];
};

#if defined(CONFIG_EDGERTC_STATS) && CONFIG_EDGERTC_STATS
uint64_t edgertc_stats_now(void);
void edgertc_stats_add(enum edgertc_stat_id id, uint64_t cycles);
void edgertc_stats_add_count(enum edgertc_stat_id id, uint64_t cycles,
                             uint32_t calls);
void edgertc_stats_reset(void);
void edgertc_stats_snapshot(struct edgertc_stats_snapshot *snapshot);
const char *edgertc_stats_name(enum edgertc_stat_id id);

#define EDGERTC_STATS_BEGIN(name) uint64_t name = edgertc_stats_now()
#define EDGERTC_STATS_END(id, name) \
    edgertc_stats_add((id), edgertc_stats_now() - (name))
#define EDGERTC_STATS_END_PAIR(first_id, second_id, name) do { \
    uint64_t name##_elapsed = edgertc_stats_now() - (name); \
    edgertc_stats_add((first_id), name##_elapsed); \
    edgertc_stats_add((second_id), name##_elapsed); \
} while (0)
#define EDGERTC_STATS_ACCUM_DECLARE(name) \
    uint64_t name = 0; \
    uint32_t name##_calls = 0
#define EDGERTC_STATS_ACCUM_BEGIN(name) uint64_t name = edgertc_stats_now()
#define EDGERTC_STATS_ACCUM_END(total, name) \
    do { \
        (total) += edgertc_stats_now() - (name); \
        (total##_calls)++; \
    } while (0)
#define EDGERTC_STATS_ACCUM_PUBLISH(id, total) \
    edgertc_stats_add_count((id), (total), (total##_calls))
#else
static inline uint64_t edgertc_stats_now(void)
{
    return 0;
}

static inline void edgertc_stats_add(enum edgertc_stat_id id,
                                     uint64_t cycles)
{
    (void)id;
    (void)cycles;
}

static inline void edgertc_stats_add_count(enum edgertc_stat_id id,
                                           uint64_t cycles, uint32_t calls)
{
    (void)id;
    (void)cycles;
    (void)calls;
}

static inline void edgertc_stats_reset(void)
{
}

static inline void edgertc_stats_snapshot(struct edgertc_stats_snapshot *snapshot)
{
    (void)snapshot;
}

static inline const char *edgertc_stats_name(enum edgertc_stat_id id)
{
    (void)id;
    return NULL;
}

#define EDGERTC_STATS_BEGIN(name) do { } while (0)
#define EDGERTC_STATS_END(id, name) do { } while (0)
#define EDGERTC_STATS_END_PAIR(first_id, second_id, name) do { } while (0)
#define EDGERTC_STATS_ACCUM_DECLARE(name) do { } while (0)
#define EDGERTC_STATS_ACCUM_BEGIN(name) do { } while (0)
#define EDGERTC_STATS_ACCUM_END(total, name) do { } while (0)
#define EDGERTC_STATS_ACCUM_PUBLISH(id, total) do { } while (0)
#endif

#endif
