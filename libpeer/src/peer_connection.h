/**
 * @file peer_connection.h
 * @brief Struct PeerConnection
 */
#ifndef PEER_CONNECTION_H_
#define PEER_CONNECTION_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SdpType {
  SDP_TYPE_OFFER = 0,
  SDP_TYPE_ANSWER,
} SdpType;

typedef enum PeerConnectionState {

  PEER_CONNECTION_CLOSED = 0,
  PEER_CONNECTION_NEW,
  PEER_CONNECTION_CHECKING,
  PEER_CONNECTION_CONNECTED,
  PEER_CONNECTION_COMPLETED,
  PEER_CONNECTION_FAILED,
  PEER_CONNECTION_DISCONNECTED,

} PeerConnectionState;

typedef enum DataChannelType {

  DATA_CHANNEL_NONE = 0,
  DATA_CHANNEL_STRING,
  DATA_CHANNEL_BINARY,

} DataChannelType;

typedef enum DecpChannelType {
  DATA_CHANNEL_RELIABLE = 0x00,
  DATA_CHANNEL_RELIABLE_UNORDERED = 0x80,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT = 0x01,
  DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED = 0x81,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED = 0x02,
  DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED = 0x82,
} DecpChannelType;

typedef enum MediaCodec {

  CODEC_NONE = 0,

  /* Video */
  CODEC_H264,
  CODEC_VP8,    // not implemented yet
  CODEC_MJPEG,  // not implemented yet

  /* Audio */
  CODEC_OPUS,  // not implemented yet
  CODEC_PCMA,
  CODEC_PCMU,

} MediaCodec;

typedef struct IceServer {
  const char* urls;
  const char* username;
  const char* credential;

} IceServer;

typedef struct RtpPacketMetadata {
  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
} RtpPacketMetadata;

typedef struct PeerConfiguration {
  IceServer ice_servers[5];

  MediaCodec audio_codec;
  MediaCodec video_codec;
  DataChannelType datachannel;
  /* Native TX DATA descriptor limit. Zero selects the four-entry control-safe
   * default; it does not disable reliable tracking or retransmission. */
  size_t sctp_send_cache_max_packets;
  /* Native RX owner-capacity budget. Zero selects 32 KiB; SDP advertises
   * min(budget / 2, 16 KiB) as the maximum single message size. */
  size_t sctp_receive_cache_max_bytes;

  void (*onaudiotrack)(uint8_t* data, size_t size,
                       const RtpPacketMetadata* metadata,
                       void* userdata);
  void (*onvideotrack)(uint8_t* data, size_t size,
                       const RtpPacketMetadata* metadata,
                       void* userdata);
  void (*on_request_keyframe)(void* userdata);
  void* user_data;

} PeerConfiguration;

typedef struct PeerConnection PeerConnection;

const char* peer_connection_state_to_string(PeerConnectionState state);

PeerConnectionState peer_connection_get_state(PeerConnection* pc);

void* peer_connection_get_sctp(PeerConnection* pc);

PeerConnection* peer_connection_create(PeerConfiguration* config);

void peer_connection_destroy(PeerConnection* pc);

void peer_connection_close(PeerConnection* pc);

int peer_connection_loop(PeerConnection* pc);

int peer_connection_create_datachannel(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol);

int peer_connection_create_datachannel_sid(PeerConnection* pc, DecpChannelType channel_type, uint16_t priority, uint32_t reliability_parameter, char* label, char* protocol, uint16_t sid);

/**
 * @brief send message to data channel
 * @param[in] peer connection
 * @param[in] message buffer
 * @param[in] length of message
 * Native SCTP success means queued. Reliable messages may be transmitted by a
 * later timer poll if the first DTLS write fails; the caller must not retry the
 * same message after a non-negative return.
 */
int peer_connection_datachannel_send(PeerConnection* pc, char* message, size_t len);

/**
 * @brief packetize and send a message in place
 *
 * The SCTP layer does not copy the payload into its packet buffer. The message
 * buffer must provide writable space before and after the payload; its contents
 * are consumed and may be modified before this function returns. Native SCTP
 * uses this mode only for maxRetransmits=0 channels; reliable channels copy or
 * retain an explicitly owned net_buf instead. The caller may release its frame
 * immediately after this function returns.
 */
int peer_connection_datachannel_send_inplace(PeerConnection* pc,
                                             uint8_t* message, size_t len,
                                             size_t headroom,
                                             size_t tailroom);

int peer_connection_datachannel_send_sid(PeerConnection* pc, char* message, size_t len, uint16_t sid);

struct net_buf;

/* Native SCTP retains one reference to the immutable buffer until cumulative
 * ACK or partial-reliability abandon, without a retransmission-cache copy. The
 * caller may unref its reference after a successful call. Each wire packet is
 * still copied into the contiguous DTLS packet scratch buffer. */
int peer_connection_datachannel_send_buf(PeerConnection* pc, struct net_buf* message,
                                         uint16_t sid);

int peer_connection_send_audio(PeerConnection* pc, const uint8_t* packet, size_t bytes);

int peer_connection_send_video(PeerConnection* pc, const uint8_t* packet, size_t bytes);

void peer_connection_set_remote_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

void peer_connection_set_local_description(PeerConnection* pc, const char* sdp, SdpType sdp_type);

const char* peer_connection_create_offer(PeerConnection* pc);

const char* peer_connection_create_answer(PeerConnection* pc);

/**
 * @brief register callback function to handle packet loss from RTCP receiver report
 * @param[in] peer connection
 * @param[in] callback function void (*cb)(float fraction_loss, uint32_t total_loss, void *userdata)
 * @param[in] userdata for callback function
 */
void peer_connection_on_receiver_packet_loss(PeerConnection* pc,
                                             void (*on_receiver_packet_loss)(float fraction_loss, uint32_t total_loss, void* userdata));

/**
 * @brief Set the callback function to handle onicecandidate event.
 * @param A PeerConnection.
 * @param A callback function to handle onicecandidate event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_onicecandidate(PeerConnection* pc, void (*onicecandidate)(char* sdp_text, void* userdata));

/**
 * @brief Set the callback function to handle oniceconnectionstatechange event.
 * @param A PeerConnection.
 * @param A callback function to handle oniceconnectionstatechange event.
 * @param A userdata which is pass to callback function.
 */
void peer_connection_oniceconnectionstatechange(PeerConnection* pc,
                                                void (*oniceconnectionstatechange)(PeerConnectionState state, void* userdata));

/**
 * @brief register callback function to handle event of datachannel
 * @param[in] peer connection
 * @param[in] callback function when message received
 * @param[in] callback function when connection is opened
 * @param[in] callback function when connection is closed
 */
void peer_connection_ondatachannel(PeerConnection* pc,
                                   void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid),
                                   void (*onopen)(void* userdata),
                                   void (*onclose)(void* userdata));

/* Native SCTP callback for complete messages on unordered
 * binary channels. A message is delivered as TSN-ordered, consecutive spans;
 * B=0x02 marks the first span and E=0x01 the last. Each pointer is borrowed
 * only for its callback and must not be retained by the application. This
 * avoids the multi-fragment linearization copy required by onmessage. With
 * usrsctp, complete messages continue to use onmessage. */
void peer_connection_ondatachannelfragment(PeerConnection* pc,
                                          void (*onfragment)(char* data, size_t size, void* userdata,
                                                             uint16_t sid, uint32_t tsn, uint8_t flags));

int peer_connection_lookup_sid(PeerConnection* pc, const char* label, uint16_t* sid);

char* peer_connection_lookup_sid_label(PeerConnection* pc, uint16_t sid);

/**
 * @brief adds a new remote candidate to the peer connection
 * @param[in] peer connection
 * @param[in] ice candidate
 */
int peer_connection_add_ice_candidate(PeerConnection* pc, char* ice_candidate);

#ifdef __cplusplus
}
#endif

#endif  // PEER_CONNECTION_H_
