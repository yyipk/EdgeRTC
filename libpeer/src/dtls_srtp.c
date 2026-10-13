#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "address.h"
#include "config.h"
#include "dtls_srtp.h"
#include "edgertc_stats.h"
#if CONFIG_MBEDTLS_DEBUG
#include "mbedtls/debug.h"
#endif
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "ports.h"
#include "socket.h"
#include "utils.h"

#define DTLS_RECORD_HEADER_SIZE 13
#define DTLS_HANDSHAKE_HEADER_SIZE 12
#define DTLS_CLIENT_HELLO_HEADER_SIZE (DTLS_RECORD_HEADER_SIZE + DTLS_HANDSHAKE_HEADER_SIZE)
#define DTLS_CONTENT_TYPE_HANDSHAKE 22
#define DTLS_HANDSHAKE_TYPE_CLIENT_HELLO 1

static const int dtls_srtp_gcm_ciphersuites[] = {
#if CONFIG_DTLS_USE_ECDSA
  MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
#else
  MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
#endif
  0,
};

typedef struct DtlsClientHelloFragment {
  uint16_t message_seq;
  size_t message_length;
  size_t fragment_offset;
  size_t fragment_length;
  const uint8_t* data;
} DtlsClientHelloFragment;

static size_t dtls_srtp_read_uint24(const uint8_t* data) {
  return ((size_t)data[0] << 16) | ((size_t)data[1] << 8) | data[2];
}

static void dtls_srtp_write_uint24(uint8_t* data, size_t value) {
  data[0] = (uint8_t)(value >> 16);
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)value;
}

static int dtls_srtp_parse_client_hello_fragment(const uint8_t* packet,
                                                 size_t packet_length,
                                                 DtlsClientHelloFragment* fragment) {
  const uint8_t* handshake;
  size_t record_length;

  if (packet_length < DTLS_CLIENT_HELLO_HEADER_SIZE ||
      packet[0] != DTLS_CONTENT_TYPE_HANDSHAKE ||
      packet[3] != 0 || packet[4] != 0) {
    return 0;
  }

  record_length = ((size_t)packet[11] << 8) | packet[12];
  if (record_length < DTLS_HANDSHAKE_HEADER_SIZE ||
      record_length + DTLS_RECORD_HEADER_SIZE > packet_length) {
    return 0;
  }

  handshake = packet + DTLS_RECORD_HEADER_SIZE;
  if (handshake[0] != DTLS_HANDSHAKE_TYPE_CLIENT_HELLO) {
    return 0;
  }

  fragment->message_seq = ((uint16_t)handshake[4] << 8) | handshake[5];
  fragment->message_length = dtls_srtp_read_uint24(handshake + 1);
  fragment->fragment_offset = dtls_srtp_read_uint24(handshake + 6);
  fragment->fragment_length = dtls_srtp_read_uint24(handshake + 9);
  fragment->data = handshake + DTLS_HANDSHAKE_HEADER_SIZE;

  if (record_length != DTLS_HANDSHAKE_HEADER_SIZE + fragment->fragment_length ||
      fragment->fragment_offset > fragment->message_length ||
      fragment->fragment_length > fragment->message_length - fragment->fragment_offset) {
    return 0;
  }

  return 1;
}

static size_t dtls_srtp_copy_client_hello_fragment(uint8_t* message,
                                                   uint8_t* received_map,
                                                   const DtlsClientHelloFragment* fragment) {
  size_t newly_received = 0;
  size_t i;

  for (i = 0; i < fragment->fragment_length; i++) {
    size_t offset = fragment->fragment_offset + i;
    uint8_t mask = (uint8_t)(1U << (offset & 7U));

    message[offset] = fragment->data[i];
    if ((received_map[offset >> 3] & mask) == 0) {
      received_map[offset >> 3] |= mask;
      newly_received++;
    }
  }

  return newly_received;
}

static int dtls_srtp_mbedtls_recv(void* ctx, uint8_t* buf, size_t len) {
  DtlsSrtp* dtls_srtp = (DtlsSrtp*)ctx;
  DtlsClientHelloFragment fragment;
  uint8_t* reassembled = NULL;
  uint8_t* received_map = NULL;
  size_t reassembled_length;
  size_t received = 0;
  unsigned int fragment_count = 0;
  int have_zero_offset_fragment;
  int ret;

  ret = dtls_srtp->udp_recv(ctx, buf, len);
  if (ret <= 0 || dtls_srtp->role != DTLS_SRTP_ROLE_SERVER ||
      !dtls_srtp_parse_client_hello_fragment(buf, (size_t)ret, &fragment) ||
      (fragment.fragment_offset == 0 && fragment.fragment_length == fragment.message_length)) {
    return ret;
  }

  if (fragment.message_length > len - DTLS_CLIENT_HELLO_HEADER_SIZE) {
    LOGE("Fragmented DTLS ClientHello is too large: %u", (unsigned int)fragment.message_length);
    return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
  }

  reassembled_length = DTLS_CLIENT_HELLO_HEADER_SIZE + fragment.message_length;
  reassembled = calloc(1, reassembled_length);
  received_map = calloc(1, (fragment.message_length + 7) / 8);
  if (reassembled == NULL || received_map == NULL) {
    free(received_map);
    free(reassembled);
    return MBEDTLS_ERR_SSL_ALLOC_FAILED;
  }

  memcpy(reassembled, buf, DTLS_CLIENT_HELLO_HEADER_SIZE);
  have_zero_offset_fragment = fragment.fragment_offset == 0;
  LOGI("Reassembling fragmented DTLS ClientHello: seq=%u, total=%u",
       fragment.message_seq, (unsigned int)fragment.message_length);

  while (1) {
    received += dtls_srtp_copy_client_hello_fragment(
        reassembled + DTLS_CLIENT_HELLO_HEADER_SIZE, received_map, &fragment);
    fragment_count++;
    if (received == fragment.message_length) {
      break;
    }

    ret = dtls_srtp->udp_recv(ctx, buf, len);
    if (ret <= 0) {
      goto exit;
    }

    DtlsClientHelloFragment next_fragment;
    if (!dtls_srtp_parse_client_hello_fragment(buf, (size_t)ret, &next_fragment) ||
        next_fragment.message_seq != fragment.message_seq ||
        next_fragment.message_length != fragment.message_length) {
      LOGE("Fragmented DTLS ClientHello was interrupted");
      goto exit;
    }
    if (!have_zero_offset_fragment && next_fragment.fragment_offset == 0) {
      memcpy(reassembled, buf, DTLS_CLIENT_HELLO_HEADER_SIZE);
      have_zero_offset_fragment = 1;
    }
    fragment = next_fragment;
  }

  reassembled[11] = (uint8_t)((DTLS_HANDSHAKE_HEADER_SIZE + fragment.message_length) >> 8);
  reassembled[12] = (uint8_t)(DTLS_HANDSHAKE_HEADER_SIZE + fragment.message_length);
  memset(reassembled + DTLS_RECORD_HEADER_SIZE + 6, 0, 3);
  dtls_srtp_write_uint24(reassembled + DTLS_RECORD_HEADER_SIZE + 9,
                         fragment.message_length);
  memcpy(buf, reassembled, reassembled_length);
  ret = (int)reassembled_length;
  LOGI("Reassembled DTLS ClientHello from %u fragments", fragment_count);

exit:
  free(received_map);
  free(reassembled);
  return ret;
}

int dtls_srtp_udp_send(void* ctx, const uint8_t* buf, size_t len) {
  DtlsSrtp* dtls_srtp = (DtlsSrtp*)ctx;
  UdpSocket* udp_socket = (UdpSocket*)dtls_srtp->user_data;

  int ret = udp_socket_sendto(udp_socket, dtls_srtp->remote_addr, buf, len);

  LOGD("dtls_srtp_udp_send (%d)", ret);

  return ret;
}

int dtls_srtp_udp_recv(void* ctx, uint8_t* buf, size_t len) {
  DtlsSrtp* dtls_srtp = (DtlsSrtp*)ctx;
  UdpSocket* udp_socket = (UdpSocket*)dtls_srtp->user_data;

  int ret;

  while ((ret = udp_socket_recvfrom(udp_socket, &udp_socket->bind_addr, buf, len)) <= 0) {
    ports_sleep_ms(1);
  }

  LOGD("dtls_srtp_udp_recv (%d)", ret);

  return ret;
}

static void dtls_srtp_x509_digest(const mbedtls_x509_crt* crt, char* buf) {
  int i;
  unsigned char digest[32];

  mbedtls_sha256_context sha256_ctx;
  mbedtls_sha256_init(&sha256_ctx);
  mbedtls_sha256_starts(&sha256_ctx, 0);
  mbedtls_sha256_update(&sha256_ctx, crt->raw.p, crt->raw.len);
  mbedtls_sha256_finish(&sha256_ctx, (unsigned char*)digest);
  mbedtls_sha256_free(&sha256_ctx);

  for (i = 0; i < 32; i++) {
    snprintf(buf, 4, "%.2X:", digest[i]);
    buf += 3;
  }

  *(--buf) = '\0';
}

// Do not verify CA
static int dtls_srtp_cert_verify(void* data, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
  *flags &= ~(MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_CN_MISMATCH | MBEDTLS_X509_BADCERT_BAD_KEY);
  return 0;
}

static int dtls_srtp_selfsign_cert(DtlsSrtp* dtls_srtp) {
  int ret;

  mbedtls_x509write_cert crt;

  unsigned char* cert_buf = NULL;
#if CONFIG_MBEDTLS_2_X
  mbedtls_mpi serial;
#else
  const char* serial = "peer";
#endif
  const char* pers = "dtls_srtp";

  cert_buf = (unsigned char*)malloc(RSA_KEY_LENGTH * 2);
  if (cert_buf == NULL) {
    LOGE("malloc failed");
    return -1;
  }

  mbedtls_ctr_drbg_seed(&dtls_srtp->ctr_drbg, mbedtls_entropy_func, &dtls_srtp->entropy, (const unsigned char*)pers, strlen(pers));

#if CONFIG_DTLS_USE_ECDSA
  mbedtls_pk_setup(&dtls_srtp->pkey, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(dtls_srtp->pkey), mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
#else
  mbedtls_pk_setup(&dtls_srtp->pkey, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
  mbedtls_rsa_gen_key(mbedtls_pk_rsa(dtls_srtp->pkey), mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg, RSA_KEY_LENGTH, 65537);
#endif

  mbedtls_x509write_crt_init(&crt);

  mbedtls_x509write_crt_set_subject_key(&crt, &dtls_srtp->pkey);

  mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

  mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

  mbedtls_x509write_crt_set_subject_key(&crt, &dtls_srtp->pkey);

  mbedtls_x509write_crt_set_issuer_key(&crt, &dtls_srtp->pkey);

  mbedtls_x509write_crt_set_subject_name(&crt, "CN=dtls_srtp");

  mbedtls_x509write_crt_set_issuer_name(&crt, "CN=dtls_srtp");

#if CONFIG_MBEDTLS_2_X
  mbedtls_mpi_init(&serial);
  mbedtls_mpi_fill_random(&serial, 16, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);
  ret = mbedtls_x509write_crt_set_serial(&crt, &serial);
  if (ret < 0) {
    LOGE("mbedtls_x509write_crt_set_serial failed -0x%.4x", (unsigned int)-ret);
  }
#else
  mbedtls_x509write_crt_set_serial_raw(&crt, (unsigned char*)serial, strlen(serial));
#endif

  mbedtls_x509write_crt_set_validity(&crt, "20180101000000", "20280101000000");

  ret = mbedtls_x509write_crt_pem(&crt, cert_buf, 2 * RSA_KEY_LENGTH, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);

  if (ret < 0) {
    LOGE("mbedtls_x509write_crt_pem failed -0x%.4x", (unsigned int)-ret);
  }

  mbedtls_x509_crt_parse(&dtls_srtp->cert, cert_buf, 2 * RSA_KEY_LENGTH);

  mbedtls_x509write_crt_free(&crt);

  free(cert_buf);

  return ret;
}

#if CONFIG_MBEDTLS_DEBUG
static void dtls_srtp_debug(void* ctx, int level, const char* file, int line, const char* str) {
  LOGD("%s:%04d: %s", file, line, str);
}
#endif

int dtls_srtp_init(DtlsSrtp* dtls_srtp, DtlsSrtpRole role, void* user_data) {
  static const mbedtls_ssl_srtp_profile default_profiles[] = {
      MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
      //MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
      //MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_80,
      //MBEDTLS_TLS_SRTP_NULL_HMAC_SHA1_32,
      MBEDTLS_TLS_SRTP_UNSET};

  memset(dtls_srtp, 0, sizeof(*dtls_srtp));

  dtls_srtp->role = role;
  dtls_srtp->state = DTLS_SRTP_STATE_INIT;
  dtls_srtp->user_data = user_data;
  dtls_srtp->udp_send = dtls_srtp_udp_send;
  dtls_srtp->udp_recv = dtls_srtp_udp_recv;

  mbedtls_ssl_config_init(&dtls_srtp->conf);
  mbedtls_ssl_init(&dtls_srtp->ssl);

  mbedtls_x509_crt_init(&dtls_srtp->cert);
  mbedtls_pk_init(&dtls_srtp->pkey);
  mbedtls_entropy_init(&dtls_srtp->entropy);
  mbedtls_ctr_drbg_init(&dtls_srtp->ctr_drbg);
  mbedtls_ssl_cookie_init(&dtls_srtp->cookie_ctx);
  dtls_srtp->initialized = 1;
#if CONFIG_MBEDTLS_DEBUG
  mbedtls_debug_set_threshold(3);
  mbedtls_ssl_conf_dbg(&dtls_srtp->conf, dtls_srtp_debug, NULL);
#endif
  dtls_srtp_selfsign_cert(dtls_srtp);

  mbedtls_ssl_conf_verify(&dtls_srtp->conf, dtls_srtp_cert_verify, NULL);

  mbedtls_ssl_conf_authmode(&dtls_srtp->conf, MBEDTLS_SSL_VERIFY_REQUIRED);

  mbedtls_ssl_conf_ca_chain(&dtls_srtp->conf, &dtls_srtp->cert, NULL);

  mbedtls_ssl_conf_own_cert(&dtls_srtp->conf, &dtls_srtp->cert, &dtls_srtp->pkey);

  mbedtls_ssl_conf_rng(&dtls_srtp->conf, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);

  mbedtls_ssl_conf_read_timeout(&dtls_srtp->conf, 1000);

  if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
    mbedtls_ssl_config_defaults(&dtls_srtp->conf,
                                MBEDTLS_SSL_IS_SERVER,
                                MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);

    mbedtls_ssl_cookie_setup(&dtls_srtp->cookie_ctx, mbedtls_ctr_drbg_random, &dtls_srtp->ctr_drbg);

    mbedtls_ssl_conf_dtls_cookies(&dtls_srtp->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &dtls_srtp->cookie_ctx);

  } else {
    mbedtls_ssl_config_defaults(&dtls_srtp->conf,
                                MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
  }

  mbedtls_ssl_conf_ciphersuites(&dtls_srtp->conf, dtls_srtp_gcm_ciphersuites);

  dtls_srtp_x509_digest(&dtls_srtp->cert, dtls_srtp->local_fingerprint);

  LOGD("local fingerprint: %s", dtls_srtp->local_fingerprint);

  mbedtls_ssl_conf_dtls_srtp_protection_profiles(&dtls_srtp->conf, default_profiles);

  mbedtls_ssl_conf_srtp_mki_value_supported(&dtls_srtp->conf, MBEDTLS_SSL_DTLS_SRTP_MKI_UNSUPPORTED);

  mbedtls_ssl_conf_cert_req_ca_list(&dtls_srtp->conf, MBEDTLS_SSL_CERT_REQ_CA_LIST_DISABLED);

  mbedtls_ssl_setup(&dtls_srtp->ssl, &dtls_srtp->conf);

  return 0;
}

void dtls_srtp_deinit(DtlsSrtp* dtls_srtp) {
  if (!dtls_srtp->initialized) {
    return;
  }

  if (dtls_srtp->srtp_in != NULL) {
    srtp_dealloc(dtls_srtp->srtp_in);
  }
  if (dtls_srtp->srtp_out != NULL) {
    srtp_dealloc(dtls_srtp->srtp_out);
  }

  mbedtls_ssl_free(&dtls_srtp->ssl);
  mbedtls_ssl_config_free(&dtls_srtp->conf);

  mbedtls_x509_crt_free(&dtls_srtp->cert);
  mbedtls_pk_free(&dtls_srtp->pkey);
  mbedtls_entropy_free(&dtls_srtp->entropy);
  mbedtls_ctr_drbg_free(&dtls_srtp->ctr_drbg);

  mbedtls_ssl_cookie_free(&dtls_srtp->cookie_ctx);
  memset(dtls_srtp, 0, sizeof(*dtls_srtp));
}

static int dtls_srtp_key_derivation(DtlsSrtp* dtls_srtp, const unsigned char* master_secret, size_t secret_len, const unsigned char* randbytes, size_t randbytes_len, mbedtls_tls_prf_types tls_prf_type) {
  int ret;
  const char* dtls_srtp_label = "EXTRACTOR-dtls_srtp";
  uint8_t key_material[DTLS_SRTP_KEY_MATERIAL_LENGTH];
  // Export keying material
  if ((ret = mbedtls_ssl_tls_prf(tls_prf_type, master_secret, secret_len, dtls_srtp_label,
                                 randbytes, randbytes_len, key_material, sizeof(key_material))) != 0) {
    LOGE("mbedtls_ssl_tls_prf failed(%d)", ret);
    return ret;
  }

#if 0
  int i, j;
  printf("    DTLS-SRTP key material is:");
  for (j = 0; j < sizeof(key_material); j++) {
    if (j % 8 == 0) {
      printf("\n    ");
    }
    printf("%02x ", key_material[j]);
  }
  printf("\n");

  /* produce a less readable output used to perform automatic checks
   * - compare client and server output
   * - interop test with openssl which client produces this kind of output
   */
  printf("    Keying material: ");
  for (j = 0; j < sizeof(key_material); j++) {
    printf("%02X", key_material[j]);
  }
  printf("\n");
#endif

  const uint8_t* client_key = key_material;
  const uint8_t* server_key = client_key + SRTP_MASTER_KEY_LENGTH;
  const uint8_t* client_salt = server_key + SRTP_MASTER_KEY_LENGTH;
  const uint8_t* server_salt = client_salt + SRTP_MASTER_SALT_LENGTH;
  uint8_t *local_key, *remote_key, *local_salt, *remote_salt;
  if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
    local_key = server_key;
    local_salt = server_salt;
    remote_key = client_key;
    remote_salt = client_salt;
  } else {
    local_key = client_key;
    local_salt = client_salt;
    remote_key = server_key;
    remote_salt = server_salt;
  }
  // derive inbounds keys

  memset(&dtls_srtp->remote_policy, 0, sizeof(dtls_srtp->remote_policy));

  srtp_crypto_policy_set_rtp_default(&dtls_srtp->remote_policy.rtp);
  srtp_crypto_policy_set_rtcp_default(&dtls_srtp->remote_policy.rtcp);

  memcpy(dtls_srtp->remote_policy_key, remote_key, SRTP_MASTER_KEY_LENGTH);
  memcpy(dtls_srtp->remote_policy_key + SRTP_MASTER_KEY_LENGTH, remote_salt, SRTP_MASTER_SALT_LENGTH);

  dtls_srtp->remote_policy.ssrc.type = ssrc_any_inbound;
  dtls_srtp->remote_policy.key = dtls_srtp->remote_policy_key;
  dtls_srtp->remote_policy.next = NULL;

  if (srtp_create(&dtls_srtp->srtp_in, &dtls_srtp->remote_policy) != srtp_err_status_ok) {
    LOGD("Error creating inbound SRTP session for component");
    return -1;
  }

  LOGI("Created inbound SRTP session");

  // derive outbounds keys
  memset(&dtls_srtp->local_policy, 0, sizeof(dtls_srtp->local_policy));

  srtp_crypto_policy_set_rtp_default(&dtls_srtp->local_policy.rtp);
  srtp_crypto_policy_set_rtcp_default(&dtls_srtp->local_policy.rtcp);

  memcpy(dtls_srtp->local_policy_key, local_key, SRTP_MASTER_KEY_LENGTH);
  memcpy(dtls_srtp->local_policy_key + SRTP_MASTER_KEY_LENGTH, local_salt, SRTP_MASTER_SALT_LENGTH);

  dtls_srtp->local_policy.ssrc.type = ssrc_any_outbound;
  dtls_srtp->local_policy.key = dtls_srtp->local_policy_key;
  dtls_srtp->local_policy.next = NULL;

  if (srtp_create(&dtls_srtp->srtp_out, &dtls_srtp->local_policy) != srtp_err_status_ok) {
    LOGE("Error creating outbound SRTP session");
    srtp_dealloc(dtls_srtp->srtp_in);
    dtls_srtp->srtp_in = NULL;
    return -1;
  }

  LOGI("Created outbound SRTP session");
  dtls_srtp->state = DTLS_SRTP_STATE_CONNECTED;
  return 0;
}

#if CONFIG_MBEDTLS_2_X
static int dtls_srtp_key_derivation_cb(void* context,
                                       const unsigned char* ms,
                                       const unsigned char* kb,
                                       size_t maclen,
                                       size_t keylen,
                                       size_t ivlen,
                                       const unsigned char client_random[32],
                                       const unsigned char server_random[32],
                                       mbedtls_tls_prf_types tls_prf_type) {
#else
static void dtls_srtp_key_derivation_cb(void* context,
                                        mbedtls_ssl_key_export_type secret_type,
                                        const unsigned char* secret,
                                        size_t secret_len,
                                        const unsigned char client_random[32],
                                        const unsigned char server_random[32],
                                        mbedtls_tls_prf_types tls_prf_type) {
#endif
  DtlsSrtp* dtls_srtp = (DtlsSrtp*)context;

  unsigned char master_secret[48];
  unsigned char randbytes[64];

  memcpy(randbytes, client_random, 32);
  memcpy(randbytes + 32, server_random, 32);

#if CONFIG_MBEDTLS_2_X
  memcpy(master_secret, ms, sizeof(master_secret));
  return dtls_srtp_key_derivation(dtls_srtp, master_secret, sizeof(master_secret), randbytes, sizeof(randbytes), tls_prf_type);
#else
  memcpy(master_secret, secret, sizeof(master_secret));
  dtls_srtp_key_derivation(dtls_srtp, master_secret, sizeof(master_secret), randbytes, sizeof(randbytes), tls_prf_type);
#endif
}

static int dtls_srtp_do_handshake(DtlsSrtp* dtls_srtp) {
  int ret;

  static mbedtls_timing_delay_context timer;

  mbedtls_ssl_set_timer_cb(&dtls_srtp->ssl, &timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

#if CONFIG_MBEDTLS_2_X
  mbedtls_ssl_conf_export_keys_ext_cb(&dtls_srtp->conf, dtls_srtp_key_derivation_cb, dtls_srtp);
#else
  mbedtls_ssl_set_export_keys_cb(&dtls_srtp->ssl, dtls_srtp_key_derivation_cb, dtls_srtp);
#endif

  mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send,
                      dtls_srtp->role == DTLS_SRTP_ROLE_SERVER ?
                          dtls_srtp_mbedtls_recv : dtls_srtp->udp_recv,
                      NULL);

  do {
    ret = mbedtls_ssl_handshake(&dtls_srtp->ssl);

  } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

  mbedtls_ssl_set_bio(&dtls_srtp->ssl, dtls_srtp, dtls_srtp->udp_send,
                      dtls_srtp->udp_recv, NULL);

  return ret;
}

static int dtls_srtp_handshake_server(DtlsSrtp* dtls_srtp) {
  int ret;

  while (1) {
    unsigned char client_ip[] = "test";

    mbedtls_ssl_session_reset(&dtls_srtp->ssl);

    mbedtls_ssl_set_client_transport_id(&dtls_srtp->ssl, client_ip, sizeof(client_ip));

    ret = dtls_srtp_do_handshake(dtls_srtp);

    if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
      LOGD("DTLS hello verification requested");

    } else if (ret != 0) {
      LOGE("failed! mbedtls_ssl_handshake returned -0x%.4x", (unsigned int)-ret);

      break;

    } else {
      break;
    }
  }

  LOGD("DTLS server handshake done");

  return ret;
}

static int dtls_srtp_handshake_client(DtlsSrtp* dtls_srtp) {
  int ret;

  ret = dtls_srtp_do_handshake(dtls_srtp);
  if (ret != 0) {
    LOGE("failed! mbedtls_ssl_handshake returned -0x%.4x\n\n", (unsigned int)-ret);
  }

  LOGD("DTLS client handshake done");

  return ret;
}

int dtls_srtp_handshake(DtlsSrtp* dtls_srtp, Address* addr) {
  int ret;
  dtls_srtp->remote_addr = addr;

  if (dtls_srtp->role == DTLS_SRTP_ROLE_SERVER) {
    ret = dtls_srtp_handshake_server(dtls_srtp);
  } else {
    ret = dtls_srtp_handshake_client(dtls_srtp);
  }

  const mbedtls_x509_crt* remote_crt;
  if ((remote_crt = mbedtls_ssl_get_peer_cert(&dtls_srtp->ssl)) != NULL) {
    dtls_srtp_x509_digest(remote_crt, dtls_srtp->actual_remote_fingerprint);

    if (strncmp(dtls_srtp->remote_fingerprint, dtls_srtp->actual_remote_fingerprint, DTLS_SRTP_FINGERPRINT_LENGTH) != 0) {
      LOGE("Actual and Expected Fingerprint mismatch: %s %s",
           dtls_srtp->remote_fingerprint,
           dtls_srtp->actual_remote_fingerprint);
      return -1;
    }

  } else {
    LOGE("no remote fingerprint");
    return -1;
  }

  mbedtls_dtls_srtp_info dtls_srtp_negotiation_result;
  mbedtls_ssl_get_dtls_srtp_negotiation_result(&dtls_srtp->ssl, &dtls_srtp_negotiation_result);

  return ret;
}

void dtls_srtp_reset_session(DtlsSrtp* dtls_srtp) {
  if (!dtls_srtp->initialized) {
    return;
  }
  if (dtls_srtp->srtp_in != NULL) {
    srtp_dealloc(dtls_srtp->srtp_in);
    dtls_srtp->srtp_in = NULL;
  }
  if (dtls_srtp->srtp_out != NULL) {
    srtp_dealloc(dtls_srtp->srtp_out);
    dtls_srtp->srtp_out = NULL;
  }
  mbedtls_ssl_session_reset(&dtls_srtp->ssl);

  dtls_srtp->state = DTLS_SRTP_STATE_INIT;
}

int dtls_srtp_write(DtlsSrtp* dtls_srtp, const unsigned char* buf, size_t len) {
  int ret;

  EDGERTC_STATS_BEGIN(start);
  do {
    ret = mbedtls_ssl_write(&dtls_srtp->ssl, buf, len);

  } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
  EDGERTC_STATS_END(EDGERTC_STAT_DTLS_WRITE, start);
  return ret;
}

int dtls_srtp_read(DtlsSrtp* dtls_srtp, unsigned char* buf, size_t len) {
  int ret;

  EDGERTC_STATS_BEGIN(start);
  do {
    ret = mbedtls_ssl_read(&dtls_srtp->ssl, buf, len);

  } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

  EDGERTC_STATS_END(EDGERTC_STAT_DTLS_READ, start);
  return ret;
}

int dtls_srtp_probe(uint8_t* buf) {
  if (buf == NULL)
    return 0;

  LOGD("DTLS content type: %d", buf[0]);
  // only handle application data
  return (buf[0] == 0x17);
}

void dtls_srtp_decrypt_rtp_packet(DtlsSrtp* dtls_srtp, uint8_t* packet, int* bytes) {
  srtp_unprotect(dtls_srtp->srtp_in, packet, bytes);
}

void dtls_srtp_decrypt_rtcp_packet(DtlsSrtp* dtls_srtp, uint8_t* packet, int* bytes) {
  srtp_unprotect_rtcp(dtls_srtp->srtp_in, packet, bytes);
}

void dtls_srtp_encrypt_rtp_packet(DtlsSrtp* dtls_srtp, uint8_t* packet, int* bytes) {
  srtp_protect(dtls_srtp->srtp_out, packet, bytes);
}

int dtls_srtp_encrypt_rctp_packet(DtlsSrtp* dtls_srtp, uint8_t* packet, int* bytes) {
  return srtp_protect_rtcp(dtls_srtp->srtp_out, packet, bytes);
}
