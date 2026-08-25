#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "agent.h"

#define TEST_TURN_CHANNEL 0x4000

static const char* test_username = "turn-user";
static const char* test_password = "turn-password";
static const char* test_realm = "turn.test";
static const char* test_nonce = "test-nonce";
static const uint8_t test_outbound[] = {
    0x80, 0x60, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
};

typedef struct TestTurnServer {
  int fd;
  uint16_t port;
  int status;
} TestTurnServer;

static int test_recv_request(TestTurnServer* server, StunMessage* request,
                             struct sockaddr_in* client,
                             StunClass expected_class,
                             StunMethod expected_method, int authenticated) {
  socklen_t client_len = sizeof(*client);
  int size;

  memset(request, 0, sizeof(*request));
  size = recvfrom(server->fd, request->buf, sizeof(request->buf), 0,
                  (struct sockaddr*)client, &client_len);
  if (size <= 0) {
    return -1;
  }
  request->size = size;
  if (stun_parse_msg_buf(request) < 0 ||
      request->stunclass != expected_class ||
      request->stunmethod != expected_method) {
    return -1;
  }
  if (authenticated &&
      stun_msg_is_valid_long_term(request->buf, request->size,
                                  test_username, test_realm,
                                  test_password) < 0) {
    return -1;
  }
  return 0;
}

static int test_send_data_indication(TestTurnServer* server,
                                     const struct sockaddr_in* client,
                                     const Address* peer) {
  uint8_t payload[CONFIG_MTU];
  StunMessage indication;
  int i;
  int size;

  for (i = 0; i < CONFIG_MTU; i++) {
    payload[i] = (uint8_t)i;
  }
  stun_msg_create(&indication, STUN_CLASS_INDICATION | STUN_METHOD_DATA);
  if (stun_msg_write_xor_address(&indication,
                                 STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                                 (Address*)peer) < 0 ||
      stun_msg_write_attr(&indication, STUN_ATTR_TYPE_DATA,
                          sizeof(payload), (char*)payload) < 0) {
    return -1;
  }
  size = sendto(server->fd, indication.buf, indication.size, 0,
                (const struct sockaddr*)client, sizeof(*client));
  return size == (int)indication.size ? 0 : -1;
}

static int test_send_response(TestTurnServer* server,
                              const StunMessage* request,
                              const struct sockaddr_in* client,
                              StunMethod method, int allocate) {
  StunMessage response;
  Address address = {0};
  uint32_t lifetime = htonl(600);
  int size;

  stun_msg_create(&response, STUN_CLASS_RESPONSE | method);
  memcpy(((StunHeader*)response.buf)->transaction_id,
         ((const StunHeader*)request->buf)->transaction_id,
         sizeof(((StunHeader*)response.buf)->transaction_id));
  if (allocate) {
    assert(addr_from_string("127.0.0.1", &address) == 1);
    addr_set_port(&address, 55000);
    if (stun_msg_write_xor_address(&response,
                                   STUN_ATTR_TYPE_XOR_RELAYED_ADDRESS,
                                   &address) < 0 ||
        stun_msg_write_xor_address(&response,
                                   STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS,
                                   &address) < 0 ||
        stun_msg_write_attr(&response, STUN_ATTR_TYPE_LIFETIME,
                            sizeof(lifetime), (char*)&lifetime) < 0) {
      return -1;
    }
  }
  snprintf(response.username, sizeof(response.username), "%s",
           test_username);
  snprintf(response.realm, sizeof(response.realm), "%s", test_realm);
  if (stun_msg_finish(&response, STUN_CREDENTIAL_LONG_TERM,
                      test_password, strlen(test_password)) < 0) {
    return -1;
  }
  size = sendto(server->fd, response.buf, response.size, 0,
                (const struct sockaddr*)client, sizeof(*client));
  return size == (int)response.size ? 0 : -1;
}

static int test_send_challenge(TestTurnServer* server,
                               const StunMessage* request,
                               const struct sockaddr_in* client) {
  StunMessage response;
  uint8_t error_code[4] = {0, 0, 4, 1};
  int size;

  stun_msg_create(&response, STUN_CLASS_ERROR | STUN_METHOD_ALLOCATE);
  memcpy(((StunHeader*)response.buf)->transaction_id,
         ((const StunHeader*)request->buf)->transaction_id,
         sizeof(((StunHeader*)response.buf)->transaction_id));
  if (stun_msg_write_attr(&response, STUN_ATTR_TYPE_ERROR_CODE,
                          sizeof(error_code), (char*)error_code) < 0 ||
      stun_msg_write_attr(&response, STUN_ATTR_TYPE_REALM,
                          strlen(test_realm), (char*)test_realm) < 0 ||
      stun_msg_write_attr(&response, STUN_ATTR_TYPE_NONCE,
                          strlen(test_nonce), (char*)test_nonce) < 0) {
    return -1;
  }
  size = sendto(server->fd, response.buf, response.size, 0,
                (const struct sockaddr*)client, sizeof(*client));
  return size == (int)response.size ? 0 : -1;
}

static int test_channel_data(TestTurnServer* server,
                             const struct sockaddr_in* client) {
  uint8_t packet[CONFIG_MTU + 4];
  int padded_size = ALIGN32(sizeof(test_outbound));
  uint16_t value;
  int size;

  size = recv(server->fd, packet, sizeof(packet), 0);
  if (size != padded_size + 4) {
    return -1;
  }
  memcpy(&value, packet, sizeof(value));
  if (ntohs(value) != TEST_TURN_CHANNEL) {
    return -1;
  }
  memcpy(&value, packet + 2, sizeof(value));
  if (ntohs(value) != sizeof(test_outbound) ||
      memcmp(packet + 4, test_outbound, sizeof(test_outbound)) != 0 ||
      packet[size - 1] != 0) {
    return -1;
  }
  value = htons(TEST_TURN_CHANNEL);
  memcpy(packet, &value, sizeof(value));
  value = htons(sizeof(test_outbound));
  memcpy(packet + 2, &value, sizeof(value));
  memcpy(packet + 4, test_outbound, sizeof(test_outbound));
  size = sendto(server->fd, packet, sizeof(test_outbound) + 4, 0,
                (const struct sockaddr*)client, sizeof(*client));
  return size == (int)sizeof(test_outbound) + 4 ? 0 : -1;
}

static void* test_turn_server_run(void* argument) {
  TestTurnServer* server = argument;
  struct sockaddr_in client;
  Address peer;
  StunMessage request;

  server->status = -1;
  if (test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_ALLOCATE, 0) < 0 ||
      test_send_challenge(server, &request, &client) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_ALLOCATE, 1) < 0 ||
      test_send_response(server, &request, &client,
                         STUN_METHOD_ALLOCATE, 1) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_CREATE_PERMISSION, 1) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_CREATE_PERMISSION, 1) < 0 ||
      test_send_response(server, &request, &client,
                         STUN_METHOD_CREATE_PERMISSION, 0) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_INDICATION,
                        STUN_METHOD_SEND, 0) < 0 ||
      request.data_size != sizeof(test_outbound) ||
      memcmp(request.data, test_outbound, sizeof(test_outbound)) != 0) {
    return NULL;
  }
  peer = request.peer_addr;
  if (test_send_data_indication(server, &client, &peer) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_REFRESH, 1) < 0 ||
      request.lifetime != 600 ||
      test_send_response(server, &request, &client,
                         STUN_METHOD_REFRESH, 0) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_INDICATION,
                        STUN_METHOD_SEND, 0) < 0 ||
      request.data_size != sizeof(test_outbound) ||
      memcmp(request.data, test_outbound, sizeof(test_outbound)) != 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_CHANNEL_BIND, 1) < 0 ||
      request.channel_number != TEST_TURN_CHANNEL ||
      test_send_response(server, &request, &client,
                         STUN_METHOD_CHANNEL_BIND, 0) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_INDICATION,
                        STUN_METHOD_SEND, 0) < 0 ||
      request.data_size != sizeof(test_outbound) ||
      memcmp(request.data, test_outbound, sizeof(test_outbound)) != 0 ||
      test_channel_data(server, &client) < 0 ||
      test_recv_request(server, &request, &client, STUN_CLASS_REQUEST,
                        STUN_METHOD_REFRESH, 1) < 0 ||
      request.lifetime != 0) {
    return NULL;
  }
  server->status = 0;
  return NULL;
}

static void test_turn_server_open(TestTurnServer* server) {
  struct sockaddr_in address = {0};
  struct timeval timeout = {.tv_sec = 2};
  socklen_t address_len = sizeof(address);

  server->fd = socket(AF_INET, SOCK_DGRAM, 0);
  assert(server->fd >= 0);
  assert(setsockopt(server->fd, SOL_SOCKET, SO_RCVTIMEO,
                    &timeout, sizeof(timeout)) == 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(server->fd, (struct sockaddr*)&address, sizeof(address)) == 0);
  assert(getsockname(server->fd, (struct sockaddr*)&address,
                     &address_len) == 0);
  server->port = ntohs(address.sin_port);
}

int main(void) {
  uint8_t inbound[CONFIG_MTU];
  TestTurnServer server = {.fd = -1};
  IceCandidate* relay;
  IceCandidate* remote;
  IceCandidatePair* pair;
  pthread_t thread;
  Agent agent;
  char url[64];
  int packet_consumed;
  int received = 0;
  int i;

  test_turn_server_open(&server);
  assert(pthread_create(&thread, NULL, test_turn_server_run, &server) == 0);
  assert(agent_create(&agent) == 0);
  snprintf(url, sizeof(url), "turn:127.0.0.1:%u", server.port);
  agent_gather_candidate(&agent, url, test_username, test_password);
  for (i = 0; i < 2000 && !agent.turn.allocated; i++) {
    agent_recv(&agent, inbound, sizeof(inbound), &packet_consumed);
  }
  assert(agent.turn.allocated);
  assert(agent.local_candidates_count == 1);
  relay = &agent.local_candidates[0];
  assert(relay->type == ICE_CANDIDATE_TYPE_RELAY);

  remote = &agent.remote_candidates[0];
  assert(addr_from_string("192.0.2.1", &remote->addr) == 1);
  addr_set_port(&remote->addr, 5004);
  ice_candidate_create(remote, 1, ICE_CANDIDATE_TYPE_HOST, &remote->addr);
  pair = &agent.candidate_pairs[0];
  pair->local = relay;
  pair->remote = remote;
  agent.nominated_pair = pair;
  assert(agent_send(&agent, test_outbound, sizeof(test_outbound)) < 0);
  for (i = 0; i < 2000 && !agent.turn.permission_active; i++) {
    agent_recv(&agent, inbound, sizeof(inbound), &packet_consumed);
    if (!agent.turn.permission_active) {
      agent_send(&agent, test_outbound, sizeof(test_outbound));
    }
  }
  assert(agent.turn.permission_active);
  assert(agent_send(&agent, test_outbound, sizeof(test_outbound)) ==
         sizeof(test_outbound));

  for (i = 0; i < 2000 && received == 0; i++) {
    packet_consumed = 0;
    received = agent_recv(&agent, inbound, sizeof(inbound),
                          &packet_consumed);
  }
  assert(received == CONFIG_MTU);
  for (i = 0; i < CONFIG_MTU; i++) {
    assert(inbound[i] == (uint8_t)i);
  }

  agent.turn.last_refresh_ms = 0;
  agent.selected_pair = pair;
  assert(agent_send(&agent, test_outbound, sizeof(test_outbound)) ==
         sizeof(test_outbound));
  for (i = 0; i < 2000 && agent.turn.request_method != 0; i++) {
    agent_recv(&agent, inbound, sizeof(inbound), &packet_consumed);
  }
  assert(agent.turn.request_method == 0);
  assert(agent_send(&agent, test_outbound, sizeof(test_outbound)) ==
         sizeof(test_outbound));
  for (i = 0; i < 2000 && !agent.turn.channel_bound; i++) {
    agent_recv(&agent, inbound, sizeof(inbound), &packet_consumed);
  }
  assert(agent.turn.channel_bound);
  assert(agent_send(&agent, test_outbound, sizeof(test_outbound)) ==
         sizeof(test_outbound));

  received = 0;
  for (i = 0; i < 2000 && received == 0; i++) {
    packet_consumed = 0;
    received = agent_recv(&agent, inbound, sizeof(inbound),
                          &packet_consumed);
  }
  assert(received == sizeof(test_outbound));
  assert(memcmp(inbound, test_outbound, sizeof(test_outbound)) == 0);
  agent_destroy(&agent);
  assert(pthread_join(thread, NULL) == 0);
  assert(server.status == 0);
  close(server.fd);
  puts("TURN UDP integration test passed");
  return 0;
}
