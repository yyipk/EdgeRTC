#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "agent.h"

void agent_process_stun_response(Agent* agent, StunMessage* stun_msg,
                                 Address* addr);

static void test_address(Address* address, const char* ip, uint16_t port) {
  memset(address, 0, sizeof(*address));
  assert(addr_from_string(ip, address) == 1);
  addr_set_port(address, port);
}

static void test_create_candidate(IceCandidate* candidate, int foundation,
                                  IceCandidateType type, const char* ip,
                                  uint16_t port,
                                  uint16_t local_preference) {
  Address address;

  test_address(&address, ip, port);
  ice_candidate_create(candidate, foundation, type, &address,
                       local_preference);
}

static void test_socket_fd_zero_is_closed(void) {
  UdpSocket socket = {.fd = 0};
  int pipefd[2];
  int saved_stdin;

  saved_stdin = dup(STDIN_FILENO);
  assert(saved_stdin >= 0);
  assert(close(STDIN_FILENO) == 0);
  assert(pipe(pipefd) == 0);
  assert(pipefd[0] == STDIN_FILENO);
  udp_socket_close(&socket);
  errno = 0;
  assert(fcntl(STDIN_FILENO, F_GETFD) == -1 && errno == EBADF);
  assert(socket.fd == -1);
  assert(close(pipefd[1]) == 0);
  assert(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
  assert(close(saved_stdin) == 0);
}

static void test_candidate_priority(void) {
  IceCandidate candidates[6];

  test_create_candidate(&candidates[0], 0, ICE_CANDIDATE_TYPE_HOST,
                        "192.0.2.1", 10000, UINT16_MAX);
  test_create_candidate(&candidates[1], 1, ICE_CANDIDATE_TYPE_HOST,
                        "192.0.2.1", 20000, UINT16_MAX);
  test_create_candidate(&candidates[2], 2, ICE_CANDIDATE_TYPE_PRFLX,
                        "192.0.2.1", 10000, UINT16_MAX);
  test_create_candidate(&candidates[3], 3, ICE_CANDIDATE_TYPE_SRFLX,
                        "192.0.2.1", 10000, UINT16_MAX);
  test_create_candidate(&candidates[4], 4, ICE_CANDIDATE_TYPE_SRFLX,
                        "192.0.2.1", 10000, UINT16_MAX - 1);
  test_create_candidate(&candidates[5], 5, ICE_CANDIDATE_TYPE_RELAY,
                        "192.0.2.1", 10000, UINT16_MAX);

  assert(candidates[0].priority == candidates[1].priority);
  assert(candidates[0].priority > candidates[2].priority);
  assert(candidates[2].priority > candidates[3].priority);
  assert(candidates[3].priority > candidates[4].priority);
  assert(candidates[4].priority > candidates[5].priority);
}

static void test_remote_candidate_identity(void) {
  Agent agent = {0};
  char first[] =
      "a=candidate:1 1 UDP 2122260223 192.0.2.1 5000 typ host";
  char same_foundation[] =
      "a=candidate:1 1 UDP 2122260223 192.0.2.1 5001 typ host";
  char same_candidate[] =
      "a=candidate:2 1 udp 2122260223 192.0.2.1 5000 typ host";
  char different_type[] =
      "a=candidate:3 1 UDP 1686052607 192.0.2.1 5000 typ srflx";

  assert(agent_add_remote_candidate(&agent, first,
                                    first + strlen(first)) == 0);
  assert(agent_add_remote_candidate(
             &agent, same_foundation,
             same_foundation + strlen(same_foundation)) == 0);
  assert(agent.remote_candidates_count == 2);
  assert(agent_add_remote_candidate(
             &agent, same_candidate,
             same_candidate + strlen(same_candidate)) == 0);
  assert(agent.remote_candidates_count == 2);
  assert(agent_add_remote_candidate(
             &agent, different_type,
             different_type + strlen(different_type)) == 0);
  assert(agent.remote_candidates_count == 3);
}

static void test_equivalent_candidate_pairs(void) {
  Agent agent = {0};
  int direct_pairs = 0;
  int relay_pairs = 0;
  int i;

  agent.mode = AGENT_MODE_CONTROLLING;
  test_create_candidate(&agent.local_candidates[0], 0,
                        ICE_CANDIDATE_TYPE_HOST, "192.168.1.2", 40000,
                        UINT16_MAX);
  test_create_candidate(&agent.local_candidates[1], 1,
                        ICE_CANDIDATE_TYPE_SRFLX, "198.51.100.1", 40000,
                        UINT16_MAX);
  test_create_candidate(&agent.local_candidates[2], 2,
                        ICE_CANDIDATE_TYPE_SRFLX, "198.51.100.2", 40000,
                        UINT16_MAX - 1);
  test_create_candidate(&agent.local_candidates[3], 3,
                        ICE_CANDIDATE_TYPE_RELAY, "203.0.113.1", 50000,
                        UINT16_MAX);
  agent.local_candidates_count = 4;

  test_create_candidate(&agent.remote_candidates[0], 0,
                        ICE_CANDIDATE_TYPE_SRFLX, "198.51.100.10", 41000,
                        UINT16_MAX);
  test_create_candidate(&agent.remote_candidates[1], 1,
                        ICE_CANDIDATE_TYPE_PRFLX, "198.51.100.11", 42000,
                        UINT16_MAX);
  agent.remote_candidates_count = 2;

  agent_update_candidate_pairs(&agent);
  assert(agent.local_candidates_count == 4);
  assert(agent.candidate_pairs_num == 4);
  for (i = 0; i < agent.candidate_pairs_num; i++) {
    if (agent.candidate_pairs[i].local->type == ICE_CANDIDATE_TYPE_RELAY) {
      relay_pairs++;
    } else {
      direct_pairs++;
      assert(agent.candidate_pairs[i].local == &agent.local_candidates[0]);
    }
  }
  assert(direct_pairs == 2);
  assert(relay_pairs == 2);
}

static void test_delayed_consent_response(void) {
  static const uint32_t delayed_transaction_id[3] = {1, 2, 3};
  static const uint32_t current_transaction_id[3] = {4, 5, 6};
  Agent agent = {0};
  IceCandidatePair pair = {0};
  StunMessage response;
  StunHeader* header;
  uint32_t received_ms;

  test_create_candidate(&agent.local_candidates[0], 0,
                        ICE_CANDIDATE_TYPE_HOST, "192.0.2.1", 5000,
                        UINT16_MAX);
  test_create_candidate(&agent.remote_candidates[0], 1,
                        ICE_CANDIDATE_TYPE_SRFLX, "198.51.100.1", 6000,
                        UINT16_MAX);
  pair.local = &agent.local_candidates[0];
  pair.remote = &agent.remote_candidates[0];
  agent.selected_pair = &pair;
  memcpy(agent.remote_upwd, "remote-password", sizeof("remote-password"));
  memcpy(agent.consent_transaction_ids[0], delayed_transaction_id,
         sizeof(delayed_transaction_id));
  memcpy(agent.consent_transaction_ids[1], current_transaction_id,
         sizeof(current_transaction_id));

  stun_msg_create(&response, STUN_CLASS_RESPONSE | STUN_METHOD_BINDING);
  header = (StunHeader*)response.buf;
  memcpy(header->transaction_id, delayed_transaction_id,
         sizeof(delayed_transaction_id));
  assert(stun_msg_finish(&response, STUN_CREDENTIAL_SHORT_TERM,
                         agent.remote_upwd,
                         strlen(agent.remote_upwd)) == 0);
  assert(stun_parse_msg_buf(&response) == 0);

  agent_process_stun_response(&agent, &response,
                              &agent.remote_candidates[0].addr);
  assert(agent.consent_responses_received == 1);
  assert(agent.consent_last_received_ms != 0);
  assert(memcmp(agent.consent_transaction_ids[0],
                delayed_transaction_id,
                sizeof(delayed_transaction_id)) != 0);
  assert(memcmp(agent.consent_transaction_ids[1],
                current_transaction_id,
                sizeof(current_transaction_id)) == 0);

  received_ms = agent.consent_last_received_ms;
  agent_process_stun_response(&agent, &response,
                              &agent.remote_candidates[0].addr);
  assert(agent.consent_responses_received == 1);
  assert(agent.consent_last_received_ms == received_ms);
}

int main(void) {
  test_socket_fd_zero_is_closed();
  test_candidate_priority();
  test_remote_candidate_identity();
  test_equivalent_candidate_pairs();
  test_delayed_consent_response();
  return 0;
}
