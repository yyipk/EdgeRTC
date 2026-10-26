#ifndef SCTP_NATIVE_H_
#define SCTP_NATIVE_H_

#include "sctp.h"

#if !CONFIG_USE_USRSCTP
#include <zephyr/net_buf.h>
#include "ports.h"

#if CONFIG_EDGERTC_SCTP_DEBUG
#define SCTP_DIAG_LOG(fmt, ...) printf("sctp_diag: " fmt "\r\n", ##__VA_ARGS__)
#else
#define SCTP_DIAG_LOG(fmt, ...)
#endif

/* Native-only helpers stay out of the usrsctp build.  sctp.c owns packet
 * parsing and association control; this header exposes the shared writer and
 * the separate TX/RX state machines. */
static inline int sctp_tsn_gte(uint32_t lhs, uint32_t rhs) {
  /* SCTP serial comparisons remain valid across uint32_t wrap within the
   * bounded receive window. */
  return (int32_t)(lhs - rhs) >= 0;
}

SctpStreamEntry* sctp_get_stream(Sctp* sctp, uint16_t sid);
int sctp_write_packet(Sctp* sctp, uint8_t* packet, size_t size);
void sctp_abort(Sctp* sctp, uint16_t cause);
int sctp_handle_dcep(Sctp* sctp, uint16_t sid, const uint8_t* data, size_t size);

int sctp_tx_init(Sctp* sctp);
void sctp_tx_destroy(Sctp* sctp);
int sctp_tx_send(Sctp* sctp, uint8_t* data, size_t size, SctpDataPpid ppid,
                 uint16_t sid, SctpDataMode mode, size_t headroom,
                 size_t tailroom, struct net_buf* owner);
void sctp_tx_sack(Sctp* sctp, const SctpSackChunk* sack, size_t size);
void sctp_tx_poll(Sctp* sctp);
int sctp_tx_can_send(Sctp* sctp, size_t size);

int sctp_rx_init(Sctp* sctp);
void sctp_rx_destroy(Sctp* sctp);
void sctp_rx_reset(Sctp* sctp, uint32_t cumulative_tsn, uint8_t initialized);
void sctp_rx_data(Sctp* sctp, const SctpDataChunk* chunk, size_t size,
                  struct net_buf* owner);
void sctp_rx_forward(Sctp* sctp, const SctpForwardTsnChunk* chunk, size_t size);
void sctp_rx_sack(Sctp* sctp);
void sctp_rx_poll(Sctp* sctp);
#endif

#endif
