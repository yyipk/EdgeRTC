#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "sctp_native.h"

#if !CONFIG_USE_USRSCTP

#define SCTP_FAST_RETRANSMIT_MISS_REPORTS 3U

/*
 * Native SCTP keeps retransmission state outside the wire buffer.  A message
 * owns the caller's payload once, while each DATA chunk has one descriptor in
 * the bounded ring below.  This separation is what allows an owned net_buf to
 * be referenced again for retransmission without making a second cache copy.
 *
 * The DTLS writer still requires one contiguous SCTP packet.  Therefore
 * sctp_tx_write() copies an owned fragment into sctp->buf for normal sends;
 * only the synchronous PR-SCTP-0 inplace path writes the SCTP header directly
 * in the caller-provided headroom.
 */
typedef struct SctpTxMessage {
  struct net_buf* meta_buf;     /* Fixed-pool owner for this message state. */
  struct net_buf* owner;       /* One retained reference for the whole message. */
  const uint8_t* data;         /* Borrowed raw pointer; never retained if inplace. */
  size_t remaining;             /* Number of ring descriptors still alive. */
  uint32_t created_ms;          /* Start time for timeout-based partial reliability. */
  uint32_t reliability;         /* Retry count or lifetime, depending on policy. */
  uint32_t ppid;                /* Data-channel PPID written into every fragment. */
  uint16_t sid;                 /* SCTP stream identifier. */
  uint16_t sqn;                 /* One SSN shared by all fragments of this message. */
  uint8_t policy;               /* 0 reliable, 1 max-retransmits, 2 max-lifetime. */
  uint8_t unordered;            /* Mirrors SCTP U bit; unordered data has no SSN gate. */
  uint8_t abandoned;            /* Payload was released and FORWARD_TSN may advance. */
} SctpTxMessage;

typedef struct SctpTxPacket {
  SctpTxMessage* message;       /* Parent message; valid until this descriptor is ACKed. */
  struct net_buf* payload;      /* Per-fragment copy for the legacy raw-pointer API. */
  size_t offset;                /* Offset in message->data for the raw-pointer API. */
  size_t length;                /* DATA payload bytes, excluding SCTP headers/padding. */
  uint32_t tsn;                 /* TSN assigned before the message enters the ring. */
  uint32_t last_send_ms;        /* Last attempted wire transmission. */
  uint32_t retries;             /* Successful retransmissions after the first send. */
  uint8_t miss_reports;         /* HTNA missing reports for Fast Retransmit. */
  uint8_t flags;                /* SCTP B/E/U bits: 0x02/0x01/0x04. */
  uint8_t sent;                 /* Initial transmission succeeded at least once. */
  uint8_t gap_acked;            /* Peer SACKed this TSN beyond its cumulative ACK. */
  uint8_t gap_seen;             /* TSN was gap-acknowledged at least once. */
  uint8_t reneged;              /* This SACK withdrew a previous gap ACK. */
  uint8_t fast_retransmitted;   /* Do not Fast Retransmit this TSN again. */
  uint8_t fast_pending;         /* One Fast Retransmit is pending for this TSN. */
} SctpTxPacket;

struct SctpTx {
  SctpTxPacket* packets;        /* Fixed-size circular descriptor storage. */
  struct net_buf_pool message_pool; /* One metadata slot per ring descriptor. */
  size_t capacity;              /* Maximum number of DATA chunks retained. */
  size_t head;                  /* Index of the oldest unretired descriptor. */
  size_t count;                 /* Number of descriptors currently in use. */
  uint32_t checked_ms;          /* 150 ms periodic retransmission checkpoint. */
  uint32_t forward_ms;          /* Last FORWARD_TSN transmission time. */
  uint32_t forward_tsn;         /* Last FORWARD_TSN value awaiting SACK. */
  uint32_t max_ack_tsn;         /* Highest TSN actually written on the wire. */
  uint32_t recovery_exit_tsn;   /* Highest outstanding TSN on entering recovery. */
  uint8_t forward_pending;      /* Suppress duplicate FORWARD_TSN for 100 ms. */
  uint8_t fast_recovery;        /* Cumulative ACK has not reached the exit TSN. */
};

static void sctp_tx_run(Sctp* sctp, int fast_only);

static size_t sctp_tx_payload_max(void) {
  size_t mtu = SCTP_MTU < CONFIG_MTU ? SCTP_MTU : CONFIG_MTU;
  /* SCTP packet length is padded to four bytes; DATA headers are 16 bytes. */
  return (mtu & ~(size_t)3) - sizeof(SctpHeader) - sizeof(SctpDataChunk);
}

static SctpTxPacket* sctp_tx_packet(SctpTx* tx, size_t index) {
  return &tx->packets[(tx->head + index) % tx->capacity];
}

static void sctp_tx_release(SctpTxPacket* packet) {
  /* A descriptor owns its private raw-API payload copy.  The caller-owned
   * net_buf is released only after the last fragment leaves the ring. */
  SctpTxMessage* message = packet->message;
  if (packet->payload != NULL) {
    net_buf_unref(packet->payload);
  }
  if (--message->remaining == 0) {
    if (message->owner != NULL) {
      net_buf_unref(message->owner);
    }
    net_buf_unref(message->meta_buf);
  }
  memset(packet, 0, sizeof(*packet));
}

static void sctp_tx_abandon(SctpTx* tx, SctpTxMessage* message) {
  /* Partial reliability abandons the entire message, not an individual DATA
   * chunk.  Descriptors remain until cumulative ACK/FORWARD_TSN retires them,
   * but all payload references are dropped immediately. */
  message->abandoned = 1;
  if (message->owner != NULL) {
    net_buf_unref(message->owner);
    message->owner = NULL;
  }
  for (size_t i = 0; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    if (packet->message == message && packet->payload != NULL) {
      net_buf_unref(packet->payload);
      packet->payload = NULL;
    }
  }
  message->data = NULL;
}

int sctp_tx_init(Sctp* sctp) {
  SctpTx* tx;
  size_t arena_size;
  /* Zero means the small control-safe default, not disabled reliability
   * tracking: DCEP and reliable DATA still need ACK state. */
  size_t capacity = sctp->sent_packet_limit ? sctp->sent_packet_limit : 4;
  if (capacity > UINT16_MAX || capacity > SIZE_MAX / sizeof(SctpTxPacket)) {
    return -1;
  }
  arena_size = net_buf_pool_arena_size(capacity, sizeof(SctpTxMessage), 0);
  if (!arena_size || arena_size > SIZE_MAX - sizeof(*tx)) {
    return -1;
  }
  tx = calloc(1, sizeof(*tx) + arena_size);
  if (tx == NULL) {
    return -1;
  }
  tx->packets = calloc(capacity, sizeof(*tx->packets));
  if (tx->packets == NULL ||
      net_buf_pool_init(&tx->message_pool, tx + 1, arena_size,
                        capacity, sizeof(SctpTxMessage), 0, NULL) < 0) {
    free(tx->packets);
    free(tx);
    return -1;
  }
  tx->capacity = capacity;
  tx->max_ack_tsn = sctp->tsn - 1;
  sctp->tx = tx;
  SCTP_DIAG_LOG("tx init cap=%u", (unsigned int)capacity);
  return 0;
}

void sctp_tx_destroy(Sctp* sctp) {
  SctpTx* tx = sctp->tx;
  if (tx == NULL) {
    return;
  }
  for (size_t i = 0; i < tx->count; ++i) {
    sctp_tx_release(sctp_tx_packet(tx, i));
  }
  net_buf_pool_deinit(&tx->message_pool);
  free(tx->packets);
  free(tx);
  sctp->tx = NULL;
}

int sctp_tx_can_send(Sctp* sctp, size_t len) {
  size_t payload_max = sctp_tx_payload_max();
  size_t packets = len == 0 ? 1 : 1 + (len - 1) / payload_max;
  /* Reserve every descriptor before assigning TSNs.  This prevents accepting
   * only the first part of a message when the ring cannot hold its tail. */
  return sctp->tx != NULL && len <= INT_MAX &&
         packets <= sctp->tx->capacity - sctp->tx->count;
}

static int sctp_tx_write(Sctp* sctp, SctpTxPacket* packet, uint8_t* inplace) {
  SctpTxMessage* message = packet->message;
  size_t header_size = sizeof(SctpHeader) + sizeof(SctpDataChunk);
  /* Normal/retransmitted packets are rebuilt from retained fragment storage;
   * PR0 sends synchronously into caller headroom and is never retried. */
  uint8_t* out = inplace != NULL ? inplace - header_size : sctp->buf;
  SctpDataChunk* chunk = (SctpDataChunk*)(out + sizeof(SctpHeader));
  size_t size = header_size + packet->length;
  size_t padded = (size + 3) & ~(size_t)3;

  chunk->type = SCTP_DATA;
  chunk->iube = packet->flags;
  chunk->length = htons(sizeof(*chunk) + packet->length);
  chunk->tsn = htonl(packet->tsn);
  chunk->sid = htons(message->sid);
  chunk->sqn = htons(message->sqn);
  chunk->ppid = htonl(message->ppid);
  if (inplace == NULL) {
    if (message->ppid == PPID_STRING_EMPTY || message->ppid == PPID_BINARY_EMPTY) {
      chunk->data[0] = 0;
    } else {
      memcpy(chunk->data, packet->payload != NULL ? packet->payload->data :
                          message->data + packet->offset, packet->length);
    }
  }
  memset(out + size, 0, padded - size);
  int ret = sctp_write_packet(sctp, out, padded);
  if (ret >= 0 && sctp_tsn_gte(packet->tsn, sctp->tx->max_ack_tsn)) {
    sctp->tx->max_ack_tsn = packet->tsn;
  }
  return ret;
}

int sctp_tx_send(Sctp* sctp, uint8_t* data, size_t len, SctpDataPpid ppid,
                 uint16_t sid, SctpDataMode mode, size_t headroom,
                 size_t tailroom, struct net_buf* owner) {
  SctpTx* tx = sctp->tx;
  SctpStreamEntry* stream = sctp_get_stream(sctp, sid);
  SctpTxMessage* message;
  size_t payload_max = sctp_tx_payload_max();
  size_t wire_len = len == 0 ? 1 : len;
  size_t packets = 1 + (wire_len - 1) / payload_max;
  size_t old_count;
  uint8_t policy = ppid == PPID_CONTROL || stream == NULL ? 0 :
                   stream->channel_type & 0x7f;
  /* Inplace is intentionally restricted to unordered max-retransmits=0.  A
   * reliable packet may outlive this call, so retaining a raw pointer would be
   * unsafe; those calls use per-fragment copies or an owned net_buf instead. */
  int inplace = owner == NULL && mode == SCTP_DATA_ZERO_COPY && len != 0 &&
                policy == 1 && stream->reliability_parameter == 0;

  if (stream == NULL || (len != 0 && data == NULL) || (len == 0 && ppid == PPID_CONTROL) ||
      !sctp_tx_can_send(sctp, len) ||
      (ppid != PPID_CONTROL && ppid != PPID_STRING && ppid != PPID_BINARY &&
       ppid != PPID_STRING_EMPTY && ppid != PPID_BINARY_EMPTY) ||
      (len != 0 && (ppid == PPID_STRING_EMPTY || ppid == PPID_BINARY_EMPTY)) ||
      policy > 2 || (policy != 0 && !sctp->peer_supports_forward_tsn) ||
      (policy == 2 && stream->reliability_parameter > INT32_MAX)) {
    return -1;
  }
  if (owner != NULL &&
      ((uintptr_t)data < (uintptr_t)owner->data ||
       (uintptr_t)data - (uintptr_t)owner->data > owner->len ||
       len > owner->len - ((uintptr_t)data - (uintptr_t)owner->data))) {
    return -1;
  }
  if (inplace) {
    size_t header_size = sizeof(SctpHeader) + sizeof(SctpDataChunk);
    size_t last = 1 + (wire_len - 1) % payload_max;
    if (headroom < header_size || (uintptr_t)data < header_size ||
        (((uintptr_t)data - header_size) & 3) != 0 ||
        ((4 - ((header_size + last) & 3)) & 3) > tailroom) {
      return -1;
    }
  }
  struct net_buf* meta_buf = net_buf_alloc(&tx->message_pool, K_NO_WAIT);
  if (meta_buf == NULL) {
    SCTP_DIAG_LOG("tx meta pool empty used=%u/%u",
                  (unsigned int)tx->count, (unsigned int)tx->capacity);
    return -1;
  }
  message = (SctpTxMessage*)meta_buf->data;
  memset(message, 0, sizeof(*message));
  message->meta_buf = meta_buf;
  if (owner != NULL) {
    message->owner = net_buf_ref(owner);
    message->data = data;
  } else {
    message->data = inplace ? data : NULL;
  }
  message->remaining = packets;
  message->created_ms = ports_get_time_ms();
  message->policy = policy;
  message->reliability = stream->reliability_parameter;
  message->unordered = ppid == PPID_CONTROL ? 0 : (stream->channel_type >> 7);
  message->sid = sid;
  message->sqn = message->unordered ? 0 : stream->send_sqn;
  message->ppid = len != 0 ? ppid :
                  ppid == PPID_STRING || ppid == PPID_STRING_EMPTY ?
                  PPID_STRING_EMPTY : PPID_BINARY_EMPTY;
  old_count = tx->count;
  /* Queue the whole message before writing.  A failed DTLS write leaves a
   * reliable/partially-reliable message in the ring for a later poll; PR0 is
   * abandoned immediately because its raw buffer is valid only in this call. */
  for (size_t i = 0; i < packets; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, old_count + i);
    packet->message = message;
    packet->offset = i * payload_max;
    packet->length = wire_len - packet->offset;
    if (packet->length > payload_max) {
      packet->length = payload_max;
    }
    if (owner == NULL && !inplace && len != 0) {
      packet->payload = sctp_packet_alloc(sctp, packet->length);
      if (packet->payload == NULL) {
        SCTP_DIAG_LOG("tx payload pool empty tsn=%lu used=%u/%u",
                      (unsigned long)(sctp->tsn + (uint32_t)i),
                      (unsigned int)tx->count, (unsigned int)tx->capacity);
        for (size_t j = 0; j < i; ++j) {
          SctpTxPacket* allocated = sctp_tx_packet(tx, old_count + j);
          net_buf_unref(allocated->payload);
          memset(allocated, 0, sizeof(*allocated));
        }
        memset(packet, 0, sizeof(*packet));
        net_buf_unref(meta_buf);
        return -1;
      }
      net_buf_add_mem(packet->payload, data + packet->offset, packet->length);
    }
    packet->tsn = sctp->tsn + (uint32_t)i;
    packet->flags = (message->unordered ? 4 : 0) |
                    (i == 0 ? 2 : 0) | (i + 1 == packets ? 1 : 0);
  }
  tx->count += packets;
  sctp->tsn += (uint32_t)packets;
  if (old_count == 0 || tx->count == tx->capacity) {
    SCTP_DIAG_LOG("tx queue tsn=%lu..%lu n=%u used=%u/%u",
                  (unsigned long)(sctp->tsn - (uint32_t)packets),
                  (unsigned long)(sctp->tsn - 1U), (unsigned int)packets,
                  (unsigned int)tx->count, (unsigned int)tx->capacity);
  }
  if (!message->unordered) {
    ++stream->send_sqn;
  }
  for (size_t i = old_count; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    int ret = sctp_tx_write(sctp, packet, inplace ? data + packet->offset : NULL);
    packet->last_send_ms = ports_get_time_ms();
    if (ret < 0) {
      SCTP_DIAG_LOG("tx send fail tsn=%lu ret=%d",
                    (unsigned long)packet->tsn, ret);
      if (policy == 1 && message->reliability == 0) {
        sctp_tx_abandon(tx, message);
      }
      break;
    }
    packet->sent = 1;
  }
  if (inplace) {
    message->data = NULL;
  }
  return (int)len;
}

void sctp_tx_sack(Sctp* sctp, const SctpSackChunk* sack, size_t size) {
  SctpTx* tx = sctp->tx;
  uint32_t cumulative;
  uint32_t previous_cumulative;
  uint32_t highest_newly_acked = 0;
  uint16_t gaps;
  uint16_t duplicates;
  uint16_t previous = 0;
  size_t released = 0;
  size_t fast_count = 0;
  int have_newly_acked = 0;
  int recovery_progress;

  /* A gap ACK marks a packet as received but does not release its payload:
   * the peer may later renege and send a SACK without that gap.  Only the
   * cumulative ACK permanently retires descriptors. */
  if (tx == NULL || size < sizeof(*sack) || ntohs(sack->common.length) != size ||
      sack->common.type != SCTP_SACK || sack->common.flags != 0) {
    return;
  }
  cumulative = ntohl(sack->cumulative_tsn_ack);
  previous_cumulative = sctp->last_sack_tsn;
  gaps = ntohs(sack->number_of_gap_ack_blocks);
  duplicates = ntohs(sack->number_of_dup_tsns);
  if (sizeof(*sack) + (size_t)gaps * 4 + (size_t)duplicates * 4 != size ||
      !sctp_tsn_gte(cumulative, sctp->last_sack_tsn) ||
      !sctp_tsn_gte(tx->max_ack_tsn, cumulative)) {
    return;
  }
  for (size_t i = 0; i < gaps; ++i) {
    uint16_t start;
    uint16_t end;
    memcpy(&start, sack->blocks + i * 4, 2);
    memcpy(&end, sack->blocks + i * 4 + 2, 2);
    start = ntohs(start);
    end = ntohs(end);
    if (start <= previous || end < start ||
        !sctp_tsn_gte(tx->max_ack_tsn, cumulative + end)) {
      return;
    }
    previous = end;
  }
  if (tx->fast_recovery && sctp_tsn_gte(cumulative, tx->recovery_exit_tsn)) {
    tx->fast_recovery = 0;
  }
  recovery_progress = tx->fast_recovery && cumulative != previous_cumulative;
  if (cumulative != previous_cumulative) {
    highest_newly_acked = cumulative;
    have_newly_acked = 1;
  }
  sctp->last_sack_tsn = cumulative;
  while (tx->count != 0 && sctp_tsn_gte(cumulative, sctp_tx_packet(tx, 0)->tsn)) {
    sctp_tx_release(sctp_tx_packet(tx, 0));
    tx->head = (tx->head + 1) % tx->capacity;
    --tx->count;
    ++released;
  }
  for (size_t i = 0; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    uint32_t offset = packet->tsn - cumulative;
    uint8_t was_gap_acked = packet->gap_acked;
    packet->gap_acked = 0;
    for (size_t j = 0; j < gaps; ++j) {
      uint16_t start;
      uint16_t end;
      memcpy(&start, sack->blocks + j * 4, 2);
      memcpy(&end, sack->blocks + j * 4 + 2, 2);
      if (offset >= ntohs(start) && offset <= ntohs(end)) {
        packet->gap_acked = 1;
        if (!packet->gap_seen) {
          packet->gap_seen = 1;
          if (!have_newly_acked || sctp_tsn_gte(packet->tsn, highest_newly_acked)) {
            highest_newly_acked = packet->tsn;
            have_newly_acked = 1;
          }
        }
        break;
      }
    }
    packet->reneged = was_gap_acked && !packet->gap_acked;
    if (packet->gap_acked) {
      packet->miss_reports = 0;
      packet->fast_pending = 0;
    }
  }
  for (size_t i = 0; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    SctpTxMessage* message = packet->message;
    if (!packet->sent || packet->gap_acked || packet->fast_retransmitted ||
        message->abandoned || (message->policy == 1 && message->reliability == 0)) {
      continue;
    }
    /* RFC 9260 sections 6.2.1/7.2.4: reneging counts once; during recovery,
     * cumulative progress reports every hole below the highest Gap ACK. */
    if (!packet->reneged &&
        !(have_newly_acked && !sctp_tsn_gte(packet->tsn, highest_newly_acked)) &&
        !(recovery_progress && gaps && !sctp_tsn_gte(packet->tsn, cumulative + previous))) {
      continue;
    }
    if (++packet->miss_reports == SCTP_FAST_RETRANSMIT_MISS_REPORTS) {
      packet->fast_retransmitted = 1;
      packet->fast_pending = 1;
      packet->miss_reports = 0;
      ++fast_count;
    }
  }
  if (tx->forward_pending && sctp_tsn_gte(cumulative, tx->forward_tsn)) {
    tx->forward_pending = 0;
  }
  if (fast_count != 0) {
    if (!tx->fast_recovery) {
      tx->fast_recovery = 1;
      tx->recovery_exit_tsn = tx->max_ack_tsn;
    }
    /* RFC 4960 section 7.2.4: three valid HTNA missing reports trigger one
     * Fast Retransmit.  The marked packets bypass the periodic retry delay. */
    sctp_tx_run(sctp, 1);
  }
  if (gaps != 0 || duplicates != 0 || (released != 0 && tx->count == 0)) {
    SCTP_DIAG_LOG("tx sack cum=%lu gaps=%u dup=%u release=%u used=%u/%u",
                  (unsigned long)cumulative, (unsigned int)gaps,
                  (unsigned int)duplicates, (unsigned int)released,
                  (unsigned int)tx->count, (unsigned int)tx->capacity);
  }
}

static void sctp_tx_forward(Sctp* sctp, uint32_t now) {
  SctpTx* tx = sctp->tx;
  uint16_t sids[SCTP_MAX_STREAMS];
  uint16_t sqns[SCTP_MAX_STREAMS];
  size_t streams = 0;
  uint32_t cumulative = sctp->last_sack_tsn;
  SctpForwardTsnChunk* chunk;

  /* FORWARD_TSN can skip only a contiguous prefix of already-abandoned DATA.
   * A reliable descriptor, or an un-abandoned gap, blocks forwarding so a
   * reliable message is never silently skipped. */
  for (size_t i = 0; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    SctpTxMessage* message = packet->message;
    if (!message->abandoned || packet->tsn != cumulative + 1) {
      break;
    }
    if (!message->unordered) {
      size_t j;
      for (j = 0; j < streams && sids[j] != message->sid; ++j) {}
      if (j == streams) {
        if (streams == SCTP_MAX_STREAMS) {
          break;
        }
        sids[streams++] = message->sid;
      }
      sqns[j] = message->sqn;
    }
    cumulative = packet->tsn;
  }
  if (cumulative == sctp->last_sack_tsn ||
      (tx->forward_pending && cumulative == tx->forward_tsn &&
       now - tx->forward_ms < 100)) {
    return;
  }
  chunk = (SctpForwardTsnChunk*)(sctp->buf + sizeof(SctpHeader));
  chunk->common.type = SCTP_FORWARD_TSN;
  chunk->common.flags = 0;
  chunk->common.length = htons(sizeof(*chunk) + streams * 4);
  chunk->new_cumulative_tsn = htonl(cumulative);
  for (size_t i = 0; i < streams; ++i) {
    uint16_t sid = htons(sids[i]);
    uint16_t sqn = htons(sqns[i]);
    memcpy((uint8_t*)(chunk + 1) + i * 4, &sid, 2);
    memcpy((uint8_t*)(chunk + 1) + i * 4 + 2, &sqn, 2);
  }
  tx->forward_pending = 1;
  tx->forward_tsn = cumulative;
  tx->forward_ms = now;
  if (sctp_write_packet(sctp, sctp->buf,
                        sizeof(SctpHeader) + sizeof(*chunk) + streams * 4) >= 0 &&
      sctp_tsn_gte(cumulative, tx->max_ack_tsn)) {
    tx->max_ack_tsn = cumulative;
  }
}

static void sctp_tx_run(Sctp* sctp, int fast_only) {
  SctpTx* tx = sctp->tx;
  uint32_t now = ports_get_time_ms();
  uint32_t started = now;
  int retransmit;
  if (tx == NULL) {
    return;
  }
  /* Periodic polling checks every 150 ms.  A Fast Retransmit invokes this
   * loop for one immediate datagram; remaining marked TSNs stay pending
   * for polling without waiting for the ordinary 300 ms retry interval. */
  retransmit = !fast_only && now - tx->checked_ms >= 150;
  if (retransmit) {
    tx->checked_ms = now;
  }
  for (size_t i = 0; i < tx->count; ++i) {
    SctpTxPacket* packet = sctp_tx_packet(tx, i);
    SctpTxMessage* message = packet->message;
    int fast_retransmit = packet->fast_pending;
    now = ports_get_time_ms();
    if (message->abandoned || (fast_only && !fast_retransmit)) {
      continue;
    }
    if ((message->policy == 2 && now - message->created_ms >= message->reliability) ||
        (message->policy == 1 && message->reliability == 0 &&
         now - message->created_ms >= 100) ||
        (message->policy == 1 && message->reliability != 0 && !packet->gap_acked &&
         packet->retries >= message->reliability &&
         (fast_retransmit || now - packet->last_send_ms >= 300))) {
      SCTP_DIAG_LOG("tx abandon tsn=%lu policy=%u retry=%lu used=%u/%u",
                    (unsigned long)packet->tsn, (unsigned int)message->policy,
                    (unsigned long)packet->retries, (unsigned int)tx->count,
                    (unsigned int)tx->capacity);
      sctp_tx_abandon(tx, message);
      continue;
    }
    if (packet->gap_acked || (message->policy == 1 && message->reliability == 0) ||
        (!fast_retransmit &&
         (!retransmit || (packet->sent && now - packet->last_send_ms < 300)))) {
      continue;
    }
    if (now - started >= 15) {
      break;
    }
    packet->fast_pending = 0;
    int ret = sctp_tx_write(sctp, packet, NULL);
    if (ret >= 0) {
      if (packet->sent) {
        ++packet->retries;
      }
      packet->sent = 1;
    }
    SCTP_DIAG_LOG("tx %sretry tsn=%lu retry=%lu ret=%d used=%u/%u",
                  fast_retransmit ? "fast " : "", (unsigned long)packet->tsn,
                  (unsigned long)packet->retries,
                  ret, (unsigned int)tx->count, (unsigned int)tx->capacity);
    packet->last_send_ms = ports_get_time_ms();
    if (fast_only) {
      break;
    }
  }
  sctp_tx_forward(sctp, ports_get_time_ms());
}

void sctp_tx_poll(Sctp* sctp) {
  sctp_tx_run(sctp, 0);
}

#endif
