#ifndef SCTP_H_
#define SCTP_H_

#include "config.h"
#include "dtls_srtp.h"
#include "utils.h"

typedef enum DecpMsgType {

  DATA_CHANNEL_OPEN = 0x03,
  DATA_CHANNEL_ACK = 0x02,

} DecpMsgType;

typedef enum DataChannelPpid {

  DATA_CHANNEL_PPID_CONTROL = 50,
  DATA_CHANNEL_PPID_DOMSTRING = 51,
  DATA_CHANNEL_PPID_BINARY_PARTIAL = 52,
  DATA_CHANNEL_PPID_BINARY = 53,
  DATA_CHANNEL_PPID_DOMSTRING_PARTIAL = 54

} DataChannelPpid;

#if !CONFIG_USE_USRSCTP

typedef struct SctpChunkParam {
  uint16_t type;
  uint16_t length;
  uint8_t value[0];

} SctpChunkParam;

typedef enum SctpParamType {

  SCTP_PARAM_STATE_COOKIE = 7,
  SCTP_PARAM_FORWARD_TSN_SUPPORTED = 0xc000,

} SctpParamType;

typedef enum SctpHeaderType {

  SCTP_DATA = 0,
  SCTP_INIT = 1,
  SCTP_INIT_ACK = 2,
  SCTP_SACK = 3,
  SCTP_HEARTBEAT = 4,
  SCTP_HEARTBEAT_ACK = 5,
  SCTP_ABORT = 6,
  SCTP_SHUTDOWN = 7,
  SCTP_SHUTDOWN_ACK = 8,
  SCTP_ERROR = 9,
  SCTP_COOKIE_ECHO = 10,
  SCTP_COOKIE_ACK = 11,
  SCTP_ECNE = 12,
  SCTP_CWR = 13,
  SCTP_SHUTDOWN_COMPLETE = 14,
  SCTP_AUTH = 15,
  SCTP_ASCONF_ACK = 128,
  SCTP_ASCONF = 130,
  SCTP_FORWARD_TSN = 192

} SctpHeaderType;

typedef struct SctpChunkCommon {
  uint8_t type;
  uint8_t flags;
  uint16_t length;

} SctpChunkCommon;

typedef struct SctpForwardTsnChunk {
  SctpChunkCommon common;
  uint32_t new_cumulative_tsn;

} SctpForwardTsnChunk;

typedef struct SctpHeader {
  uint16_t source_port;
  uint16_t destination_port;
  uint32_t verification_tag;
  uint32_t checksum;

} SctpHeader;

typedef struct SctpPacket {
  SctpHeader header;
  uint8_t chunks[0];

} SctpPacket;

typedef struct SctpSackChunk {
  SctpChunkCommon common;
  uint32_t cumulative_tsn_ack;
  uint32_t a_rwnd;
  uint16_t number_of_gap_ack_blocks;
  uint16_t number_of_dup_tsns;
  uint8_t blocks[0];

} SctpSackChunk;

typedef struct SctpDataChunk {
  uint8_t type;
  uint8_t iube;
  uint16_t length;
  uint32_t tsn;
  uint16_t sid;
  uint16_t sqn;
  uint32_t ppid;
  uint8_t data[0];

} SctpDataChunk;

typedef struct SctpInitChunk {
  SctpChunkCommon common;
  uint32_t initiate_tag;
  uint32_t a_rwnd;
  uint16_t number_of_outbound_streams;
  uint16_t number_of_inbound_streams;
  uint32_t initial_tsn;
  SctpChunkParam param[0];

} SctpInitChunk;

typedef struct SctpCookieEchoChunk {
  SctpChunkCommon common;
  uint8_t cookie[0];
} SctpCookieEchoChunk;

#endif

typedef enum SctpDataPpid {

  PPID_CONTROL = 50,
  PPID_STRING = 51,
  PPID_BINARY = 53,
  PPID_STRING_EMPTY = 56,
  PPID_BINARY_EMPTY = 57

} SctpDataPpid;

typedef enum SctpDataMode {

  SCTP_DATA_COPY,       /* Retain a private copy for any asynchronous retry. */
  SCTP_DATA_ZERO_COPY   /* Use caller headroom; valid only for synchronous PR0. */

} SctpDataMode;

#define SCTP_MAX_STREAMS 5

#if !CONFIG_USE_USRSCTP
#define SCTP_RECEIVE_TSN_WINDOW 256U

typedef struct SctpTx SctpTx;
typedef struct SctpRx SctpRx;
#endif

struct net_buf;
struct net_buf_pool;

typedef struct {
  char label[32];                    /* DCEP label, truncated to this storage. */
  uint16_t sid;                      /* SCTP stream identifier. */
  uint8_t no_retransmit;             /* Unordered maxRetransmits=0 RX expiry policy. */
  uint8_t channel_type;              /* DCEP type; bit 7 is unordered. */
  uint32_t reliability_parameter;    /* Retry count or lifetime in milliseconds. */
  uint16_t send_sqn;                 /* Next SSN for locally ordered messages. */
  uint16_t recv_sqn;                 /* Next SSN expected on this stream. */
} SctpStreamEntry;

typedef struct Sctp {
  struct socket* sock;

  int local_port;
  int remote_port;
  int connected;
  uint32_t verification_tag;
  uint32_t tsn;
  DtlsSrtp* dtls_srtp;
  int stream_count;
  SctpStreamEntry stream_table[SCTP_MAX_STREAMS];

  /* datachannel */
  void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid);
  void (*onfragment)(char* data, size_t size, void* userdata, uint16_t sid,
                     uint32_t tsn, uint8_t flags);
  void (*onopen)(void* userdata);
  void (*onclose)(void* userdata);

  void* userdata;
  uint8_t buf[CONFIG_MTU];

#if !CONFIG_USE_USRSCTP
  SctpTx* tx;                        /* Native TX ring and retained payload refs. */
  SctpRx* rx;                        /* Native RX fragment list and cache usage. */
  struct net_buf_pool* packets;      /* Fixed slots shared by this connection's TX/RX. */
  size_t sent_packet_limit;          /* Maximum retained DATA descriptors. */
  size_t recv_cache_limit;           /* Maximum owner capacity retained by RX. */
  struct net_buf* handshake;          /* Immutable INIT or COOKIE_ECHO snapshot. */
  uint32_t handshake_sent_ms;        /* Last handshake snapshot transmission. */
  uint16_t handshake_timeout_ms;     /* Exponential retry delay, capped at 640 ms. */
  uint32_t last_sack_tsn;            /* Highest cumulative TSN acknowledged by peer. */
  uint8_t peer_supports_forward_tsn;
  uint32_t recv_cumulative_tsn;      /* Highest contiguous TSN accepted locally. */
  uint32_t recv_tsn_map[SCTP_RECEIVE_TSN_WINDOW / 32U]; /* SACK gap bitmap. */
  uint8_t recv_initialized;          /* Peer INIT established the receive baseline. */
#endif
} Sctp;

int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls_srtp);

void sctp_destroy_association(Sctp* sctp);

/* Final teardown after the peer loop has returned all borrowed packet refs. */
void sctp_deinit(Sctp* sctp);

void sctp_usrsctp_init();

void sctp_usrsctp_deinit();

int sctp_is_connected(Sctp* sctp);

void sctp_incoming_data(Sctp* sctp, char* buf, size_t len);

/* Borrow packet for synchronous parsing; queued receive fragments retain their
 * own net_buf reference before the caller releases its reference. */
void sctp_incoming_net_buf(Sctp* sctp, struct net_buf* packet);

/* Allocate one fixed-pool net_buf reference with capacity up to CONFIG_MTU
 * for DTLS plaintext, handshake snapshots, or outgoing owned messages. */
#if !CONFIG_USE_USRSCTP
struct net_buf* sctp_packet_alloc(Sctp* sctp, size_t size);
#endif

/* Return the native maximum message size advertised through SDP. */
size_t sctp_receive_message_limit(const Sctp* sctp);

/* Run native handshake, TX retransmission/FORWARD_TSN and RX expiry work. */
void sctp_process_timers(Sctp* sctp);

/* Queue one data-channel message. A non-negative native return means the
 * complete message was admitted to the bounded TX ring, not that every DATA
 * packet has already been acknowledged by the peer. */
int sctp_outgoing_data(Sctp* sctp, char* buf, size_t len,
                       SctpDataPpid ppid, uint16_t sid,
                       SctpDataMode mode, size_t headroom, size_t tailroom);

/* Native SCTP retains one reference to this contiguous immutable payload until
 * cumulative ACK or partial-reliability abandon. The caller may unref its own
 * reference after a successful call; the packetizer still copies each wire
 * fragment into the contiguous DTLS packet scratch buffer. */
int sctp_outgoing_net_buf(Sctp* sctp, struct net_buf* message,
                          SctpDataPpid ppid, uint16_t sid);

int sctp_configure_stream(Sctp* sctp, uint16_t sid, const char* label,
                          uint8_t channel_type, uint32_t reliability_parameter);

void sctp_onmessage(Sctp* sctp, void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid));
/* Register the borrowed-span callback used by complete unordered binary messages. */
void sctp_onfragment(Sctp* sctp,
                     void (*onfragment)(char* data, size_t size, void* userdata,
                                        uint16_t sid, uint32_t tsn, uint8_t flags));

void sctp_onopen(Sctp* sctp, void (*onopen)(void* userdata));

void sctp_onclose(Sctp* sctp, void (*onclose)(void* userdata));

#endif  // SCTP_H_
