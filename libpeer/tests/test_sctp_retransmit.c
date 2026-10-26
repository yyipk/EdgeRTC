#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t heap_allocations;

static void* counted_malloc(size_t size) {
  heap_allocations++;
  return malloc(size);
}

static void* counted_calloc(size_t count, size_t size) {
  heap_allocations++;
  return calloc(count, size);
}

/* Substitute transport and owned allocation; execute the actual protocol. */
#define malloc counted_malloc
#define calloc counted_calloc
#define DTLS_SRTP_H_
typedef enum { DTLS_SRTP_ROLE_CLIENT, DTLS_SRTP_ROLE_SERVER } DtlsSrtpRole;
typedef struct { DtlsSrtpRole role; } DtlsSrtp;
int dtls_srtp_write(DtlsSrtp* dtls, const uint8_t* data, size_t size);
#include "../src/sctp.c"
#include "../src/sctp_tx.c"
#include "../src/sctp_rx.c"
#include "../src/sdp.c"
#undef malloc
#undef calloc

static uint32_t now_ms = 1000;
static size_t sends, forwards, messages, fragments;
static uint32_t sent_tsn[1024], forward_tsn;
static const uint8_t* expected_fragment[3];
static uint8_t received[32];
static size_t received_size;
static int fail_send;
static int check_payload;
static uint32_t write_delay_ms;
static DtlsSrtp transport;
static uint8_t wire_type[1024], wire_bytes[1024][64];
static size_t wire_size[1024], wire_count;
static size_t opens;
static size_t closes;
static uint16_t abort_cause;
static int network_active;
static int network_transmit(DtlsSrtp* dtls, const uint8_t* data, size_t size);
NET_BUF_POOL_FIXED_DEFINE(test_owner_pool, 2, 2 * SCTP_MTU, 0, NULL);
uint32_t ports_get_time_ms(void) { return now_ms; }
uint32_t ports_get_epoch_time(void) { return now_ms; }

int dtls_srtp_write(DtlsSrtp* dtls, const uint8_t* data, size_t size) {
  if (network_active) return network_transmit(dtls, data, size);
  const SctpChunkCommon* chunk = (const void*)(data + sizeof(SctpHeader));
  (void)dtls;
  now_ms += write_delay_ms;
  assert(size >= sizeof(SctpHeader) + sizeof(*chunk));
  assert(wire_count < 1024);
  wire_type[wire_count] = chunk->type;
  wire_size[wire_count] = size;
  memcpy(wire_bytes[wire_count], data, size < 64 ? size : 64);
  wire_count++;
  if (chunk->type == SCTP_DATA) {
    assert(sends < sizeof(sent_tsn) / sizeof(sent_tsn[0]));
    sent_tsn[sends++] = ntohl(((const SctpDataChunk*)chunk)->tsn);
    if (check_payload) {
      const SctpDataChunk* d = (const void*)chunk;
      for (size_t i = 0; i < ntohs(d->length) - sizeof(*d); ++i) {
        assert(d->data[i] == 0x5a);
      }
    }
  } else if (chunk->type == SCTP_FORWARD_TSN) {
    forward_tsn = ntohl(((const SctpForwardTsnChunk*)chunk)->new_cumulative_tsn);
    forwards++;
  } else if (chunk->type == SCTP_ABORT) {
    uint16_t cause;
    assert(size == sizeof(SctpHeader) + sizeof(*chunk) + 4);
    memcpy(&cause, chunk + 1, sizeof(cause));
    abort_cause = ntohs(cause);
  }
  if (fail_send) { fail_send = 0; return -1; }
  return (int)size;
}

static void init(Sctp* s, uint8_t channel, uint32_t reliability) {
  memset(s, 0, sizeof(*s));
  s->dtls_srtp = &transport;
  s->connected = 1;
  s->local_port = s->remote_port = 5000;
  s->tsn = 100;
  s->last_sack_tsn = 99;
  s->sent_packet_limit = 16;
  s->recv_cache_limit = 32768;
  s->peer_supports_forward_tsn = 1;
  assert(sctp_packet_pool_init(s) == 0);
  assert(sctp_tx_init(s) == 0);
  assert(sctp_rx_init(s) == 0);
  assert(sctp_configure_stream(s, 0, "test", channel, reliability) == 0);
  sctp_rx_reset(s, 9, 1);
  sends = forwards = messages = fragments = received_size = 0;
  check_payload = 0;
  write_delay_ms = 0;
  wire_count = opens = 0;
  closes = abort_cause = 0;
  now_ms += 1000;
}

static void destroy(Sctp* s) {
  sctp_deinit(s);
  assert(!s->tx && !s->rx && !s->packets && fixture_live == 0);
}

static void sack(Sctp* s, uint32_t cumulative, uint16_t start, uint16_t end) {
  uint32_t storage[(sizeof(SctpSackChunk) + 4) / 4] = {0};
  SctpSackChunk* ack = (void*)storage;
  size_t size = sizeof(*ack) + (start ? 4 : 0);
  ack->common.type = SCTP_SACK;
  ack->common.length = htons(size);
  ack->cumulative_tsn_ack = htonl(cumulative);
  if (start) {
    uint16_t pair[] = {htons(start), htons(end)};
    ack->number_of_gap_ack_blocks = htons(1);
    memcpy(ack->blocks, pair, sizeof(pair));
  }
  sctp_tx_sack(s, ack, size);
}

static int send_bytes(Sctp* s, size_t size) {
  static uint8_t data[SCTP_MTU * 16];
  memset(data, 0x5a, sizeof(data));
  assert(size <= sizeof(data));
  return sctp_outgoing_data(s, (char*)data, size, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0);
}

static void message(char* data, size_t size, void* user, uint16_t sid) {
  (void)user;
  assert(sid == 0 && size <= sizeof(received));
  memcpy(received, data, size);
  received_size = size;
  messages++;
}

static void fragment(char* data, size_t size, void* user, uint16_t sid,
                      uint32_t tsn, uint8_t flags) {
  (void)user;
  (void)flags;
  assert(sid == 0 && fragments < 3 && tsn == 10 + fragments && size == 1);
  assert((const uint8_t*)data == expected_fragment[fragments]);
  fragments++;
}

static struct net_buf* packet(uint32_t tsn, uint8_t flags, uint16_t ssn, char value) {
  struct net_buf* b = net_buf_alloc_len(&test_owner_pool, 32, K_NO_WAIT);
  assert(b);
  memset(b->data, 0, b->size);
  SctpHeader* h = net_buf_add(b, sizeof(*h));
  SctpDataChunk* d = net_buf_add(b, sizeof(*d) + 4);
  d->type = SCTP_DATA;
  d->iube = flags;
  d->length = htons(sizeof(*d) + 1);
  d->tsn = htonl(tsn);
  d->sqn = htons(ssn);
  d->ppid = htonl(PPID_BINARY);
  d->data[0] = value;
  h->checksum = sctp_get_checksum(NULL, b->data, b->len);
  return b;
}

static void incoming(Sctp* s, struct net_buf* b) {
  sctp_incoming_net_buf(s, b);
  net_buf_unref(b);
}

static void test_reliable(void) {
  Sctp s;
  init(&s, 0, 0);
  assert(send_bytes(&s, 1) == 1 && sends == 1 && s.tx->count == 1);
  now_ms += 150;
  sctp_process_timers(&s);
  assert(sends == 1);
  now_ms += 150;
  sctp_process_timers(&s);
  assert(sends == 2 && sent_tsn[1] == 100);
  sack(&s, 100, 0, 0);
  assert(s.tx->count == 0 && fixture_live == 0);
  fail_send = 1;
  assert(send_bytes(&s, 1) == 1 && s.tx->count == 1);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sent_tsn[sends - 1] == 101);
  destroy(&s);
}

static void test_gap_reneging_and_owner(void) {
  Sctp s;
  init(&s, 0, 0);
  struct net_buf* owner = net_buf_alloc_len(&test_owner_pool,
                                            2 * sctp_tx_payload_max(),
                                            K_NO_WAIT);
  size_t length = owner->size;
  memset(net_buf_add(owner, length), 0x5a, length);
  const uint8_t* original = owner->data;
  check_payload = 1;
  assert(sctp_outgoing_net_buf(&s, owner, PPID_BINARY, 0) == (int)length);
  assert(owner->ref == 2 && s.tx->count == 2);
  assert(sctp_tx_packet(s.tx, 0)->message->data == original);
  assert(sctp_tx_packet(s.tx, 0)->payload == NULL);
  net_buf_unref(owner);
  sack(&s, 99, 2, 2);
  assert(s.tx->count == 2 && fixture_live == 1);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 3 && sent_tsn[2] == 100);
  sack(&s, 99, 0, 0);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 5 && sent_tsn[4] == 101);
  sack(&s, 101, 0, 0);
  assert(s.tx->count == 0 && fixture_live == 0);
  destroy(&s);
}

static void test_sack_gap_retransmit(void) {
  Sctp s;
  init(&s, 0, 0);
  assert(send_bytes(&s, 3 * sctp_tx_payload_max()) > 0);
  now_ms += 299;
  sack(&s, 99, 2, 3);
  assert(sends == 3);
  now_ms++;
  sack(&s, 99, 2, 3); /* A duplicate SACK is not a new miss indication. */
  assert(sends == 3);
  sctp_process_timers(&s);
  assert(sends == 4 && sent_tsn[3] == 100);
  /* DATA 101 and 102 were gap-acknowledged, so the timer must not resend them. */
  assert(sctp_tx_packet(s.tx, 2)->retries == 0);
  sack(&s, 99, 0, 0);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 7 && sent_tsn[5] == 101 && sent_tsn[6] == 102);
  destroy(&s);
}

static void test_standard_fast_retransmit(void) {
  Sctp s;
  size_t initial_sends;

  init(&s, 0, 0);
  assert(send_bytes(&s, 10 * sctp_tx_payload_max()) > 0);
  initial_sends = sends;
  size_t allocations = heap_allocations;
  for (uint16_t end = 2; end <= 10; ++end) {
    sack(&s, 99, 2, end);
    assert(sends == initial_sends + (end >= 4));
  }
  /* Nine SACKs report the same hole, but only the third valid miss report
   * triggers one Fast Retransmit for TSN 100. */
  assert(sends == initial_sends + 1 && sent_tsn[initial_sends] == 100);
  assert(sctp_tx_packet(s.tx, 0)->fast_retransmitted);
  sack(&s, 99, 2, 10);
  assert(sends == initial_sends + 1);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == initial_sends + 2 && sent_tsn[initial_sends + 1] == 100);
  assert(heap_allocations == allocations);
  destroy(&s);
}

static void test_fast_retransmit_partial_reliability(void) {
  Sctp s;
  for (uint32_t retries = 0; retries <= 1; ++retries) {
    init(&s, 0x81, retries);
    if (retries == 0) {
      static uint32_t storage[SCTP_MTU * 4 / sizeof(uint32_t)];
      size_t headroom = sizeof(SctpHeader) + sizeof(SctpDataChunk);
      size_t length = 4 * sctp_tx_payload_max();
      uint8_t* data = (uint8_t*)storage + headroom;
      assert(sctp_outgoing_data(&s, (char*)data, length, PPID_BINARY, 0,
                                SCTP_DATA_ZERO_COPY, headroom,
                                sizeof(storage) - headroom - length) == (int)length);
      assert(!sctp_tx_packet(s.tx, 0)->message->data);
    } else {
      assert(send_bytes(&s, 4 * sctp_tx_payload_max()) > 0);
      now_ms += 300;
      sctp_process_timers(&s);
      assert(sctp_tx_packet(s.tx, 0)->retries == retries);
    }
    size_t initial_sends = sends;
    for (uint16_t end = 2; end <= 4; ++end) {
      sack(&s, 99, 2, end);
    }
    assert(sends == initial_sends);
    destroy(&s);
  }
  init(&s, 0x82, 100);
  assert(send_bytes(&s, 4 * sctp_tx_payload_max()) > 0);
  sack(&s, 99, 2, 2);
  sack(&s, 99, 2, 3);
  now_ms += 100;
  sack(&s, 99, 2, 4);
  assert(sends == 4 && sctp_tx_packet(s.tx, 0)->message->abandoned);
  destroy(&s);
}

static void test_fast_retransmit_reneging(void) {
  Sctp s;
  init(&s, 0, 0);
  assert(send_bytes(&s, 4 * sctp_tx_payload_max()) > 0);
  sack(&s, 99, 1, 1);
  sack(&s, 99, 0, 0); /* Reneging counts once even without newly ACKed DATA. */
  assert(sctp_tx_packet(s.tx, 0)->miss_reports == 1);
  sack(&s, 99, 0, 0);
  assert(sctp_tx_packet(s.tx, 0)->miss_reports == 1);
  sack(&s, 99, 2, 2);
  assert(sends == 4);
  sack(&s, 99, 2, 3);
  assert(sends == 5 && sent_tsn[4] == 100);
  destroy(&s);
}

static void test_fast_retransmit_recovery(void) {
  Sctp s;
  init(&s, 0, 0);
  assert(send_bytes(&s, 10 * sctp_tx_payload_max()) > 0);
  for (uint16_t end = 2; end <= 4; ++end) {
    sack(&s, 99, 2, end);
  }
  assert(sends == 11 && sent_tsn[10] == 100);
  assert(s.tx->fast_recovery && s.tx->recovery_exit_tsn == 109);
  for (uint32_t cumulative = 100; cumulative <= 102; ++cumulative) {
    uint32_t storage[(sizeof(SctpSackChunk) + 8) / 4] = {0};
    SctpSackChunk* ack = (void*)storage;
    uint16_t blocks[] = {htons(1), htons(103 - cumulative),
                         htons(105 - cumulative), htons(109 - cumulative)};
    ack->common.type = SCTP_SACK;
    ack->common.length = htons(sizeof(storage));
    ack->cumulative_tsn_ack = htonl(cumulative);
    ack->number_of_gap_ack_blocks = htons(2);
    memcpy(ack->blocks, blocks, sizeof(blocks));
    sctp_tx_sack(&s, ack, sizeof(storage));
    assert(sends == 11 + (cumulative == 102));
  }
  /* In recovery, advancing cumulative ACKs report hole 104 even though
   * all higher TSNs have already been gap-ACKed. */
  assert(sent_tsn[11] == 104);
  assert(s.tx->fast_recovery && s.tx->recovery_exit_tsn == 109);
  sack(&s, 109, 0, 0);
  assert(s.tx->count == 0 && !s.tx->fast_recovery);
  destroy(&s);
}

static void test_fast_retransmit_wrap(void) {
  Sctp s;
  init(&s, 0, 0);
  s.tsn = UINT32_MAX - 1U;
  s.last_sack_tsn = s.tsn - 1U;
  s.tx->max_ack_tsn = s.last_sack_tsn;
  assert(send_bytes(&s, 4 * sctp_tx_payload_max()) > 0);
  for (uint16_t end = 2; end <= 4; ++end) {
    sack(&s, UINT32_MAX - 2U, 2, end);
  }
  assert(sends == 5 && sent_tsn[4] == UINT32_MAX - 1U);
  sack(&s, 1, 0, 0);
  assert(s.tx->count == 0);
  destroy(&s);
}

static void test_fast_retransmit_deferred(void) {
  Sctp s;
  for (uint32_t delay = 0; delay <= 15; delay += 15) {
    init(&s, 0, 0);
    assert(send_bytes(&s, 10 * sctp_tx_payload_max()) > 0);
    sack(&s, 99, 4, 4);
    sack(&s, 99, 4, 5);
    write_delay_ms = delay;
    sack(&s, 99, 4, 6);
    assert(sends == 11 && sent_tsn[10] == 100);
    sctp_process_timers(&s);
    if (delay) {
      assert(sends == 12 && sent_tsn[11] == 101);
      sctp_process_timers(&s);
    }
    assert(sends == 13 && sent_tsn[11] == 101 && sent_tsn[12] == 102);
    destroy(&s);
  }
}

static struct net_buf* control_packet(uint8_t type, size_t chunk_size) {
  struct net_buf* b = net_buf_alloc_len(&test_owner_pool, sizeof(SctpHeader) + chunk_size, K_NO_WAIT);
  assert(b && b->len == 0);
  memset(net_buf_add(b, b->size), 0, b->size);
  SctpChunkCommon* c = (void*)(b->data + sizeof(SctpHeader));
  c->type = type;
  c->length = htons(chunk_size);
  return b;
}

static void seal(struct net_buf* b) {
  SctpHeader* h = (void*)b->data;
  h->checksum = 0;
  h->checksum = sctp_get_checksum(NULL, b->data, b->len);
}

static void opened(void* user) {
  Sctp* s = user;
  opens++;
  assert(s->connected);
}

static void opened_send(void* user) {
  opened(user);
  assert(send_bytes(user, 1) == 1);
}

static void test_handshake_retries_and_cookie(void) {
  Sctp s = {0};
  s.sent_packet_limit = 16;
  s.userdata = &s;
  s.onopen = opened;
  wire_count = opens = 0;
  now_ms += 1000;
  fail_send = 1; /* Losing the first INIT must not prevent association creation. */
  assert(sctp_create_association(&s, &transport) == 0);
  assert(wire_count == 1 && wire_type[0] == SCTP_INIT && s.handshake);
  assert(s.handshake->len == wire_size[0] && fixture_live == 1);
  for (unsigned interval = 20, i = 0; i < 8; ++i) {
    size_t count = wire_count;
    now_ms += interval - 1;
    sctp_process_timers(&s);
    assert(wire_count == count);
    now_ms++;
    sctp_process_timers(&s);
    assert(wire_count == count + 1 && wire_size[count] == wire_size[0]);
    assert(!memcmp(wire_bytes[count], wire_bytes[0], wire_size[0]));
    if (interval < 640) interval *= 2;
  }
  struct net_buf* ack = control_packet(SCTP_INIT_ACK, sizeof(SctpInitChunk) + 12);
  SctpInitChunk* init_ack = (void*)(ack->data + sizeof(SctpHeader));
  init_ack->initiate_tag = htonl(0x76543210);
  init_ack->initial_tsn = htonl(77);
  SctpChunkParam* cookie = init_ack->param;
  cookie->type = htons(SCTP_PARAM_STATE_COOKIE);
  cookie->length = htons(8);
  memcpy(cookie->value, "cook", 4);
  SctpChunkParam* pr = (void*)((uint8_t*)cookie + 8);
  pr->type = htons(SCTP_PARAM_FORWARD_TSN_SUPPORTED);
  pr->length = htons(4);
  seal(ack);
  incoming(&s, ack);
  size_t echo = wire_count - 1;
  assert(wire_type[echo] == SCTP_COOKIE_ECHO && wire_size[echo] == 20);
  assert(!memcmp(wire_bytes[echo] + 16, "cook", 4));
  assert(s.handshake && s.handshake->data[12] == SCTP_COOKIE_ECHO && fixture_live == 1);
  assert(s.handshake_timeout_ms == 20 && !s.connected && opens == 0);
  now_ms += 19;
  sctp_process_timers(&s);
  assert(wire_count == echo + 1);
  now_ms++;
  sctp_process_timers(&s);
  assert(wire_count == echo + 2 && !memcmp(wire_bytes[echo], wire_bytes[echo + 1], 20));
  struct net_buf* cookie_ack = control_packet(SCTP_COOKIE_ACK, sizeof(SctpChunkCommon));
  seal(cookie_ack);
  incoming(&s, cookie_ack);
  assert(s.connected && opens == 1 && !s.handshake && fixture_live == 0);
  now_ms += 1000;
  size_t count = wire_count;
  sctp_process_timers(&s);
  assert(wire_count == count);
  destroy(&s);
}

static void test_cookie_ack_precedes_reentrant_open_and_recreate(void) {
  Sctp s;
  init(&s, 0, 0);
  s.connected = 0;
  s.userdata = &s;
  s.onopen = opened_send;
  struct net_buf* cookie = control_packet(SCTP_COOKIE_ECHO, 8);
  memcpy(cookie->data + 16, "cook", 4);
  seal(cookie);
  incoming(&s, cookie);
  assert(opens == 1 && wire_count == 2);
  assert(wire_type[0] == SCTP_COOKIE_ACK && wire_size[0] == 16);
  assert(wire_type[1] == SCTP_DATA && sends == 1);
  s.stream_table[0].send_sqn = 42;
  s.stream_table[0].recv_sqn = 43;
  destroy(&s);
  assert(sctp_create_association(&s, &transport) == 0);
  assert(s.stream_count == 0 && s.last_sack_tsn == s.tsn - 1);
  assert(sctp_configure_stream(&s, 0, "test", 0, 0) == 0);
  assert(s.stream_table[0].send_sqn == 0 && s.stream_table[0].recv_sqn == 0);
  assert(!s.connected && s.handshake);
  destroy(&s);
}

static void test_ring_capacity_and_destroy(void) {
  const size_t limits[] = {0, 32, 48, 64, 96};
  Sctp s;
  for (size_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
    init(&s, 0, 0);
    sctp_tx_destroy(&s);
    s.sent_packet_limit = limits[i];
    assert(sctp_tx_init(&s) == 0);
    size_t capacity = limits[i] ? limits[i] : 4;
    assert(s.tx->capacity == capacity && s.tx->message_pool.buf_count == capacity);
    for (unsigned round = 0; round < 3; ++round) {
      size_t allocations = fixture_allocations;
      for (size_t j = 0; j < capacity; ++j) assert(send_bytes(&s, 0) == 0);
      assert(s.tx->message_pool.used == capacity && fixture_allocations == allocations);
      assert(!net_buf_alloc(&s.tx->message_pool, K_NO_WAIT));
      assert(!sctp_tx_can_send(&s, 1));
      size_t before = sends;
      assert(send_bytes(&s, 1) < 0 && sends == before && s.tx->count == capacity);
      sack(&s, s.tsn, 0, 0); /* Reject a cumulative ACK beyond any sent DATA. */
      assert(s.tx->count == capacity);
      sack(&s, s.tsn - 1, 0, 0);
      assert(s.tx->count == 0 && !s.tx->message_pool.used && fixture_live == 0);
    }
    struct net_buf* owner = sctp_packet_alloc(&s, 1);
    net_buf_add_mem(owner, "x", 1);
    assert(sctp_outgoing_net_buf(&s, owner, PPID_BINARY, 0) == 1);
    assert(owner->ref == 2);
    sctp_tx_destroy(&s);
    assert(owner->ref == 1 && fixture_live == 1);
    net_buf_unref(owner);
    destroy(&s);
  }
  fixture_fail_pool_init = 1;
  assert(sctp_tx_init(&s) < 0 && !s.tx);
  assert(sctp_tx_init(&s) == 0);
  sctp_tx_destroy(&s);
  s.sent_packet_limit = (size_t)UINT16_MAX + 1;
  assert(sctp_tx_init(&s) < 0 && !s.tx);
}

static void test_tx_pools_are_independent(void) {
  Sctp a, b;
  init(&a, 0, 0);
  init(&b, 0, 0);
  sctp_deinit(&a);
  sctp_deinit(&b);
  a.sent_packet_limit = 64;
  b.sent_packet_limit = 32;
  assert(sctp_packet_pool_init(&a) == 0 && sctp_packet_pool_init(&b) == 0);
  assert(sctp_tx_init(&a) == 0 && sctp_tx_init(&b) == 0);
  a.connected = b.connected = 1;
  for (size_t i = 0; i < a.tx->capacity; i++) assert(send_bytes(&a, 1) == 1);
  for (size_t i = 0; i < b.tx->capacity; i++) assert(send_bytes(&b, 1) == 1);
  assert(a.tx->message_pool.used == 64 && b.tx->message_pool.used == 32);
  assert(fixture_live == 96);
  sctp_destroy_association(&a);
  assert(!a.tx && !a.rx && b.tx->message_pool.used == 32 && fixture_live == 32);
  sack(&b, b.tsn - 1, 0, 0);
  assert(!b.tx->message_pool.used && fixture_live == 0);
  assert(send_bytes(&b, 1) == 1);
  destroy(&b);
  destroy(&a);
}

static void test_partial_reliability(void) {
  Sctp s;
  init(&s, 0x81, 0);
  assert(send_bytes(&s, 1) == 1);
  now_ms += 100;
  sctp_process_timers(&s);
  assert(sends == 1 && forwards == 1 && forward_tsn == 100);
  now_ms += 99;
  sctp_process_timers(&s);
  assert(forwards == 1);
  now_ms++;
  sctp_process_timers(&s);
  assert(forwards == 2 && sends == 1);
  sack(&s, 100, 0, 0);
  assert(!s.tx->count && !s.tx->forward_pending);
  destroy(&s);
  init(&s, 0, 0);
  assert(send_bytes(&s, 1) == 1);
  assert(sctp_configure_stream(&s, 0, "test", 0x81, 0) == 0);
  assert(send_bytes(&s, 1) == 1);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(forwards == 0 && sends == 3);
  sack(&s, 100, 0, 0);
  sctp_process_timers(&s);
  assert(forwards == 1 && forward_tsn == 101);
  destroy(&s);
  init(&s, 1, 1);
  assert(send_bytes(&s, sctp_tx_payload_max() + 1) > 0);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 4);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 4 && forwards == 1 && forward_tsn == 101 && fixture_live == 0);
  destroy(&s);
  init(&s, 2, 50);
  assert(send_bytes(&s, sctp_tx_payload_max() + 1) > 0);
  now_ms += 50;
  sctp_process_timers(&s);
  assert(sends == 2 && forwards == 1 && fixture_live == 0);
  destroy(&s);
}

static void test_receive(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onmessage = message;
  incoming(&s, packet(10, 2, 0, 'a'));
  incoming(&s, packet(12, 1, 0, 'c'));
  assert(messages == 0 && s.rx->count == 2);
  incoming(&s, packet(11, 0, 0, 'b'));
  assert(messages == 1 && received_size == 3 && !memcmp(received, "abc", 3));
  assert(s.recv_cumulative_tsn == 12 && s.rx->count == 0 && fixture_live == 0);
  incoming(&s, packet(11, 3, 0, 'x'));
  assert(messages == 1);
  struct net_buf* one = packet(13, 3, 1, 'd');
  size_t allocations = fixture_allocations;
  incoming(&s, one);
  assert(messages == 2 && fixture_allocations == allocations);
  incoming(&s, packet(15, 3, 3, 'f'));
  assert(messages == 2 && s.rx->count == 1);
  incoming(&s, packet(14, 3, 2, 'e'));
  assert(messages == 4 && s.rx->count == 0 && received[0] == 'f');
  incoming(&s, packet(16 + SCTP_RECEIVE_TSN_WINDOW, 3, 4, 'x'));
  assert(messages == 4 && s.recv_cumulative_tsn == 15);
  sctp_rx_reset(&s, UINT32_MAX - 1, 1);
  incoming(&s, packet(UINT32_MAX, 7, 0, 'w'));
  incoming(&s, packet(0, 7, 0, 'z'));
  assert(messages == 6 && s.recv_cumulative_tsn == 0);
  destroy(&s);
  init(&s, 0x81, 0);
  s.onfragment = fragment;
  struct net_buf* a = packet(10, 6, 0, 'a');
  struct net_buf* b = packet(11, 4, 0, 'b');
  struct net_buf* c = packet(12, 5, 0, 'c');
  expected_fragment[0] = a->data + 28;
  expected_fragment[1] = b->data + 28;
  expected_fragment[2] = c->data + 28;
  incoming(&s, a);
  incoming(&s, c);
  assert(fragments == 0);
  incoming(&s, b);
  assert(fragments == 3 && fixture_live == 0);
  destroy(&s);
}

static void test_receive_fragment_callback_with_retries(void) {
  Sctp s;
  init(&s, 0x81, 3);
  s.onmessage = message;
  s.onfragment = fragment;
  struct net_buf* a = packet(10, 6, 0, 'a');
  struct net_buf* b = packet(11, 4, 0, 'b');
  struct net_buf* c = packet(12, 5, 0, 'c');
  expected_fragment[0] = a->data + sizeof(SctpHeader) + sizeof(SctpDataChunk);
  expected_fragment[1] = b->data + sizeof(SctpHeader) + sizeof(SctpDataChunk);
  expected_fragment[2] = c->data + sizeof(SctpHeader) + sizeof(SctpDataChunk);
  size_t allocations = heap_allocations;
  incoming(&s, a);
  incoming(&s, c);
  assert(fragments == 0 && messages == 0 && s.rx->count == 2);
  incoming(&s, b);
  assert(fragments == 3 && messages == 0 && fixture_live == 0);
  assert(heap_allocations == allocations);

  sctp_rx_reset(&s, 9, 1);
  fragments = 0;
  struct net_buf* single = packet(10, 7, 0, 'x');
  expected_fragment[0] = single->data + sizeof(SctpHeader) + sizeof(SctpDataChunk);
  allocations = heap_allocations;
  incoming(&s, single);
  assert(fragments == 1 && messages == 0 && heap_allocations == allocations);
  destroy(&s);
}

static void test_receive_coalescing_pool(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onmessage = message;
  struct net_buf* held = net_buf_alloc(&s.rx->coalesce_pool, K_NO_WAIT);
  assert(held && held->size == sctp_receive_message_limit(&s));
  assert(!net_buf_alloc(&s.rx->coalesce_pool, K_NO_WAIT));
  struct net_buf* a = packet(10, 2, 0, 'a');
  struct net_buf* b = packet(11, 0, 0, 'b');
  struct net_buf* c = packet(12, 1, 0, 'c');
  size_t allocations = heap_allocations;
  incoming(&s, a);
  incoming(&s, c);
  incoming(&s, b);
  assert(messages == 0 && s.rx->count == 3);
  net_buf_unref(held);
  sctp_rx_poll(&s);
  assert(messages == 1 && received_size == 3 && !memcmp(received, "abc", 3));
  assert(s.rx->count == 0 && fixture_live == 0 && heap_allocations == allocations);

  for (uint16_t i = 1; i <= 64; i++) {
    a = packet(10 + 3 * i, 2, i, 'a');
    b = packet(11 + 3 * i, 0, i, 'b');
    c = packet(12 + 3 * i, 1, i, 'c');
    allocations = heap_allocations;
    incoming(&s, c);
    incoming(&s, a);
    incoming(&s, b);
    assert(messages == (size_t)i + 1 && received_size == 3 && !memcmp(received, "abc", 3));
    assert(s.rx->count == 0 && fixture_live == 0 && heap_allocations == allocations);
  }
  destroy(&s);
}

static void test_receive_shared_owner_charge(void) {
  Sctp s;
  init(&s, 0x81, 3);
  s.recv_cache_limit = 64;
  struct net_buf* owner = sctp_packet_alloc(&s, 64);
  assert(owner);
  memset(owner->data, 0, owner->size);
  SctpHeader* header = net_buf_add(owner, sizeof(*header));
  for (unsigned i = 0; i < 2; i++) {
    SctpDataChunk* data = net_buf_add(owner, sizeof(*data) + 4);
    data->type = SCTP_DATA;
    data->iube = 5;
    data->length = htons(sizeof(*data) + 1);
    data->tsn = htonl(11 + 2 * i);
    data->ppid = htonl(PPID_BINARY);
    data->data[0] = 'a' + i;
  }
  header->checksum = sctp_get_checksum(&s, owner->data, owner->len);
  incoming(&s, owner);
  /* Bundled tails retain one allocation, even though each holds a reference. */
  assert(s.rx->count == 2 && s.rx->bytes == 64 && owner->ref == 2);
  assert(rx_received(&s, 11) && rx_received(&s, 13));
  SctpForwardTsnChunk forward = {0};
  forward.new_cumulative_tsn = htonl(11);
  sctp_rx_forward(&s, &forward, sizeof(forward));
  assert(s.rx->count == 1 && s.rx->bytes == 64 && owner->ref == 1);
  forward.new_cumulative_tsn = htonl(13);
  sctp_rx_forward(&s, &forward, sizeof(forward));
  assert(s.rx->count == 0 && s.rx->bytes == 0 && fixture_live == 0);
  destroy(&s);
}

static void test_receive_fragment_pool_recovery(void) {
  Sctp s;
  init(&s, 0x81, 3);
  size_t capacity = s.rx->capacity;
  struct net_buf* owner = sctp_packet_alloc(&s, CONFIG_MTU);
  assert(owner);
  memset(owner->data, 0, owner->size);
  SctpHeader* header = net_buf_add(owner, sizeof(*header));
  for (unsigned i = 0; i < capacity; i++) {
    SctpDataChunk* data = net_buf_add(owner, sizeof(*data) + 4);
    data->type = SCTP_DATA;
    data->iube = 5;
    data->length = htons(sizeof(*data) + 1);
    data->tsn = htonl(11 + 2 * i);
    data->ppid = htonl(PPID_BINARY);
    data->data[0] = 'a';
  }
  header->checksum = sctp_get_checksum(&s, owner->data, owner->len);
  incoming(&s, owner);
  assert(s.rx->count == capacity);
  assert(s.rx->bytes == CONFIG_MTU);
  incoming(&s, packet(10, 6, 0, 'b'));
  /* The lowest hole must reclaim a later descriptor even with spare bytes. */
  assert(!rx_find(&s, 9 + 2 * capacity));
  assert(s.recv_cumulative_tsn == 11);
  assert(s.rx->count == capacity - 2);
  destroy(&s);
}

static void test_receive_pool_follows_cache_budget(void) {
  const size_t budgets[] = {0, 32768, 65536, 98304, 512 * 1024};
  const size_t capacities[] = {24, 24, 46, 68, 352};
  Sctp a = {0}, b = {0};
  for (size_t i = 0; i < sizeof(budgets) / sizeof(budgets[0]); i++) {
    a.recv_cache_limit = budgets[i];
    assert(sctp_rx_init(&a) == 0);
    assert(a.rx->capacity == capacities[i]);
    struct net_buf* held[352];
    for (size_t j = 0; j < capacities[i]; j++) {
      held[j] = net_buf_alloc(&a.rx->fragment_pool, K_NO_WAIT);
      assert(held[j]);
    }
    assert(!net_buf_alloc(&a.rx->fragment_pool, K_NO_WAIT));
    for (size_t j = 0; j < capacities[i]; j++) net_buf_unref(held[j]);
    sctp_rx_destroy(&a);
    assert(!a.rx);
  }
  a.recv_cache_limit = 96 * 1024;
  b.recv_cache_limit = 32 * 1024;
  assert(sctp_rx_init(&a) == 0 && sctp_rx_init(&b) == 0);
  assert(a.rx->capacity == 68 && b.rx->capacity == 24);
  struct net_buf* held = net_buf_alloc(&b.rx->fragment_pool, K_NO_WAIT);
  assert(held);
  sctp_rx_destroy(&a);
  memset(net_buf_add(held, sizeof(SctpRxFragment)), 0x5a, sizeof(SctpRxFragment));
  net_buf_unref(held);
  sctp_rx_destroy(&b);
  fixture_fail_pool_init = 1;
  assert(sctp_rx_init(&a) < 0 && !a.rx);
  fixture_fail_pool_init = 2;
  assert(sctp_rx_init(&a) < 0 && !a.rx);
  assert(sctp_rx_init(&a) == 0);
  sctp_rx_destroy(&a);
  a.recv_cache_limit = SIZE_MAX;
  assert(sctp_rx_init(&a) < 0 && !a.rx);
}

static void test_receive_failures_and_forward(void) {
  Sctp s;
  init(&s, 0, 0);
  struct net_buf* b = packet(10, 2, 0, 'a');
  fixture_fail_alloc = 1;
  sctp_incoming_data(&s, (char*)b->data, b->len);
  assert(s.recv_cumulative_tsn == 9 && s.rx->count == 0 && !rx_received(&s, 10));
  assert(s.connected && abort_cause == 0);
  net_buf_unref(b);
  incoming(&s, packet(10, 2, 0, 'a'));
  incoming(&s, packet(12, 1, 0, 'c'));
  assert(s.rx->count == 2);
  uint32_t storage[3] = {0};
  SctpForwardTsnChunk* f = (void*)storage;
  f->new_cumulative_tsn = htonl(12);
  uint16_t pair[] = {htons(0), htons(0)};
  memcpy(f + 1, pair, sizeof(pair));
  sctp_rx_forward(&s, f, sizeof(storage));
  assert(s.rx->count == 0 && fixture_live == 0 && s.stream_table[0].recv_sqn == 1);
  assert(s.recv_cumulative_tsn == 12);
  b = packet(13, 3, 1, 'd');
  b->data[31] ^= 1;
  incoming(&s, b);
  assert(s.recv_cumulative_tsn == 12);
  b = packet(13, 3, 1, 'd');
  ((SctpDataChunk*)(b->data + 12))->length = htons(500);
  ((SctpHeader*)b->data)->checksum = 0;
  ((SctpHeader*)b->data)->checksum = sctp_get_checksum(&s, b->data, b->len);
  incoming(&s, b);
  assert(s.recv_cumulative_tsn == 12);
  incoming(&s, packet(13, 2, 1, 'e'));
  assert(fixture_live == 1);
  destroy(&s);
}

static void test_receive_full_cache_recovers_lowest_hole(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onmessage = message;
  s.recv_cache_limit = 3 * 32;
  /* Three later tails exhaust backing storage while TSN 10 is still missing. */
  incoming(&s, packet(11, 1, 0, 'b'));
  incoming(&s, packet(13, 1, 1, 'd'));
  incoming(&s, packet(15, 1, 2, 'f'));
  assert(s.rx->bytes == 96 && s.rx->count == 3 && s.recv_cumulative_tsn == 9);
  assert(rx_received(&s, 15));
  incoming(&s, packet(10, 2, 0, 'a'));
  assert(messages == 1 && received_size == 2 && !memcmp(received, "ab", 2));
  assert(s.recv_cumulative_tsn == 11 && s.rx->count == 1 && s.rx->bytes == 32);
  assert(!rx_received(&s, 15) && rx_received(&s, 13));
  incoming(&s, packet(12, 2, 1, 'c'));
  assert(messages == 2 && !memcmp(received, "cd", 2) && s.recv_cumulative_tsn == 13);
  incoming(&s, packet(14, 2, 2, 'e'));
  assert(messages == 2); /* Evicted DATA must be received again before delivery. */
  incoming(&s, packet(15, 1, 2, 'f'));
  assert(messages == 3 && !memcmp(received, "ef", 2) && s.recv_cumulative_tsn == 15);
  assert(s.rx->count == 0 && s.rx->bytes == 0 && fixture_live == 0);
  destroy(&s);
}

static void test_forward_discards_whole_message_across_holes(void) {
  Sctp s;
  init(&s, 0x81, 0);
  s.onmessage = message;
  incoming(&s, packet(10, 6, 0, 'a'));
  incoming(&s, packet(12, 4, 0, 'c'));
  incoming(&s, packet(14, 5, 0, 'e'));
  /* A later BEGIN bounds the next message, whose missing middle may still arrive. */
  incoming(&s, packet(15, 6, 0, 'x'));
  incoming(&s, packet(17, 5, 0, 'z'));
  assert(s.rx->count == 5 && messages == 0);
  SctpForwardTsnChunk forward = {0};
  forward.new_cumulative_tsn = htonl(11);
  sctp_rx_forward(&s, &forward, sizeof(forward));
  assert(s.recv_cumulative_tsn == 12 && s.rx->count == 2 && fixture_live == 2);
  assert(!rx_received(&s, 12) && rx_received(&s, 14));
  assert(rx_received(&s, 15) && rx_received(&s, 17));
  incoming(&s, packet(16, 4, 0, 'y'));
  assert(messages == 1 && received_size == 3 && !memcmp(received, "xyz", 3));
  assert(s.rx->count == 0 && fixture_live == 0);
  destroy(&s);
}

static void test_forward_preserves_received_tail(void) {
  const uint32_t baselines[] = {9, UINT32_MAX - 2U};
  for (size_t i = 0; i < sizeof(baselines) / sizeof(baselines[0]); ++i) {
    for (unsigned unordered = 0; unordered < 2; ++unordered) {
      Sctp s;
      uint32_t base = baselines[i];
      uint8_t flags = unordered ? 4 : 0;
      uint32_t storage[3] = {0};
      SctpForwardTsnChunk* forward = (void*)storage;
      uint16_t pair[] = {htons(0), htons(0)};
      init(&s, unordered ? 0x81 : 0, 0);
      s.onmessage = message;
      sctp_rx_reset(&s, base, 1);
      incoming(&s, packet(base + 1U, flags | 2U, 0, 'a'));
      incoming(&s, packet(base + 3U, flags | 1U, 0, 'c'));
      assert(s.recv_cumulative_tsn == base + 1U && rx_received(&s, base + 3U));
      forward->new_cumulative_tsn = htonl(base + 2U);
      memcpy(forward + 1, pair, sizeof(pair));
      sctp_rx_forward(&s, forward, unordered ? sizeof(*forward) : sizeof(storage));
      assert(s.recv_cumulative_tsn == base + 3U && s.rx->count == 0);
      /* A delayed skipped fragment cannot restore a tail receipt lost by FORWARD. */
      incoming(&s, packet(base + 2U, flags, 0, 'b'));
      incoming(&s, packet(base + 4U, flags | 3U, 1, 'd'));
      assert(s.recv_cumulative_tsn == base + 4U && messages == 1 && received[0] == 'd');
      destroy(&s);
    }
  }
}

static void test_forward_discards_late_orphan_tails(void) {
  const uint32_t baselines[] = {9, UINT32_MAX - 2U};
  for (size_t i = 0; i < sizeof(baselines) / sizeof(*baselines); i++) {
    Sctp s;
    uint32_t base = baselines[i];
    init(&s, 0x81, 3);
    s.onmessage = message;
    sctp_rx_reset(&s, base, 1);
    incoming(&s, packet(base + 3U, 4, 0, 'c'));
    incoming(&s, packet(base + 5U, 5, 0, 'e'));
    SctpForwardTsnChunk forward = {0};
    forward.new_cumulative_tsn = htonl(base + 1U);
    sctp_rx_forward(&s, &forward, sizeof(forward));
    /* Above the cumulative ACK, a missing BEGIN may still arrive. */
    assert(s.rx->count == 2 && rx_received(&s, base + 3U));
    incoming(&s, packet(base + 2U, 4, 0, 'b'));
    assert(s.recv_cumulative_tsn == base + 3U && s.rx->count == 1);
    assert(rx_received(&s, base + 5U));
    incoming(&s, packet(base + 4U, 4, 0, 'd'));
    assert(s.recv_cumulative_tsn == base + 5U && !s.rx->count && !s.rx->bytes);
    /* This valid out-of-order message must survive until its BEGIN arrives. */
    incoming(&s, packet(base + 7U, 5, 1, 'y'));
    assert(s.rx->count == 1);
    incoming(&s, packet(base + 6U, 6, 1, 'x'));
    assert(messages == 1 && received_size == 2 && !memcmp(received, "xy", 2));
    assert(!s.rx->count && !s.rx->bytes);
    destroy(&s);
  }
}

static void test_receive_window_precedes_duplicate_bitmap(void) {
  const uint32_t baselines[] = {9, UINT32_MAX - 2U};
  for (size_t i = 0; i < sizeof(baselines) / sizeof(baselines[0]); ++i) {
    Sctp s;
    uint32_t base = baselines[i];
    const SctpSackChunk* ack;
    init(&s, 0x81, 0);
    sctp_rx_reset(&s, base, 1);
    incoming(&s, packet(base + 2U, 7, 0, 'x'));
    incoming(&s, packet(base + 2U + SCTP_RECEIVE_TSN_WINDOW, 7, 0, 'y'));
    ack = (const void*)(wire_bytes[wire_count - 1] + sizeof(SctpHeader));
    assert(ntohs(ack->number_of_dup_tsns) == 0);
    assert(s.recv_cumulative_tsn == base && rx_received(&s, base + 2U));
    incoming(&s, packet(base + 2U, 7, 0, 'x'));
    ack = (const void*)(wire_bytes[wire_count - 1] + sizeof(SctpHeader));
    assert(ntohs(ack->number_of_dup_tsns) == 1);
    incoming(&s, packet(base, 7, 0, 'z'));
    ack = (const void*)(wire_bytes[wire_count - 1] + sizeof(SctpHeader));
    assert(ntohs(ack->number_of_dup_tsns) == 1);
    destroy(&s);
  }
}

static void test_orphan_tail_after_complete_boundary(void) {
  Sctp s;
  init(&s, 0, 0);
  SctpForwardTsnChunk forward = {0};
  forward.new_cumulative_tsn = htonl(9);
  sctp_rx_forward(&s, &forward, sizeof(forward));
  /* An ordered single-chunk message can wait for an earlier SSN. */
  incoming(&s, packet(10, 3, 1, 'a'));
  assert(s.rx->count == 1);
  /* A malformed same-SSN tail cannot extend that already-ended message. */
  incoming(&s, packet(11, 1, 1, 'b'));
  assert(s.recv_cumulative_tsn == 11 && s.rx->count == 1);
  assert(rx_find(&s, 10) && !rx_find(&s, 11));
  destroy(&s);
}

static void closed(void* user) {
  (void)user;
  closes++;
}

static struct net_buf* sized_data(uint32_t tsn, uint8_t flags, size_t payload_size) {
  size_t chunk_size = (sizeof(SctpDataChunk) + payload_size + 3) & ~(size_t)3;
  struct net_buf* b = control_packet(SCTP_DATA, chunk_size);
  SctpDataChunk* d = (void*)(b->data + sizeof(SctpHeader));
  d->length = htons(sizeof(*d) + payload_size);
  d->iube = flags;
  d->tsn = htonl(tsn);
  d->ppid = htonl(PPID_BINARY);
  memset(d->data, 'x', payload_size);
  seal(b);
  return b;
}

static void large_fragment(char* data, size_t size, void* user, uint16_t sid,
                           uint32_t tsn, uint8_t flags) {
  (void)user;
  assert(sid == 0 && tsn == 10 + fragments && received_size + size <= 61440);
  assert(!!(flags & 2U) == (received_size == 0));
  for (size_t i = 0; i < size; i++) {
    assert((uint8_t)data[i] == (uint8_t)((received_size + i) * 37U));
  }
  received_size += size;
  assert(!!(flags & 1U) == (received_size == 61440));
  fragments++;
}

static void test_receive_192k_chrome_fragment_sizes(void) {
  for (unsigned pattern = 0; pattern < 2; pattern++) {
    Sctp s;
    struct net_buf* tx_held[32];
    init(&s, 0x81, 3);
    sctp_deinit(&s);
    s.recv_cache_limit = 192 * 1024;
    s.sent_packet_limit = 32;
    assert(!sctp_packet_pool_init(&s) && !sctp_tx_init(&s) && !sctp_rx_init(&s));
    sctp_rx_reset(&s, 9, 1);
    s.connected = 1;
    s.onfragment = large_fragment;
    assert(s.packets->buf_count == 168 && s.rx->capacity == 134);
    for (size_t i = 0; i < 32; i++) {
      tx_held[i] = sctp_packet_alloc(&s, CONFIG_MTU);
      assert(tx_held[i]);
    }
    size_t offset = 0;
    uint32_t tsn = 10;
    while (offset < 61440) {
      size_t size = (tsn - 10) % 2 ? (pattern ? 20 : 12) : (pattern ? 1136 : 1144);
      if (size > 61440 - offset) size = 61440 - offset;
      struct net_buf* p = sctp_packet_alloc(&s, CONFIG_MTU);
      assert(p);
      memset(p->data, 0, p->size);
      net_buf_add(p, sizeof(SctpHeader) + sizeof(SctpDataChunk) + size);
      SctpDataChunk* d = (void*)(p->data + sizeof(SctpHeader));
      d->type = SCTP_DATA;
      d->iube = 4U | (offset == 0 ? 2U : 0) | (offset + size == 61440 ? 1U : 0);
      d->length = htons(sizeof(*d) + size);
      d->tsn = htonl(tsn++);
      d->ppid = htonl(PPID_BINARY);
      for (size_t i = 0; i < size; i++) d->data[i] = (uint8_t)((offset + i) * 37U);
      seal(p);
      incoming(&s, p);
      assert(s.connected);
      offset += size;
    }
    assert(fragments == 107 && received_size == 61440 && s.recv_cumulative_tsn == 116);
    assert(!s.rx->count && !s.rx->bytes && fixture_live == 32);
    for (size_t i = 0; i < 32; i++) net_buf_unref(tx_held[i]);
    assert(!fixture_live);
    destroy(&s);
  }
}

static void assert_aborted(Sctp* s, uint16_t cause) {
  assert(abort_cause == cause && closes == 1 && !s->connected);
  assert(!s->tx && !s->rx && !s->handshake && fixture_live == 0);
  assert(messages == 0);
  destroy(s);
}

static void test_receive_size_limit_aborts(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onclose = closed;
  s.onmessage = message;
  s.recv_cache_limit = 64;
  incoming(&s, sized_data(10, 3, 33));
  assert_aborted(&s, 13);
  for (unsigned missing_begin = 0; missing_begin < 2; ++missing_begin) {
    init(&s, 0, 0);
    s.onclose = closed;
    s.onmessage = message;
    s.recv_cache_limit = 192;
    incoming(&s, sized_data(10, missing_begin ? 0 : 2, 49));
    assert(s.connected && s.rx->count == 1 && closes == 0);
    incoming(&s, sized_data(11, 1, 49));
    assert_aborted(&s, 13);
  }
  /* The exact boundary is accepted by an independent healthy association. */
  init(&s, 0, 0);
  s.onclose = closed;
  s.onmessage = message;
  s.recv_cache_limit = 64;
  incoming(&s, sized_data(10, 3, 32));
  assert(messages == 1 && received_size == 32 && closes == 0 && abort_cause == 0);
  assert(s.connected && s.recv_cumulative_tsn == 10);
  destroy(&s);
}

static void test_receive_irrecoverable_storage_aborts(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onclose = closed;
  s.recv_cache_limit = 64;
  incoming(&s, packet(10, 2, 0, 'a'));
  incoming(&s, packet(11, 0, 0, 'b'));
  assert(s.rx->bytes == 64 && s.recv_cumulative_tsn == 11);
  incoming(&s, packet(12, 1, 0, 'c'));
  assert_aborted(&s, 4);
  /* Allocation pressure with no irrevocably ACKed data remains retryable. */
  init(&s, 0, 0);
  s.onclose = closed;
  struct net_buf* b = packet(10, 2, 0, 'a');
  fixture_fail_alloc = 1;
  sctp_incoming_data(&s, (char*)b->data, b->len);
  net_buf_unref(b);
  assert(s.connected && s.rx && closes == 0 && abort_cause == 0);
  assert(s.recv_cumulative_tsn == 9 && s.rx->count == 0 && !rx_received(&s, 10));
  incoming(&s, packet(10, 2, 0, 'a'));
  assert(s.connected && s.recv_cumulative_tsn == 10 && s.rx->count == 1);
  destroy(&s);
}

static void test_receive_shared_pool_pressure_recovers_hole(void) {
  Sctp s;
  init(&s, 0, 0);
  s.onclose = closed;
  s.onmessage = message;
  incoming(&s, packet(10, 2, 0, 'a'));
  incoming(&s, packet(12, 1, 0, 'c'));
  assert(s.recv_cumulative_tsn == 10 && rx_received(&s, 12));
  assert(s.rx->bytes < s.recv_cache_limit);
  struct net_buf* b = packet(11, 0, 0, 'b');
  fixture_fail_alloc = 1;
  sctp_incoming_data(&s, (char*)b->data, b->len);
  net_buf_unref(b);
  assert(s.connected && closes == 0 && s.recv_cumulative_tsn == 11);
  assert(rx_find(&s, 10) && rx_find(&s, 11) && !rx_find(&s, 12));
  assert(!rx_received(&s, 12) && messages == 0);
  incoming(&s, packet(12, 1, 0, 'c'));
  assert(messages == 1 && received_size == 3 && !memcmp(received, "abc", 3));
  assert(s.recv_cumulative_tsn == 12 && s.rx->count == 0);
  destroy(&s);
}

static void test_sdp_matches_receive_budget(void) {
  Sctp s = {0};
  char description[CONFIG_SDP_BUFFER_SIZE] = {0};
  assert(sctp_receive_message_limit(&s) == 16384);
  sdp_append_datachannel(description, "data", sctp_receive_message_limit(&s));
  assert(strstr(description, "a=max-message-size:16384\r\n"));
  s.recv_cache_limit = 8192;
  sdp_reset(description);
  assert(sctp_receive_message_limit(&s) == 4096);
  sdp_append_datachannel(description, "data", sctp_receive_message_limit(&s));
  assert(strstr(description, "a=max-message-size:4096\r\n"));
  s.recv_cache_limit = 96 * 1024;
  assert(sctp_receive_message_limit(&s) == 48 * 1024);
  s.recv_cache_limit = 128 * 1024;
  sdp_reset(description);
  assert(sctp_receive_message_limit(&s) == UINT16_MAX);
  sdp_append_datachannel(description, "data", sctp_receive_message_limit(&s));
  assert(strstr(description, "a=max-message-size:65535\r\n"));
}

static void test_raw_control_ack_releases_full_pool(void) {
  Sctp s;
  init(&s, 0, 0);
  assert(send_bytes(&s, 1) == 1 && fixture_live == 1);
  uint32_t raw[(sizeof(SctpHeader) + sizeof(SctpSackChunk)) / 4] = {0};
  SctpSackChunk* ack = (void*)((uint8_t*)raw + sizeof(SctpHeader));
  ack->common.type = SCTP_SACK;
  ack->common.length = htons(sizeof(*ack));
  ack->cumulative_tsn_ack = htonl(100);
  ((SctpHeader*)raw)->checksum = sctp_get_checksum(&s, (const uint8_t*)raw, sizeof(raw));
  fixture_fail_alloc = 1;
  sctp_incoming_data(&s, (char*)raw, sizeof(raw));
  assert(s.tx->count == 0 && fixture_live == 0 && fixture_fail_alloc == 1);
  assert(s.connected && abort_cause == 0);
  fixture_fail_alloc = 0;
  destroy(&s);
}

static void test_abort_is_terminal(void) {
  Sctp s;
  init(&s, 0, 0);
  struct net_buf* owner = sctp_packet_alloc(&s, 1);
  net_buf_add_mem(owner, "x", 1);
  assert(sctp_outgoing_net_buf(&s, owner, PPID_BINARY, 0) == 1);
  net_buf_unref(owner);
  incoming(&s, packet(10, 2, 0, 'a'));
  assert(fixture_live == 2);
  struct net_buf* remote_abort = control_packet(SCTP_ABORT, 8);
  seal(remote_abort);
  size_t count = wire_count;
  incoming(&s, remote_abort);
  assert(!s.connected && !s.tx && !s.rx && fixture_live == 0);
  now_ms += 1000;
  sctp_process_timers(&s);
  assert(wire_count == count);
  destroy(&s);

  init(&s, 0, 0);
  s.recv_cache_limit = 64;
  s.userdata = &s;
  s.onopen = opened_send;
  s.onclose = closed;
  struct net_buf* oversized = sized_data(10, 3, 33);
  struct net_buf* bundled = sctp_packet_alloc(&s, oversized->len + 8);
  net_buf_add_mem(bundled, oversized->data, oversized->len);
  net_buf_unref(oversized);
  SctpChunkCommon* cookie = net_buf_add(bundled, 8);
  memset(cookie, 0, 8);
  cookie->type = SCTP_COOKIE_ECHO;
  cookie->length = htons(8);
  seal(bundled);
  incoming(&s, bundled);
  assert_aborted(&s, 13);
  assert(opens == 0 && wire_count == 1 && wire_type[0] == SCTP_ABORT);
  struct net_buf* later_cookie = control_packet(SCTP_COOKIE_ECHO, 8);
  seal(later_cookie);
  incoming(&s, later_cookie);
  now_ms += 1000;
  sctp_process_timers(&s);
  assert(opens == 0 && wire_count == 1 && fixture_live == 0);
}

static void test_gap_acked_retry_limit_does_not_abandon_message(void) {
  Sctp s;
  init(&s, 1, 1);
  assert(send_bytes(&s, sctp_tx_payload_max() + 1) > 0);
  sack(&s, 99, 2, 2);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 3 && sent_tsn[2] == 100);
  assert(sctp_tx_packet(s.tx, 0)->retries == 1);
  assert(sctp_tx_packet(s.tx, 1)->retries == 0);
  /* Peer now ACKs A but reneges B; A's retry limit cannot discard pending B. */
  sack(&s, 99, 1, 1);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 4 && sent_tsn[3] == 101 && forwards == 0);
  assert(!sctp_tx_packet(s.tx, 0)->message->abandoned);
  assert(sctp_tx_packet(s.tx, 1)->retries == 1 && fixture_live == 2);
  sack(&s, 101, 0, 0);
  assert(s.tx->count == 0 && fixture_live == 0);
  destroy(&s);
}

static void message_destroy(char* data, size_t size, void* user, uint16_t sid) {
  assert(sid == 0 && size == 1 && data[0] == 'x');
  messages++;
  sctp_destroy_association(user);
}

static void fragment_destroy(char* data, size_t size, void* user, uint16_t sid,
                             uint32_t tsn, uint8_t flags) {
  assert(sid == 0 && size == 1 && tsn == 10 && flags == 6);
  assert((const uint8_t*)data == expected_fragment[0] && data[0] == 'a');
  assert(fragments == 0);
  fragments++;
  sctp_destroy_association(user);
  assert(data[0] == 'a');
}

static void coalesced_message_destroy(char* data, size_t size, void* user, uint16_t sid) {
  assert(sid == 0 && size == 3 && !memcmp(data, "abc", size));
  messages++;
  sctp_destroy_association(user);
  /* Closing the association must not invalidate this borrowed callback span. */
  assert(!memcmp(data, "abc", size));
}

static void test_coalesced_message_callback_destroy(void) {
  Sctp s;
  init(&s, 0, 0);
  s.userdata = &s;
  s.onmessage = coalesced_message_destroy;
  struct net_buf* a = packet(10, 2, 0, 'a');
  struct net_buf* b = packet(11, 0, 0, 'b');
  struct net_buf* c = packet(12, 1, 0, 'c');
  size_t allocations = heap_allocations;
  incoming(&s, a);
  incoming(&s, c);
  incoming(&s, b);
  assert(messages == 1 && !s.rx && !s.tx && !s.connected && fixture_live == 0);
  assert(heap_allocations == allocations);
  destroy(&s);
}

static void test_single_message_callback_destroy(void) {
  Sctp s;
  init(&s, 0, 0);
  s.userdata = &s;
  s.onmessage = message_destroy;
  incoming(&s, packet(10, 3, 0, 'x'));
  assert(messages == 1 && !s.rx && !s.tx && !s.connected && fixture_live == 0);
  destroy(&s);
}

static void test_fragment_callback_destroy_stops_delivery(void) {
  Sctp s;
  for (uint32_t retries = 0; retries <= 3; retries += 3) {
    init(&s, 0x81, retries);
    s.userdata = &s;
    s.onfragment = fragment_destroy;
    struct net_buf* a = packet(10, 6, 0, 'a');
    expected_fragment[0] = a->data + sizeof(SctpHeader) + sizeof(SctpDataChunk);
    incoming(&s, a);
    incoming(&s, packet(12, 5, 0, 'c'));
    assert(fragments == 0 && s.rx->count == 2);
    incoming(&s, packet(11, 4, 0, 'b'));
    assert(fragments == 1 && !s.rx && !s.tx && !s.connected && fixture_live == 0);
    destroy(&s);
  }
}

static void test_partial_reliability_initial_write_failure(void) {
  Sctp s;
  for (uint8_t policy = 1; policy <= 2; ++policy) {
    init(&s, policy, policy == 1 ? 1 : 1000);
    struct net_buf* owner = sctp_packet_alloc(&s, 1);
    net_buf_add_mem(owner, "x", 1);
    fail_send = 1;
    assert(sctp_outgoing_net_buf(&s, owner, PPID_BINARY, 0) == 1);
    assert(owner->ref == 2 && s.tx->count == 1);
    assert(!sctp_tx_packet(s.tx, 0)->message->abandoned);
    net_buf_unref(owner);
    now_ms += 150;
    sctp_process_timers(&s);
    assert(sends == 2 && sent_tsn[1] == 100 && forwards == 0);
    assert(sctp_tx_packet(s.tx, 0)->sent && fixture_live == 1);
    sack(&s, 100, 0, 0);
    assert(s.tx->count == 0 && fixture_live == 0);
    destroy(&s);
  }
  init(&s, 0x81, 0);
  uint32_t storage[9] = {0};
  uint8_t* data = (uint8_t*)storage + sizeof(SctpHeader) + sizeof(SctpDataChunk);
  data[0] = 'x';
  fail_send = 1;
  assert(sctp_outgoing_data(&s, (char*)data, 1, PPID_BINARY, 0,
                            SCTP_DATA_ZERO_COPY, 28, sizeof(storage) - 29) == 1);
  SctpTxMessage* message = sctp_tx_packet(s.tx, 0)->message;
  assert(message->abandoned && !message->data && !message->owner);
  assert(sctp_tx_packet(s.tx, 0)->payload == NULL && fixture_live == 0);
  now_ms += 300;
  sctp_process_timers(&s);
  assert(sends == 1 && forwards >= 1);
  destroy(&s);
}

enum { NETWORK_MESSAGES = 64, NETWORK_QUEUE = 256, NETWORK_QUEUE_DELAY_MS = 250 };

typedef struct {
  const char* name;
  unsigned loss, ack_loss, delay, jitter, reorder, duplicate, kbps;
  unsigned fault, outage, policy, lifetime, drop_forward, large;
} NetworkCase;

static struct {
  const NetworkCase* config;
  Sctp peer[2];
  DtlsSrtp transport[2];
  unsigned id[2], submitted[2], delivered[2], forward_next_message[2];
  uint64_t seen[2];
  uint32_t rng, started, next_wire[2], next_message[2], highest_arrived[2];
  struct {
    uint8_t data[CONFIG_MTU];
    size_t size;
    uint32_t due;
    unsigned source, original;
  } queue[NETWORK_QUEUE];
  size_t queued, data_packets, sacks, dropped, ack_dropped, duplicates, reordered;
  size_t retransmits, early_retransmits, congestion_dropped;
  size_t forward_dropped[2], recovered[2];
  uint32_t first_retry_ms;
  uint32_t first_delivery_ms[2];
  unsigned forced[3];
} network;

static uint32_t network_random(void) {
  network.rng = network.rng * 1664525U + 1013904223U;
  return network.rng;
}

static void network_enqueue(unsigned source, const uint8_t* data, size_t size,
                             uint32_t due, unsigned original) {
  for (size_t i = 0; i < NETWORK_QUEUE; ++i) {
    if (network.queue[i].size) continue;
    memcpy(network.queue[i].data, data, size);
    network.queue[i].size = size;
    network.queue[i].source = source;
    network.queue[i].original = original;
    network.queue[i].due = due;
    ++network.queued;
    return;
  }
  assert(!"simulated link queue overflow");
}

static int network_transmit(DtlsSrtp* dtls, const uint8_t* data, size_t size) {
  const NetworkCase* config = network.config;
  unsigned source = dtls == &network.transport[1];
  const SctpChunkCommon* chunk = (const void*)(data + sizeof(SctpHeader));
  assert(dtls == &network.transport[source] && size <= CONFIG_MTU);
  assert(size >= sizeof(SctpHeader) + sizeof(*chunk) && chunk->type != SCTP_ABORT);
  uint32_t wire = network.next_wire[source];
  if (sctp_tsn_gte(now_ms, wire)) wire = now_ms;
  wire += (size * 8U + config->kbps - 1U) / config->kbps;
  int drop = now_ms - network.started < config->outage;
  unsigned original = chunk->type == SCTP_DATA;
  /* A finite bottleneck drops overflow instead of accumulating unlimited delay. */
  if (wire - now_ms > NETWORK_QUEUE_DELAY_MS) {
    drop = 1;
    ++network.congestion_dropped;
  } else {
    network.next_wire[source] = wire;
  }
  if (chunk->type == SCTP_DATA) {
    uint32_t tsn = ntohl(((const SctpDataChunk*)chunk)->tsn);
    ++network.data_packets;
    SctpTx* tx = network.peer[source].tx;
    for (size_t i = 0; i < tx->count; ++i) {
      SctpTxPacket* packet = sctp_tx_packet(tx, i);
      if (packet->tsn != tsn || !packet->sent) continue;
      original = 0;
      ++network.retransmits;
      if (now_ms - packet->last_send_ms < 300) ++network.early_retransmits;
      if (!source && tsn == 100 && !network.first_retry_ms) {
        network.first_retry_ms = now_ms - network.started;
      }
      break;
    }
    if (!source && tsn >= 100 && tsn <= 102 && config->fault) {
      unsigned limit = config->fault == 1 ? (tsn == 100) : (tsn == 100 ? 2 : 1);
      if (network.forced[tsn - 100] < limit) {
        ++network.forced[tsn - 100];
        drop = 1;
      }
    }
    drop |= network_random() % 100 < config->loss;
  } else if (chunk->type == SCTP_SACK) {
    ++network.sacks;
    drop |= network_random() % 100 < config->ack_loss;
  } else if (chunk->type == SCTP_FORWARD_TSN && config->drop_forward &&
             !network.forward_dropped[source]) {
    drop = 1;
    ++network.forward_dropped[source];
    network.forward_next_message[source] = network.submitted[source];
  }
  /* A network drop still reports a successful local DTLS write. */
  if (drop) {
    ++network.dropped;
    if (chunk->type == SCTP_SACK) ++network.ack_dropped;
    return (int)size;
  }
  uint32_t due = wire + config->delay;
  if (config->jitter) due += network_random() % (config->jitter + 1U);
  if (network_random() % 100 < config->reorder) due += 100;
  network_enqueue(source, data, size, due, original);
  if (network_random() % 100 < config->duplicate) {
    network_enqueue(source, data, size, due + 3, 0);
    ++network.duplicates;
  }
  return (int)size;
}

static size_t network_message_size(unsigned index) {
  return index % 8 == 0 ? (network.config->large ? 40 : 12) * 1024 :
                         128 + (index * 997U) % (6 * 1024);
}

static uint8_t network_byte(unsigned source, unsigned index, size_t offset) {
  return (uint8_t)(source * 97U + index * 37U + offset * 13U + (offset >> 8));
}

static void network_message(char* data, size_t size, void* user, uint16_t sid) {
  unsigned destination = *(unsigned*)user;
  unsigned source = 1U - destination;
  uint32_t header[2];
  assert(sid == 0 && size >= sizeof(header));
  memcpy(header, data, sizeof(header));
  unsigned index = header[1];
  assert(header[0] == source && index < NETWORK_MESSAGES);
  assert(size == network_message_size(index));
  assert(!(network.seen[destination] & (UINT64_C(1) << index)));
  if (!network.config->policy) assert(index == network.delivered[destination]);
  for (size_t i = sizeof(header); i < size; ++i) {
    assert((uint8_t)data[i] == network_byte(source, index, i));
  }
  network.seen[destination] |= UINT64_C(1) << index;
  if (!network.delivered[destination]) {
    network.first_delivery_ms[destination] = now_ms - network.started;
  }
  if (network.forward_dropped[source] && index >= network.forward_next_message[source]) {
    ++network.recovered[destination];
  }
  ++network.delivered[destination];
}

static void network_deliver(void) {
  for (size_t i = 0; i < NETWORK_QUEUE; ++i) {
    if (!network.queue[i].size || !sctp_tsn_gte(now_ms, network.queue[i].due)) continue;
    unsigned destination = 1U - network.queue[i].source;
    size_t size = network.queue[i].size;
    Sctp* peer = &network.peer[destination];
    struct net_buf* owner = sctp_packet_alloc(peer, size);
    assert(owner);
    net_buf_add_mem(owner, network.queue[i].data, size);
    network.queue[i].size = 0;
    --network.queued;
    const SctpChunkCommon* chunk = (const void*)(owner->data + sizeof(SctpHeader));
    if (chunk->type == SCTP_DATA && network.queue[i].original) {
      uint32_t tsn = ntohl(((const SctpDataChunk*)chunk)->tsn);
      if (!sctp_tsn_gte(tsn, network.highest_arrived[destination])) ++network.reordered;
      else network.highest_arrived[destination] = tsn;
    }
    /* Copy before freeing the queue slot: receive processing can enqueue SACKs. */
    sctp_incoming_net_buf(peer, owner);
    net_buf_unref(owner);
    assert(peer->connected && peer->tx && peer->rx);
  }
}

static void network_submit(unsigned source) {
  static uint8_t payload[40 * 1024];
  unsigned index = network.submitted[source];
  if (index == NETWORK_MESSAGES || !sctp_tsn_gte(now_ms, network.next_message[source])) return;
  size_t size = network_message_size(index);
  Sctp* peer = &network.peer[source];
  if (!sctp_tx_can_send(peer, size)) return;
  for (size_t i = 0; i < size; ++i) payload[i] = network_byte(source, index, i);
  uint32_t header[] = {source, index};
  memcpy(payload, header, sizeof(header));
  assert(sctp_outgoing_data(peer, (char*)payload, size, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == (int)size);
  ++network.submitted[source];
  network.next_message[source] = now_ms + 33;
}

static void test_network_impairments(void) {
  const NetworkCase cases[] = {
    {.name = "clean", .delay = 10, .kbps = 2000},
    {.name = "first-DATA-loss", .delay = 10, .kbps = 2000, .fault = 1},
    {.name = "burst-and-retry-loss", .delay = 10, .kbps = 2000, .fault = 2},
    {.name = "reorder-and-duplicate", .delay = 10, .jitter = 5, .reorder = 60,
     .duplicate = 10, .kbps = 4000},
    {.name = "DATA10%-SACK10%", .loss = 10, .ack_loss = 10, .delay = 20,
     .jitter = 40, .kbps = 2000},
    {.name = "SACK30%-only", .ack_loss = 30, .delay = 20, .kbps = 2000},
    {.name = "DATA30%-SACK30%-512kbps", .loss = 30, .ack_loss = 30, .delay = 60,
     .jitter = 100, .reorder = 20, .duplicate = 5, .kbps = 512},
    {.name = "outage-1000ms", .delay = 20, .kbps = 2000, .outage = 1000},
    {.name = "MJPEG40KiB-loss10%-reorder", .loss = 10, .ack_loss = 10, .delay = 20,
     .jitter = 40, .reorder = 20, .kbps = 2000, .large = 1},
    {.name = "video-lifetime-100ms", .loss = 20, .ack_loss = 20, .delay = 20,
     .jitter = 40, .reorder = 20, .kbps = 2000, .fault = 1,
     .policy = 0x82, .lifetime = 100, .drop_forward = 1},
  };
  for (size_t scenario = 0; scenario < sizeof(cases) / sizeof(cases[0]); ++scenario) {
    size_t dropped = 0, retries = 0, early = 0, delivered = 0, reordered = 0, congestion = 0;
    uint32_t first_retry_max = 0, first_delivery_max = 0;
    for (unsigned seed = 1; seed <= 8; ++seed) {
      memset(&network, 0, sizeof(network));
      network.config = &cases[scenario];
      network.rng = seed;
      for (unsigned i = 0; i < 2; ++i) {
        init(&network.peer[i], network.config->policy, network.config->lifetime);
        sctp_deinit(&network.peer[i]);
        /* Match webrtc_solution: 32 retained TX chunks and a 192 KiB RX budget. */
        network.peer[i].sent_packet_limit = 32;
        network.peer[i].recv_cache_limit = 192 * 1024;
        assert(!sctp_packet_pool_init(&network.peer[i]) &&
               !sctp_tx_init(&network.peer[i]) && !sctp_rx_init(&network.peer[i]));
        network.peer[i].connected = 1;
        network.id[i] = i;
        network.peer[i].dtls_srtp = &network.transport[i];
        network.peer[i].userdata = &network.id[i];
        network.peer[i].onmessage = network_message;
        network.highest_arrived[i] = 99;
        sctp_rx_reset(&network.peer[i], 99, 1);
      }
      network.started = now_ms;
      size_t allocations = heap_allocations;
      network_active = 1;
      for (; now_ms - network.started < 60000; now_ms += 5) {
        network_deliver();
        int complete = !network.queued;
        for (unsigned i = 0; i < 2; ++i) {
          network_submit(i);
          sctp_process_timers(&network.peer[i]);
          complete &= network.submitted[i] == NETWORK_MESSAGES &&
                      !network.peer[i].tx->count && !network.peer[i].rx->count;
        }
        if (complete && !network.queued) break;
      }
      if (now_ms - network.started >= 60000) {
        fprintf(stderr, "network timeout: case=%s seed=%u sent=%u/%u recv=%u/%u tx=%zu/%zu rx=%zu/%zu queued=%zu\n",
                network.config->name, seed, network.submitted[0], network.submitted[1],
                network.delivered[0], network.delivered[1], network.peer[0].tx->count,
                network.peer[1].tx->count, network.peer[0].rx->count,
                network.peer[1].rx->count, network.queued);
        assert(!"weak-network test failed to drain");
      }
      assert(heap_allocations == allocations && !network.queued && !fixture_live);
      for (unsigned i = 0; i < 2; ++i) {
        Sctp* peer = &network.peer[i];
        assert(!peer->tx->count && !peer->tx->message_pool.used);
        assert(!peer->rx->count && !peer->rx->bytes && !peer->rx->fragment_pool.used);
        assert(!peer->rx->coalesce_pool.used && !peer->packets->used);
        if (!network.config->policy) assert(network.delivered[i] == NETWORK_MESSAGES);
        else assert(network.delivered[i] > 0);
        delivered += network.delivered[i];
      }
      if (network.config->fault) {
        assert(network.forced[0]);
        if (!network.config->policy) {
          assert(network.early_retransmits && network.first_retry_ms < 300);
        }
      }
      if (network.config->fault == 2) {
        assert(network.forced[0] == 2 && network.forced[1] == 1 && network.forced[2] == 1);
        assert(network.retransmits > network.early_retransmits);
      }
      if (network.config->loss || network.config->outage) assert(network.dropped);
      if (network.config->ack_loss) assert(network.ack_dropped);
      if (network.config->reorder) assert(network.reordered);
      if (network.config->duplicate) assert(network.duplicates);
      if (network.config->drop_forward) {
        for (unsigned i = 0; i < 2; ++i) {
          assert(network.forward_dropped[i] == 1 && network.recovered[i]);
        }
      }
      dropped += network.dropped;
      retries += network.retransmits;
      early += network.early_retransmits;
      reordered += network.reordered;
      congestion += network.congestion_dropped;
      if (network.first_retry_ms > first_retry_max) first_retry_max = network.first_retry_ms;
      if (network.first_delivery_ms[1] > first_delivery_max) {
        first_delivery_max = network.first_delivery_ms[1];
      }
      network_active = 0;
      destroy(&network.peer[0]);
      destroy(&network.peer[1]);
    }
    printf("network: %-26s seeds=8 messages=%zu/1024 dropped=%zu congestion=%zu reordered=%zu retries=%zu early=%zu first_retry<=%ums first_frame<=%ums PASS\n",
           cases[scenario].name, delivered, dropped, congestion, reordered, retries, early,
           first_retry_max, first_delivery_max);
  }
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  test_reliable();
  test_gap_reneging_and_owner();
  test_sack_gap_retransmit();
  test_standard_fast_retransmit();
  test_fast_retransmit_partial_reliability();
  test_fast_retransmit_reneging();
  test_fast_retransmit_recovery();
  test_fast_retransmit_wrap();
  test_fast_retransmit_deferred();
  test_ring_capacity_and_destroy();
  test_tx_pools_are_independent();
  test_partial_reliability();
  test_receive();
  test_receive_fragment_callback_with_retries();
  test_receive_coalescing_pool();
  test_receive_shared_owner_charge();
  test_receive_fragment_pool_recovery();
  test_receive_pool_follows_cache_budget();
  test_receive_failures_and_forward();
  test_receive_full_cache_recovers_lowest_hole();
  test_forward_discards_whole_message_across_holes();
  test_forward_preserves_received_tail();
  test_forward_discards_late_orphan_tails();
  test_orphan_tail_after_complete_boundary();
  test_receive_window_precedes_duplicate_bitmap();
  test_handshake_retries_and_cookie();
  test_cookie_ack_precedes_reentrant_open_and_recreate();
  test_receive_size_limit_aborts();
  test_receive_192k_chrome_fragment_sizes();
  test_receive_irrecoverable_storage_aborts();
  test_receive_shared_pool_pressure_recovers_hole();
  test_sdp_matches_receive_budget();
  test_raw_control_ack_releases_full_pool();
  test_abort_is_terminal();
  test_gap_acked_retry_limit_does_not_abandon_message();
  test_single_message_callback_destroy();
  test_coalesced_message_callback_destroy();
  test_fragment_callback_destroy_stops_delivery();
  test_partial_reliability_initial_write_failure();
  test_network_impairments();
  puts("SCTP ownership, retransmission, reassembly and malformed-input tests passed");
  return 0;
}
