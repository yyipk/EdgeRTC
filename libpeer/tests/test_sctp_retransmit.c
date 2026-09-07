#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/sctp.c"

#define TEST_PAYLOAD_MAX (SCTP_MTU - sizeof(SctpPacket) - sizeof(SctpDataChunk))

static uint32_t test_now_ms;
static uint32_t sent_tsns[16];
static size_t sent_count;
static uint32_t forwarded_tsn;
static size_t forward_count;
static int fail_next_send;

int dtls_srtp_write(DtlsSrtp* dtls_srtp, const uint8_t* buf, size_t len) {
  const SctpPacket* packet = (const SctpPacket*)buf;
  const SctpDataChunk* chunk = (const SctpDataChunk*)packet->chunks;

  (void)dtls_srtp;
  assert(len >= sizeof(SctpHeader) + sizeof(SctpChunkCommon));
  if (chunk->type == SCTP_FORWARD_TSN) {
    const SctpForwardTsnChunk* forward =
        (const SctpForwardTsnChunk*)packet->chunks;

    assert(len == sizeof(SctpHeader) + sizeof(SctpForwardTsnChunk));
    forwarded_tsn = ntohl(forward->new_cumulative_tsn);
    forward_count++;
    return (int)len;
  }
  assert(chunk->type == SCTP_DATA);
  assert(len >= sizeof(SctpHeader) + sizeof(SctpDataChunk));
  assert(sent_count < sizeof(sent_tsns) / sizeof(sent_tsns[0]));
  sent_tsns[sent_count++] = ntohl(chunk->tsn);
  if (fail_next_send) {
    fail_next_send = 0;
    return -1;
  }
  return (int)len;
}

uint32_t ports_get_epoch_time(void) {
  return test_now_ms;
}

uint32_t ports_get_time_ms(void) {
  return test_now_ms;
}

static size_t cached_packet_count(Sctp* sctp) {
  SctpSentPacket* packet;
  size_t count = 0;

  for (packet = sctp->sent_packets; packet != NULL; packet = packet->next) {
    count++;
  }
  return count;
}

static SctpSackChunk* make_sack(uint8_t* storage, uint32_t cumulative_tsn,
                                uint16_t gap_start, uint16_t gap_end) {
  SctpSackChunk* sack = (SctpSackChunk*)storage;
  uint16_t value;

  memset(storage, 0, sizeof(SctpSackChunk) + 4);
  sack->common.type = SCTP_SACK;
  sack->common.length = htons(sizeof(SctpSackChunk) +
                              (gap_start == 0 ? 0 : 4));
  sack->cumulative_tsn_ack = htonl(cumulative_tsn);
  if (gap_start != 0) {
    sack->number_of_gap_ack_blocks = htons(1);
    value = htons(gap_start);
    memcpy(sack->blocks, &value, sizeof(value));
    value = htons(gap_end);
    memcpy(sack->blocks + sizeof(value), &value, sizeof(value));
  }
  return sack;
}

int main(void) {
  uint8_t payload[TEST_PAYLOAD_MAX * 3] = {0};
  uint8_t sack_storage[sizeof(SctpSackChunk) + 4];
  DtlsSrtp dtls_srtp = {0};
  Sctp sctp = {0};
  SctpSackChunk* sack;

  sctp.dtls_srtp = &dtls_srtp;
  sctp.local_port = 5000;
  sctp.remote_port = 5000;
  sctp.tsn = 1234;
  sctp.sent_packet_limit = 3;
  sctp.last_sack_tsn = sctp.tsn - 1;

  test_now_ms = 1000;
  assert(sctp_outgoing_data(&sctp, (char*)payload, sizeof(payload),
                            PPID_BINARY, 0, SCTP_DATA_COPY, 0, 0) ==
         sizeof(payload));
  assert(sent_count == 3);
  assert(cached_packet_count(&sctp) == 3);
  assert(sctp.sent_packet_count == 3);
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) < 0);
  assert(sent_count == 3);

  test_now_ms = 1301;
  sack = make_sack(sack_storage, 1234, 2, 2);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(sent_count == 4);
  assert(sent_tsns[3] == 1235);
  assert(cached_packet_count(&sctp) == 1);

  sack = make_sack(sack_storage, 1236, 0, 0);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(cached_packet_count(&sctp) == 0);

  test_now_ms = 2000;
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == 1);
  assert(sent_count == 5);
  sctp.last_retransmit_check_ms = test_now_ms;
  test_now_ms = 2150;
  sctp_process_timers(&sctp);
  assert(sent_count == 5);
  test_now_ms = 2301;
  sctp_process_timers(&sctp);
  assert(sent_count == 6);
  assert(sent_tsns[5] == 1237);

  sack = make_sack(sack_storage, 1236, 1, 1);
  sack->common.length = htons(sizeof(SctpSackChunk));
  sctp_handle_sack(&sctp, sack, sizeof(SctpSackChunk));
  assert(cached_packet_count(&sctp) == 1);

  sack = make_sack(sack_storage, 1237, 0, 0);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(cached_packet_count(&sctp) == 0);

  test_now_ms = 3000;
  sctp.stream_count = 1;
  sctp.stream_table[0].sid = 0;
  sctp.stream_table[0].no_retransmit = 1;
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == 1);
  assert(cached_packet_count(&sctp) == 0);

  sctp.stream_table[0].no_retransmit = 0;
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == 1);
  assert(cached_packet_count(&sctp) == 1);
  sack = make_sack(sack_storage, 1239, 0, 0);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(cached_packet_count(&sctp) == 0);

  test_now_ms = 4000;
  fail_next_send = 1;
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) < 0);
  assert(cached_packet_count(&sctp) == 1);
  sctp.last_retransmit_check_ms = test_now_ms;
  test_now_ms = 4301;
  sctp_process_timers(&sctp);
  assert(sent_tsns[sent_count - 1] == 1240);

  sctp_destroy_association(&sctp);
  assert(sctp.sent_packets == NULL);
  assert(sctp.sent_packets_tail == NULL);
  assert(sctp.sent_packet_count == 0);

  memset(&sctp, 0, sizeof(sctp));
  sctp.dtls_srtp = &dtls_srtp;
  sctp.local_port = 5000;
  sctp.remote_port = 5000;
  sctp.tsn = 2000;
  sctp.last_sack_tsn = sctp.tsn - 1;
  sctp.last_pr_tsn = sctp.tsn - 1;
  sctp.peer_supports_forward_tsn = 1;
  sctp.stream_count = 1;
  sctp.stream_table[0].sid = 0;
  sctp.stream_table[0].no_retransmit = 1;
  test_now_ms = 5000;
  assert(sctp_outgoing_data(&sctp, (char*)payload,
                            TEST_PAYLOAD_MAX * 3, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == TEST_PAYLOAD_MAX * 3);
  sack = make_sack(sack_storage, 2000, 2, 2);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(sctp.forward_state == SCTP_FORWARD_STATE_GAP_WAIT);
  test_now_ms += SCTP_GAP_WAIT_MS + 1;
  sctp_process_timers(&sctp);
  assert(forward_count == 1);
  assert(forwarded_tsn == 2002);
  assert(sctp.forward_state == SCTP_FORWARD_STATE_PENDING);
  test_now_ms += SCTP_FORWARD_RETRY_MS;
  sctp_process_timers(&sctp);
  assert(forward_count == 2);
  assert(sctp.forward_state == SCTP_FORWARD_STATE_PENDING);
  sack = make_sack(sack_storage, forwarded_tsn, 0, 0);
  sctp_handle_sack(&sctp, sack, ntohs(sack->common.length));
  assert(sctp.forward_state == SCTP_FORWARD_STATE_IDLE);

  memset(&sctp, 0, sizeof(sctp));
  sctp.dtls_srtp = &dtls_srtp;
  sctp.local_port = 5000;
  sctp.remote_port = 5000;
  assert(sctp_outgoing_data(&sctp, (char*)payload, 1, PPID_BINARY, 0,
                            SCTP_DATA_COPY, 0, 0) == 1);
  assert(sctp.sent_packet_count == 0);
  return 0;
}
