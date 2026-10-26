#include <stdlib.h>
#include <string.h>

#include "sctp_native.h"

#if !CONFIG_USE_USRSCTP

/*
 * The native RX path receives one DTLS plaintext net_buf from the peer loop.
 * A fragment normally stores only a pointer into that packet plus one retained
 * owner reference; the packet is therefore not copied while waiting for the
 * missing TSN.  The owner reference is released when the complete message is
 * delivered, abandoned, or the association is destroyed.
 *
 * The list is deliberately keyed by TSN rather than arrival order.  DATA
 * chunks can arrive in one packet or in several packets, and SCTP's cumulative
 * ACK can advance only across a contiguous TSN run.
 */
typedef struct SctpRxFragment {
  struct net_buf* meta_buf;     /* Fixed-pool owner for this fragment state. */
  struct SctpRxFragment* next;  /* Unordered linked list; lookup is by TSN. */
  struct net_buf* owner;        /* Retained packet reference, or an owned copy. */
  const uint8_t* data;          /* Payload inside owner; valid while owner is held. */
  size_t size;                  /* DATA payload length, excluding the 16-byte header. */
  size_t charge;                /* Accounted storage: owner capacity, not payload size. */
  uint32_t tsn;                 /* Wire TSN used for ordering, SACK and deduplication. */
  uint32_t ppid;                /* DATA channel PPID; part of the message identity. */
  uint32_t received_ms;         /* Used to expire unordered PR0 fragments. */
  uint16_t sid;                 /* Stream identity. */
  uint16_t sqn;                 /* Ordered stream sequence number. */
  uint8_t flags;                /* SCTP B/E/U bits: 0x02/0x01/0x04. */
  uint8_t discard;              /* Marked by FORWARD_TSN before unlinking safely. */
} SctpRxFragment;

struct SctpRx {
  struct net_buf_pool fragment_pool; /* Per-association pool, sized at initialization. */
  struct net_buf_pool coalesce_pool; /* One contiguous legacy delivery at a time. */
  void* coalesce_arena;         /* Fixed payload storage allocated only at initialization. */
  struct net_buf* delivery_buf; /* Keeps the pools alive if a callback destroys RX. */
  SctpRxFragment* fragments;    /* Retained but not yet delivered DATA fragments. */
  size_t bytes;                 /* Sum of charge for all retained fragments. */
  size_t count;                 /* Descriptor count guard for the TSN window. */
  size_t capacity;              /* Descriptor count derived from the receive budget. */
  uint32_t duplicates[16];      /* Duplicate TSNs included in the next SACK. */
  uint16_t duplicate_count;
  uint8_t ack_dirty;            /* Receipt state changed since the last SACK. */
  uint8_t forwarded;            /* A skipped BEGIN can leave late orphan tails. */
};

static int rx_received(Sctp* sctp, uint32_t tsn) {
  /* A fixed 256-bit receive window makes duplicate/received checks O(1) and
   * naturally handles uint32_t TSN wrap through modulo indexing. */
  uint32_t index = tsn % SCTP_RECEIVE_TSN_WINDOW;
  return (sctp->recv_tsn_map[index / 32U] & (1U << (index % 32U))) != 0;
}

static void rx_clear(Sctp* sctp, uint32_t tsn) {
  uint32_t index = tsn % SCTP_RECEIVE_TSN_WINDOW;
  if (!sctp_tsn_gte(sctp->recv_cumulative_tsn, tsn)) {
    if (rx_received(sctp, tsn)) sctp->rx->ack_dirty = 1;
    sctp->recv_tsn_map[index / 32U] &= ~(1U << (index % 32U));
  }
}

static void rx_advance(Sctp* sctp, uint32_t cumulative) {
  uint32_t distance = cumulative - sctp->recv_cumulative_tsn;
  uint32_t i;
  /* Clear bits covered by the new cumulative ACK, then consume any immediately
   * following received bits.  A hole therefore remains visible in SACK gaps. */
  if (distance >= SCTP_RECEIVE_TSN_WINDOW) {
    memset(sctp->recv_tsn_map, 0, sizeof(sctp->recv_tsn_map));
  } else {
    for (i = 1; i <= distance; i++) {
      uint32_t index = (sctp->recv_cumulative_tsn + i) % SCTP_RECEIVE_TSN_WINDOW;
      sctp->recv_tsn_map[index / 32U] &= ~(1U << (index % 32U));
    }
  }
  sctp->recv_cumulative_tsn = cumulative;
  while (rx_received(sctp, sctp->recv_cumulative_tsn + 1U)) {
    uint32_t index = ++sctp->recv_cumulative_tsn % SCTP_RECEIVE_TSN_WINDOW;
    sctp->recv_tsn_map[index / 32U] &= ~(1U << (index % 32U));
  }
}

static void rx_ack(Sctp* sctp, uint32_t tsn) {
  uint32_t index = tsn % SCTP_RECEIVE_TSN_WINDOW;
  /* Receipt is recorded separately from cumulative advancement.  This permits
   * a SACK to advertise later TSNs while retaining the missing lower TSN. */
  if (!sctp_tsn_gte(sctp->recv_cumulative_tsn, tsn)) {
    sctp->recv_tsn_map[index / 32U] |= 1U << (index % 32U);
    sctp->rx->ack_dirty = 1;
  }
}

static SctpRxFragment* rx_find(Sctp* sctp, uint32_t tsn) {
  SctpRxFragment* fragment;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    if (fragment->tsn == tsn) return fragment;
  }
  return NULL;
}

static size_t rx_owner_charge(Sctp* sctp, struct net_buf* owner) {
  SctpRxFragment* fragment;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    if (fragment->owner == owner) return 0;
  }
  return owner->size;
}

static int rx_same_message(const SctpRxFragment* first, const SctpRxFragment* next) {
  /* Classic DATA has no message identifier.  The B/E flags, stream, PPID and
   * ordered SSN are the available identity checks; U=1 intentionally ignores
   * SSN because unordered channels do not sequence messages by SSN. */
  return next && first->sid == next->sid && first->ppid == next->ppid &&
         ((first->flags ^ next->flags) & 4U) == 0 &&
         ((first->flags & 4U) || first->sqn == next->sqn);
}

static void rx_remove(Sctp* sctp, SctpRxFragment* fragment, int revoke) {
  SctpRxFragment** link = &sctp->rx->fragments;
  while (*link && *link != fragment) link = &(*link)->next;
  if (!*link) return;
  *link = fragment->next;
  /* A bundled packet remains allocated until its last retained fragment leaves. */
  if (fragment->charge) {
    SctpRxFragment* shared;
    for (shared = sctp->rx->fragments; shared; shared = shared->next) {
      if (shared->owner == fragment->owner) {
        shared->charge += fragment->charge;
        fragment->charge = 0;
        break;
      }
    }
  }
  /* revoke=1 is used only when reneging/abandoning a gap-ACKed fragment.  A
   * cumulatively ACKed TSN cannot be revoked because the peer will not resend
   * it. */
  if (revoke) rx_clear(sctp, fragment->tsn);
  sctp->rx->bytes -= fragment->charge;
  sctp->rx->ack_dirty = 1;
  sctp->rx->count--;
  if (revoke) {
    SCTP_DIAG_LOG("rx revoke tsn=%lu used=%u bytes=%u/%u",
                  (unsigned long)fragment->tsn, (unsigned int)sctp->rx->count,
                  (unsigned int)sctp->rx->bytes,
                  (unsigned int)sctp->recv_cache_limit);
  }
  net_buf_unref(fragment->owner);
  net_buf_unref(fragment->meta_buf);
}

static int rx_revoke_highest(Sctp* sctp, uint32_t tsn) {
  SctpRxFragment* fragment;
  SctpRxFragment* highest = NULL;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    if (sctp_tsn_gte(tsn, fragment->tsn) ||
        sctp_tsn_gte(sctp->recv_cumulative_tsn, fragment->tsn)) continue;
    if (!highest || sctp_tsn_gte(fragment->tsn, highest->tsn)) highest = fragment;
  }
  if (!highest) return 0;
  /* Only higher gap-ACKed fragments may be reneged; cumulative ACKs are final. */
  rx_remove(sctp, highest, 1);
  return 1;
}

static int rx_make_room(Sctp* sctp, size_t charge, uint32_t tsn) {
  if (charge > sctp->recv_cache_limit) return 0;
  while (charge > sctp->recv_cache_limit - sctp->rx->bytes ||
         sctp->rx->count >= sctp->rx->capacity ||
         sctp->rx->count >= SCTP_RECEIVE_TSN_WINDOW) {
    if (!rx_revoke_highest(sctp, tsn)) return 0;
  }
  return 1;
}

static SctpRxFragment* rx_previous_in_stream(Sctp* sctp, SctpRxFragment* current) {
  SctpRxFragment* fragment;
  SctpRxFragment* previous = NULL;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    if (fragment->sid != current->sid || fragment == current ||
        !sctp_tsn_gte(current->tsn, fragment->tsn)) continue;
    if (!previous || sctp_tsn_gte(fragment->tsn, previous->tsn)) previous = fragment;
  }
  return previous;
}

static int rx_message_too_large(Sctp* sctp, SctpRxFragment* incoming) {
  SctpRxFragment* first = incoming;
  SctpRxFragment* fragment;
  SctpRxFragment* previous;
  size_t total = 0;
  size_t limit = sctp_receive_message_limit(sctp);

  /* Check the contiguous message prefix that would exist after accepting the
   * incoming fragment.  Rejecting before storing/ACKing it prevents an
   * oversized message from occupying the receive cache forever. */
  while (!(first->flags & 2U) && (previous = rx_find(sctp, first->tsn - 1U)) != NULL &&
         !(previous->flags & 1U) && rx_same_message(first, previous)) first = previous;
  for (fragment = first;;) {
    if (fragment->size > limit - total) return 1;
    total += fragment->size;
    if (fragment->flags & 1U) return 0;
    fragment = fragment->tsn + 1U == incoming->tsn ? incoming :
               rx_find(sctp, fragment->tsn + 1U);
    if (!rx_same_message(first, fragment) || (fragment->flags & 2U)) return 0;
  }
}

static void rx_storage_failed(Sctp* sctp, uint32_t tsn) {
  SctpRxFragment* fragment;

  SCTP_DIAG_LOG("rx storage fail tsn=%lu used=%u bytes=%u/%u",
                (unsigned long)tsn, (unsigned int)sctp->rx->count,
                (unsigned int)sctp->rx->bytes,
                (unsigned int)sctp->recv_cache_limit);
  if (tsn != sctp->recv_cumulative_tsn + 1U) return;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    if (sctp_tsn_gte(sctp->recv_cumulative_tsn, fragment->tsn)) {
      /* The lowest missing TSN cannot be stored while an incomplete message
       * already contains irreversible cumulative-ACKed data.  Silently
       * dropping it would deadlock reassembly, so close the association with
       * the SCTP resource cause instead of claiming an ACK. */
      sctp_abort(sctp, 4);
      return;
    }
  }
}

/* Classic DATA fragments occupy consecutive TSNs; I-DATA is not negotiated.
 * A message is deliverable only after B..E is present and all TSNs between
 * those markers are present in the cache. */
static SctpRxFragment* rx_message_end(Sctp* sctp, SctpRxFragment* first, size_t* size) {
  SctpRxFragment* fragment = first;
  size_t total = 0;
  if (!(first->flags & 2U)) return NULL;
  for (;;) {
    if (fragment->size > sctp->recv_cache_limit - total) return NULL;
    total += fragment->size;
    if (fragment->flags & 1U) {
      *size = total;
      return fragment;
    }
    fragment = rx_find(sctp, fragment->tsn + 1U);
    if (!rx_same_message(first, fragment) || (fragment->flags & 2U)) return NULL;
  }
}

static void rx_free(SctpRx* rx) {
  net_buf_pool_deinit(&rx->coalesce_pool);
  net_buf_pool_deinit(&rx->fragment_pool);
  free(rx->coalesce_arena);
  free(rx);
}

static int rx_deliver(Sctp* sctp, SctpRxFragment* first, SctpRxFragment* last, size_t size) {
  SctpRx* rx = sctp->rx;
  SctpStreamEntry* stream = sctp_get_stream(sctp, first->sid);
  SctpRxFragment* fragment;
  struct net_buf* contiguous = NULL;
  const uint8_t* data = first->data;
  int fragment_callback = first->ppid == PPID_BINARY && (first->flags & 4U) &&
                          stream && sctp->onfragment;
  int ret = 0;

  if (first != last) {
    SCTP_DIAG_LOG("rx deliver tsn=%lu..%lu size=%u used=%u bytes=%u/%u",
                  (unsigned long)first->tsn, (unsigned long)last->tsn,
                  (unsigned int)size, (unsigned int)rx->count,
                  (unsigned int)rx->bytes,
                  (unsigned int)sctp->recv_cache_limit);
  }

  /* B..E is complete here, so retransmission policy does not affect whether
   * unordered binary messages can expose their owner-backed spans directly. */
  if (!(first->flags & 4U) && first->sqn != (stream ? stream->recv_sqn : 0)) return 0;
  if (!fragment_callback && first != last && (first->ppid == PPID_CONTROL || sctp->onmessage)) {
    /* The legacy onmessage API accepts one contiguous pointer.  This is the
     * only RX copy for a multi-fragment message; the fragment callback avoids
     * it by exposing each owner-backed span consecutively. */
    contiguous = net_buf_alloc(&rx->coalesce_pool, K_NO_WAIT);
    if (!contiguous) return 0;
    if (size > contiguous->size) {
      net_buf_unref(contiguous);
      return 0;
    }
    for (fragment = first;; fragment = rx_find(sctp, fragment->tsn + 1U)) {
      net_buf_add_mem(contiguous, fragment->data, fragment->size);
      if (fragment == last) break;
    }
    data = contiguous->data;
    rx->delivery_buf = contiguous;
  }
  if (first->ppid == PPID_CONTROL) {
    ret = sctp_handle_dcep(sctp, first->sid, data, size);
  } else if (fragment_callback) {
    for (fragment = first;; fragment = rx_find(sctp, fragment->tsn + 1U)) {
      /* The pointer is borrowed for this callback only.  A callback may close
       * the association, so verify the RX instance before touching the list. */
      struct net_buf* owner = net_buf_ref(fragment->owner);
      sctp->onfragment((char*)fragment->data, fragment->size, sctp->userdata,
                       fragment->sid, fragment->tsn, fragment->flags);
      net_buf_unref(owner);
      if (sctp->rx != rx) return 0;
      if (fragment == last) break;
    }
  } else if (sctp->onmessage) {
    sctp->onmessage((char*)data,
                    first->ppid == PPID_STRING_EMPTY || first->ppid == PPID_BINARY_EMPTY ? 0 : size,
                    sctp->userdata, first->sid);
  }
  if (contiguous) {
    net_buf_unref(contiguous);
    rx->delivery_buf = NULL;
    if (sctp->rx != rx) {
      rx_free(rx);
      return 0;
    }
  }
  if (sctp->rx != rx) return 0;
  if (ret < 0) return 0;
  stream = sctp_get_stream(sctp, first->sid);
  if (!(first->flags & 4U) && stream) stream->recv_sqn++;
  /* Only successful delivery ACKs the message.  DCEP failures and allocation
   * failures leave the fragments unacknowledged so the peer can retry. */
  for (fragment = first;;) {
    uint32_t next = fragment->tsn + 1U;
    int done = fragment == last;
    rx_ack(sctp, fragment->tsn);
    rx_remove(sctp, fragment, 0);
    if (done) break;
    fragment = rx_find(sctp, next);
  }
  rx_advance(sctp, sctp->recv_cumulative_tsn);
  return 1;
}

static void rx_discard_orphan_tails(Sctp* sctp) {
  SctpRxFragment* fragment;
  SctpRxFragment* oldest = sctp->rx->fragments;
  int single_stream = 1;
  int has_end = 0;
  if (!oldest || !sctp->rx->forwarded) return;
  for (fragment = oldest; fragment; fragment = fragment->next) {
    if (fragment->sid != oldest->sid) single_stream = 0;
    if (fragment->flags & 1U) has_end = 1;
    if (sctp_tsn_gte(oldest->tsn, fragment->tsn)) oldest = fragment;
  }
  /* The normal single-stream partial message has its BEGIN and no END yet. */
  if (single_stream && !has_end && (oldest->flags & 2U)) return;
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    SctpRxFragment* previous;
    SctpRxFragment* boundary = NULL;
    if ((fragment->flags & 2U) || !sctp_tsn_gte(sctp->recv_cumulative_tsn, fragment->tsn)) continue;
    for (previous = sctp->rx->fragments; previous; previous = previous->next) {
      if (previous == fragment || previous->sid != fragment->sid ||
          !(previous->flags & 3U) || !sctp_tsn_gte(fragment->tsn, previous->tsn)) continue;
      if (!boundary || sctp_tsn_gte(previous->tsn, boundary->tsn)) boundary = previous;
    }
    /* A missing BEGIN below the cumulative ACK was skipped, so it cannot arrive. */
    if (!boundary || (boundary->flags & 3U) != 2U || !rx_same_message(boundary, fragment)) {
      fragment->discard = 1;
    }
  }
  fragment = sctp->rx->fragments;
  while (fragment) {
    SctpRxFragment* next = fragment->next;
    if (fragment->discard) rx_remove(sctp, fragment, 0);
    fragment = next;
  }
}

static void rx_drain(Sctp* sctp) {
  SctpRx* rx = sctp->rx;
  SctpRxFragment* first;
  rx_discard_orphan_tails(sctp);
  first = sctp->rx->fragments;
  /* Re-enter after each delivery because callbacks may send data, close the
   * association, or mutate the stream table. */
  while (first) {
    size_t size;
    SctpRxFragment* last = rx_message_end(sctp, first, &size);
    int delivered = last && rx_deliver(sctp, first, last, size);
    if (sctp->rx != rx) return;
    if (delivered) {
      first = sctp->rx->fragments;
    } else {
      first = first->next;
    }
  }
}

int sctp_rx_init(Sctp* sctp) {
  size_t fragment_capacity;
  size_t arena_size;
  size_t coalesce_size;
  size_t message_limit;
  if (!sctp->recv_cache_limit) sctp->recv_cache_limit = 32768U;
  fragment_capacity = sctp->recv_cache_limit / CONFIG_MTU +
                      (sctp->recv_cache_limit % CONFIG_MTU != 0) + 2U;
  if (fragment_capacity > UINT16_MAX) return -1;
  arena_size = net_buf_pool_arena_size(fragment_capacity, sizeof(SctpRxFragment), 0);
  if (!arena_size || arena_size > SIZE_MAX - sizeof(*sctp->rx)) return -1;
  /* Allocate descriptors and queue storage once; DATA RX only takes/returns slots. */
  sctp->rx = calloc(1, sizeof(*sctp->rx) + arena_size);
  if (!sctp->rx) return -1;
  if (net_buf_pool_init(&sctp->rx->fragment_pool, sctp->rx + 1, arena_size,
                       fragment_capacity, sizeof(SctpRxFragment), 0, NULL) < 0) {
    free(sctp->rx);
    sctp->rx = NULL;
    return -1;
  }
  message_limit = sctp_receive_message_limit(sctp);
  coalesce_size = net_buf_pool_arena_size(1, message_limit ? message_limit : 1U, 0);
  sctp->rx->coalesce_arena = calloc(1, coalesce_size);
  if (!sctp->rx->coalesce_arena ||
      net_buf_pool_init(&sctp->rx->coalesce_pool, sctp->rx->coalesce_arena,
                        coalesce_size, 1, message_limit ? message_limit : 1U, 0, NULL) < 0) {
    rx_free(sctp->rx);
    sctp->rx = NULL;
    return -1;
  }
  sctp->rx->capacity = fragment_capacity;
  SCTP_DIAG_LOG("rx init cache=%u fragments=%u",
                (unsigned int)sctp->recv_cache_limit,
                (unsigned int)fragment_capacity);
  return 0;
}

void sctp_rx_destroy(Sctp* sctp) {
  SctpRx* rx = sctp->rx;
  if (!rx) return;
  while (rx->fragments) rx_remove(sctp, rx->fragments, 0);
  sctp->rx = NULL;
  /* A legacy callback may close RX and still read its borrowed payload. */
  if (!rx->delivery_buf) rx_free(rx);
}

void sctp_rx_reset(Sctp* sctp, uint32_t cumulative, uint8_t initialized) {
  /* A new association starts a new TSN space.  Retained fragments and old
   * duplicate bits must not leak into the next SDP/DTLS session. */
  if (sctp->rx) {
    while (sctp->rx->fragments) rx_remove(sctp, sctp->rx->fragments, 0);
    sctp->rx->duplicate_count = 0;
    sctp->rx->ack_dirty = 0;
    sctp->rx->forwarded = 0;
  }
  memset(sctp->recv_tsn_map, 0, sizeof(sctp->recv_tsn_map));
  sctp->recv_cumulative_tsn = cumulative;
  sctp->recv_initialized = initialized;
}

void sctp_rx_data(Sctp* sctp, const SctpDataChunk* chunk, size_t length, struct net_buf* owner) {
  SctpRx* rx = sctp->rx;
  SctpRxFragment incoming;
  SctpRxFragment* stored;
  struct net_buf* meta_buf;
  SctpStreamEntry* stream;
  size_t charge;

  /* owner is borrowed from sctp_input.  This function takes a reference only
   * when it must retain the fragment beyond the caller's packet lifetime. */
  if (!sctp->rx || !sctp->recv_initialized || length < sizeof(*chunk) || (chunk->iube & ~15U)) return;

  memset(&incoming, 0, sizeof(incoming));
  incoming.data = chunk->data;
  incoming.size = length - sizeof(*chunk);
  incoming.tsn = ntohl(chunk->tsn);
  incoming.ppid = ntohl(chunk->ppid);
  incoming.sid = ntohs(chunk->sid);
  incoming.sqn = ntohs(chunk->sqn);
  incoming.flags = chunk->iube;
  incoming.received_ms = ports_get_time_ms();
  if (incoming.ppid != PPID_CONTROL && incoming.ppid != PPID_STRING && incoming.ppid != PPID_BINARY &&
      incoming.ppid != PPID_STRING_EMPTY && incoming.ppid != PPID_BINARY_EMPTY) return;
  /* New TSNs outside the window can alias received bits in the circular bitmap. */
  if (!sctp_tsn_gte(sctp->recv_cumulative_tsn, incoming.tsn) &&
      incoming.tsn - sctp->recv_cumulative_tsn > SCTP_RECEIVE_TSN_WINDOW) {
    SCTP_DIAG_LOG("rx window drop tsn=%lu cum=%lu",
                  (unsigned long)incoming.tsn,
                  (unsigned long)sctp->recv_cumulative_tsn);
    return;
  }
  if (sctp_tsn_gte(sctp->recv_cumulative_tsn, incoming.tsn) || rx_received(sctp, incoming.tsn)) {
    if (sctp->rx->duplicate_count < 16U) {
      sctp->rx->duplicates[sctp->rx->duplicate_count++] = incoming.tsn;
    }
    sctp->rx->ack_dirty = 1;
    SCTP_DIAG_LOG("rx duplicate tsn=%lu cum=%lu",
                  (unsigned long)incoming.tsn,
                  (unsigned long)sctp->recv_cumulative_tsn);
    return;
  }
  if (rx_find(sctp, incoming.tsn)) {
    SCTP_DIAG_LOG("rx held duplicate tsn=%lu", (unsigned long)incoming.tsn);
    rx_drain(sctp);
    return;
  }
  if (sctp_tsn_gte(incoming.tsn, sctp->recv_cumulative_tsn + 2U)) {
    SCTP_DIAG_LOG("rx gap tsn=%lu cum=%lu",
                  (unsigned long)incoming.tsn,
                  (unsigned long)sctp->recv_cumulative_tsn);
  }
  stream = sctp_get_stream(sctp, incoming.sid);
  if (!stream && incoming.ppid != PPID_CONTROL) return;
  if (rx_message_too_large(sctp, &incoming)) {
    sctp_abort(sctp, 13);
    return;
  }
  if ((incoming.flags & 3U) == 3U &&
      ((incoming.flags & 4U) || incoming.sqn == (stream ? stream->recv_sqn : 0))) {
    int ret = 0;
    if (incoming.ppid == PPID_CONTROL) {
      ret = sctp_handle_dcep(sctp, incoming.sid, incoming.data, incoming.size);
    } else if (incoming.ppid == PPID_BINARY && (incoming.flags & 4U) &&
               sctp->onfragment) {
      sctp->onfragment((char*)incoming.data, incoming.size, sctp->userdata,
                       incoming.sid, incoming.tsn, incoming.flags);
    } else if (sctp->onmessage) {
      sctp->onmessage((char*)incoming.data,
                      incoming.ppid == PPID_STRING_EMPTY || incoming.ppid == PPID_BINARY_EMPTY ? 0 : incoming.size,
                      sctp->userdata, incoming.sid);
    }
    if (sctp->rx != rx || ret < 0) return;
    stream = sctp_get_stream(sctp, incoming.sid);
    if (!(incoming.flags & 4U) && stream) stream->recv_sqn++;
    rx_ack(sctp, incoming.tsn);
    rx_advance(sctp, sctp->recv_cumulative_tsn);
    rx_drain(sctp);
    return;
  }
  if (!(incoming.flags & 4U) && stream && (int16_t)(incoming.sqn - stream->recv_sqn) < 0) {
    rx_ack(sctp, incoming.tsn);
    rx_advance(sctp, sctp->recv_cumulative_tsn);
    return;
  }
  /* A DTLS packet owner is charged once at its full capacity because stored
   * fragments keep that allocation alive, even when DATA payloads are small.
   * The raw-pointer compatibility path allocates a private owner instead. */
  charge = owner ? rx_owner_charge(sctp, owner) : incoming.size;
  if (!rx_make_room(sctp, charge, incoming.tsn)) {
    rx_storage_failed(sctp, incoming.tsn);
    return;
  }
  meta_buf = net_buf_alloc(&sctp->rx->fragment_pool, K_NO_WAIT);
  if (!meta_buf) {
    SCTP_DIAG_LOG("rx meta pool empty tsn=%lu used=%u/%u",
                  (unsigned long)incoming.tsn, (unsigned int)sctp->rx->count,
                  (unsigned int)sctp->rx->capacity);
    rx_storage_failed(sctp, incoming.tsn);
    return;
  }
  stored = (SctpRxFragment*)meta_buf->data;
  memset(stored, 0, sizeof(*stored));
  *stored = incoming;
  stored->meta_buf = meta_buf;
  if (owner) {
    stored->owner = net_buf_ref(owner);
  } else {
    stored->owner = sctp_packet_alloc(sctp, incoming.size ? incoming.size : 1U);
    /* TX shares this fixed pool: it can fill before the RX byte budget does.
     * Revoke later gaps until a slot is freed, including shared packet owners. */
    while (!stored->owner && rx_revoke_highest(sctp, incoming.tsn)) {
      stored->owner = sctp_packet_alloc(sctp, incoming.size ? incoming.size : 1U);
    }
    if (stored->owner) {
      charge = stored->owner->size;
      if (!rx_make_room(sctp, charge, incoming.tsn)) {
        net_buf_unref(stored->owner);
        net_buf_unref(meta_buf);
        rx_storage_failed(sctp, incoming.tsn);
        return;
      }
      stored->data = net_buf_add_mem(stored->owner, incoming.data, incoming.size);
    }
  }
  if (!stored->owner) {
    SCTP_DIAG_LOG("rx packet pool empty tsn=%lu", (unsigned long)incoming.tsn);
    net_buf_unref(meta_buf);
    rx_storage_failed(sctp, incoming.tsn);
    return;
  }
  /* Making room may have evicted the owner's last previously retained fragment. */
  charge = rx_owner_charge(sctp, stored->owner);
  stored->charge = charge;
  stored->next = sctp->rx->fragments;
  sctp->rx->fragments = stored;
  sctp->rx->bytes += charge;
  sctp->rx->count++;
  if (incoming.tsn != sctp->recv_cumulative_tsn + 1U) {
    SCTP_DIAG_LOG("rx hold tsn=%lu size=%u used=%u bytes=%u/%u",
                  (unsigned long)incoming.tsn, (unsigned int)incoming.size,
                  (unsigned int)sctp->rx->count,
                  (unsigned int)sctp->rx->bytes,
                  (unsigned int)sctp->recv_cache_limit);
  }
  /* Control DATA is ACKed only after DCEP validation and ACK queuing. */
  if (incoming.ppid != PPID_CONTROL) {
    rx_ack(sctp, incoming.tsn);
    rx_advance(sctp, sctp->recv_cumulative_tsn);
  }
  rx_drain(sctp);
}

void sctp_rx_sack(Sctp* sctp) {
  SctpPacket* packet = (SctpPacket*)sctp->buf;
  SctpSackChunk* sack = (SctpSackChunk*)packet->chunks;
  size_t length = sizeof(*packet) + sizeof(*sack);
  uint16_t gaps = 0;
#if CONFIG_EDGERTC_SCTP_DEBUG
  uint16_t first_gap_start = 0;
  uint16_t first_gap_end = 0;
#endif
  uint16_t duplicates = 0;
  uint32_t offset = 1;
  if (!sctp->rx || !sctp->recv_initialized || length > sizeof(sctp->buf)) return;
  memset(sctp->buf, 0, length);
  /* Serialize the bitmap as RFC 4960 gap blocks relative to the cumulative
   * TSN.  Duplicate TSNs are reported once and cleared only after a successful
   * DTLS write. */
  while (offset <= SCTP_RECEIVE_TSN_WINDOW) {
    uint16_t block[2];
    if (!rx_received(sctp, sctp->recv_cumulative_tsn + offset)) {
      offset++;
      continue;
    }
    block[0] = htons(offset);
    do { offset++; } while (offset <= SCTP_RECEIVE_TSN_WINDOW &&
                             rx_received(sctp, sctp->recv_cumulative_tsn + offset));
    block[1] = htons(offset - 1U);
    if (length + sizeof(block) > sizeof(sctp->buf)) break;
    memcpy(sctp->buf + length, block, sizeof(block));
    length += sizeof(block);
#if CONFIG_EDGERTC_SCTP_DEBUG
    if (gaps == 0) {
      first_gap_start = ntohs(block[0]);
      first_gap_end = ntohs(block[1]);
    }
#endif
    gaps++;
  }
  while (duplicates < sctp->rx->duplicate_count && length + 4U <= sizeof(sctp->buf)) {
    uint32_t tsn = htonl(sctp->rx->duplicates[duplicates++]);
    memcpy(sctp->buf + length, &tsn, sizeof(tsn));
    length += sizeof(tsn);
  }
  sack->common.type = SCTP_SACK;
  sack->common.length = htons(length - sizeof(*packet));
  sack->cumulative_tsn_ack = htonl(sctp->recv_cumulative_tsn);
  sack->a_rwnd = htonl(sctp->recv_cache_limit - sctp->rx->bytes);
  sack->number_of_gap_ack_blocks = htons(gaps);
  sack->number_of_dup_tsns = htons(duplicates);
  if (sctp_write_packet(sctp, sctp->buf, length) >= 0) {
    sctp->rx->duplicate_count = 0;
    sctp->rx->ack_dirty = 0;
  }
  if (gaps != 0 || duplicates != 0) {
    SCTP_DIAG_LOG("rx sack cum=%lu gaps=%u first=%u..%u dup=%u used=%u bytes=%u/%u",
                  (unsigned long)sctp->recv_cumulative_tsn,
                  (unsigned int)gaps, (unsigned int)first_gap_start,
                  (unsigned int)first_gap_end, (unsigned int)duplicates,
                  (unsigned int)sctp->rx->count,
                  (unsigned int)sctp->rx->bytes,
                  (unsigned int)sctp->recv_cache_limit);
  }
}

void sctp_rx_forward(Sctp* sctp, const SctpForwardTsnChunk* chunk, size_t length) {
  uint32_t cumulative;
  uint32_t previous_cumulative;
  SctpRxFragment* fragment;
  size_t offset;
  if (!sctp->rx || !sctp->recv_initialized || !sctp->peer_supports_forward_tsn ||
      length < sizeof(*chunk) || (length - sizeof(*chunk)) % 4U) return;
  cumulative = ntohl(chunk->new_cumulative_tsn);
  if (!sctp_tsn_gte(cumulative, sctp->recv_cumulative_tsn)) return;
  previous_cumulative = sctp->recv_cumulative_tsn;
  sctp->rx->forwarded = 1;
  /* FORWARD_TSN advances ordered SSN state and discards only incomplete
   * messages covered by the new cumulative TSN.  A complete message already
   * ready for delivery is kept so forwarding cannot drop valid application
   * data. */
  for (offset = sizeof(*chunk); offset < length; offset += 4U) {
    uint16_t pair[2];
    SctpStreamEntry* stream;
    memcpy(pair, (const uint8_t*)chunk + offset, sizeof(pair));
    stream = sctp_get_stream(sctp, ntohs(pair[0]));
    if (stream && (int16_t)(ntohs(pair[1]) - stream->recv_sqn) >= 0) {
      stream->recv_sqn = ntohs(pair[1]) + 1U;
    }
  }
  for (fragment = sctp->rx->fragments; fragment; fragment = fragment->next) {
    SctpRxFragment* first = fragment;
    SctpRxFragment* previous;
    SctpRxFragment* last;
    SctpStreamEntry* stream = sctp_get_stream(sctp, fragment->sid);
    size_t size;
    if (!(fragment->flags & 4U) && stream &&
        (int16_t)(fragment->sqn - stream->recv_sqn) < 0) {
      fragment->discard = 1;
      continue;
    }
    while (!(first->flags & 2U) &&
           (previous = rx_previous_in_stream(sctp, first)) != NULL &&
           !(previous->flags & 1U) && rx_same_message(first, previous)) first = previous;
    last = rx_message_end(sctp, first, &size);
    if (!last && (sctp_tsn_gte(cumulative, first->tsn) ||
                  (!(first->flags & 2U) && first->tsn == cumulative + 1U))) {
      fragment->discard = 1;
    }
  }
  fragment = sctp->rx->fragments;
  while (fragment) {
    SctpRxFragment* next = fragment->next;
    /* Dropping payload must retain tail receipts beyond the FORWARD point. */
    if (fragment->discard) rx_remove(sctp, fragment, 0);
    fragment = next;
  }
  rx_advance(sctp, cumulative);
  if (sctp->recv_cumulative_tsn != previous_cumulative) {
    SCTP_DIAG_LOG("rx forward old=%lu requested=%lu cum=%lu",
                  (unsigned long)previous_cumulative, (unsigned long)cumulative,
                  (unsigned long)sctp->recv_cumulative_tsn);
  }
  sctp->rx->ack_dirty = 1;
  rx_drain(sctp);
}

void sctp_rx_poll(Sctp* sctp) {
  SctpRx* rx = sctp->rx;
  SctpRxFragment* fragment;
  uint32_t now = ports_get_time_ms();
  if (!sctp->rx) return;
  rx_drain(sctp);
  if (sctp->rx != rx) return;
  fragment = sctp->rx->fragments;
  while (fragment) {
    SctpRxFragment* next = fragment->next;
    SctpStreamEntry* stream = sctp_get_stream(sctp, fragment->sid);
    /* Unordered PR0 has no retransmission opportunity.  Expire a partial
     * message after 5 s and revoke its gap ACK so later traffic can progress. */
    if (stream && stream->no_retransmit && fragment->ppid != PPID_CONTROL &&
        now - fragment->received_ms >= 5000U) {
      rx_remove(sctp, fragment, 1);
    }
    fragment = next;
  }
  if (sctp->rx->ack_dirty) sctp_rx_sack(sctp);
}

#endif
