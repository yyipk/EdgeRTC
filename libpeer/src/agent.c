#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "agent.h"
#include "base64.h"
#include "edgertc_stats.h"
#include "ice.h"
#include "nat_pmp.h"
#include "ports.h"
#include "socket.h"
#include "stun.h"
#include "utils.h"

#define AGENT_POLL_TIMEOUT 1
#define AGENT_CONNCHECK_TIMEOUT_MS 1000
#define AGENT_CONNCHECK_INTERVAL_MS 100
#define AGENT_TURN_CHANNEL_NUMBER 0x4000
#define AGENT_TURN_DEFAULT_LIFETIME 600
#define AGENT_TURN_PERMISSION_REFRESH_MS 240000
#define AGENT_TURN_CHANNEL_REFRESH_MS 480000
#define AGENT_TURN_REQUEST_TIMEOUT_MS 500
#define AGENT_STUN_RECV_MAXTIMES 1000
#define AGENT_NAT_PMP_LIFETIME 7200

static int agent_turn_refresh(Agent* agent, uint32_t lifetime);
static void agent_delete_nat_pmp_mapping(Agent* agent);

static void agent_store_consent_transaction(Agent* agent,
                                            const uint32_t transaction_id[3]) {
  memcpy(agent->consent_transaction_ids[agent->consent_transaction_next],
         transaction_id, sizeof(agent->consent_transaction_ids[0]));
  agent->consent_transaction_next =
      (agent->consent_transaction_next + 1) %
      AGENT_CONSENT_TRANSACTION_COUNT;
}

static int agent_take_consent_transaction(Agent* agent,
                                          const uint32_t transaction_id[3]) {
  int i;

  for (i = 0; i < AGENT_CONSENT_TRANSACTION_COUNT; i++) {
    if (memcmp(agent->consent_transaction_ids[i], transaction_id,
               sizeof(agent->consent_transaction_ids[i])) == 0) {
      memset(agent->consent_transaction_ids[i], 0,
             sizeof(agent->consent_transaction_ids[i]));
      return 1;
    }
  }
  return 0;
}

static const char* agent_candidate_type_name(IceCandidateType type) {
  switch (type) {
    case ICE_CANDIDATE_TYPE_HOST:
      return "host";
    case ICE_CANDIDATE_TYPE_SRFLX:
      return "srflx";
    case ICE_CANDIDATE_TYPE_PRFLX:
      return "prflx";
    case ICE_CANDIDATE_TYPE_RELAY:
      return "relay";
    default:
      return "unknown";
  }
}

static void agent_log_candidate_pair(const char* action, IceCandidatePair* pair) {
  char local_addr[ADDRSTRLEN];
  char remote_addr[ADDRSTRLEN];

  addr_to_string(&pair->local->addr, local_addr, sizeof(local_addr));
  addr_to_string(&pair->remote->addr, remote_addr, sizeof(remote_addr));
  LOGI("ICE %s pair: local=%s %s:%d, remote=%s %s:%d", action,
       agent_candidate_type_name(pair->local->type),
       local_addr, pair->local->addr.port,
       agent_candidate_type_name(pair->remote->type),
       remote_addr, pair->remote->addr.port);
}

static uint64_t agent_candidate_pair_priority(Agent* agent, IceCandidate* local, IceCandidate* remote) {
  if (agent->mode == AGENT_MODE_CONTROLLING) {
    return ice_candidate_pair_rfc8445(local->priority, remote->priority);
  }
  return ice_candidate_pair_rfc8445(remote->priority, local->priority);
}

static IceCandidate* agent_find_local_candidate(Agent* agent, Address* addr,
                                                int via_turn) {
  int i;
  IceCandidateType type = via_turn ? ICE_CANDIDATE_TYPE_RELAY
                                   : ICE_CANDIDATE_TYPE_HOST;

  for (i = 0; i < agent->local_candidates_count; i++) {
    if (agent->local_candidates[i].type == type &&
        agent->local_candidates[i].addr.family == addr->family) {
      return &agent->local_candidates[i];
    }
  }
  return NULL;
}

static IceCandidate* agent_find_remote_candidate(Agent* agent, Address* addr) {
  int i;

  for (i = 0; i < agent->remote_candidates_count; i++) {
    if (addr_equal(&agent->remote_candidates[i].addr, addr)) {
      return &agent->remote_candidates[i];
    }
  }
  return NULL;
}

static IceCandidatePair* agent_find_candidate_pair(Agent* agent, IceCandidate* local, IceCandidate* remote) {
  int i;

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].local == local &&
        agent->candidate_pairs[i].remote == remote) {
      return &agent->candidate_pairs[i];
    }
  }
  return NULL;
}

static int agent_candidate_identity_equal(const IceCandidate* first,
                                          const IceCandidate* second) {
  return first->component == second->component &&
         first->type == second->type &&
         strcmp(first->transport, second->transport) == 0 &&
         addr_equal(&first->addr, &second->addr);
}

static IceCandidatePair* agent_find_equivalent_candidate_pair(Agent* agent,
                                                              IceCandidate* local,
                                                              IceCandidate* remote) {
  int i;

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    IceCandidatePair* pair = &agent->candidate_pairs[i];
    int pair_uses_relay = pair->local->type == ICE_CANDIDATE_TYPE_RELAY;
    int candidate_uses_relay = local->type == ICE_CANDIDATE_TYPE_RELAY;

    if (pair->local->component != local->component ||
        pair->remote->component != remote->component ||
        strcmp(pair->local->transport, local->transport) != 0 ||
        strcmp(pair->remote->transport, remote->transport) != 0 ||
        !addr_equal(&pair->remote->addr, &remote->addr) ||
        pair_uses_relay != candidate_uses_relay) {
      continue;
    }
    if ((!candidate_uses_relay &&
         pair->local->addr.family == local->addr.family) ||
        (candidate_uses_relay &&
         addr_equal(&pair->local->addr, &local->addr))) {
      return pair;
    }
  }
  return NULL;
}

static IceCandidatePair* agent_queue_triggered_check(Agent* agent,
                                                     StunMessage* stun_msg,
                                                     Address* addr,
                                                     int via_turn) {
  char addr_string[ADDRSTRLEN];
  IceCandidate* local = agent_find_local_candidate(agent, addr, via_turn);
  IceCandidate* remote = agent_find_remote_candidate(agent, addr);
  IceCandidatePair* pair;

  if (local == NULL) {
    LOGW("ICE cannot create triggered pair: no local host candidate for family %d", addr->family);
    return NULL;
  }

  if (remote == NULL) {
    if (agent->remote_candidates_count >= AGENT_MAX_CANDIDATES) {
      LOGW("ICE cannot create prflx candidate: remote candidate limit reached");
      return NULL;
    }
    remote = &agent->remote_candidates[agent->remote_candidates_count];
    memset(remote, 0, sizeof(*remote));
    ice_candidate_create(remote, agent->remote_candidates_count,
                         ICE_CANDIDATE_TYPE_PRFLX, addr, UINT16_MAX);
    remote->priority = stun_msg->priority;
    agent->remote_candidates_count++;
    addr_to_string(addr, addr_string, sizeof(addr_string));
    LOGI("ICE created remote prflx candidate: %s:%d, priority=%u",
         addr_string, addr->port, (unsigned int)remote->priority);
  }

  pair = agent_find_candidate_pair(agent, local, remote);
  if (pair == NULL) {
    if (agent->candidate_pairs_num >= AGENT_MAX_CANDIDATE_PAIRS) {
      LOGW("ICE cannot create triggered pair: candidate pair limit reached");
      return NULL;
    }
    pair = &agent->candidate_pairs[agent->candidate_pairs_num++];
    memset(pair, 0, sizeof(*pair));
    pair->local = local;
    pair->remote = remote;
    pair->priority = agent_candidate_pair_priority(agent, local, remote);
    pair->state = ICE_CANDIDATE_STATE_WAITING;
    agent_log_candidate_pair("queued triggered", pair);
  } else if (pair->state == ICE_CANDIDATE_STATE_FROZEN ||
             pair->state == ICE_CANDIDATE_STATE_FAILED) {
    pair->state = ICE_CANDIDATE_STATE_WAITING;
    pair->conncheck = 0;
    agent_log_candidate_pair("queued triggered", pair);
  }

  return pair;
}

static void agent_start_candidate_pair(Agent* agent, IceCandidatePair* pair, uint32_t now) {
  agent->nominated_pair = pair;
  pair->conncheck = 0;
  pair->state = ICE_CANDIDATE_STATE_INPROGRESS;
  memset(agent->conncheck_transaction_id, 0, sizeof(agent->conncheck_transaction_id));
  agent->conncheck_start_ms = now;
  agent->conncheck_last_send_ms = now;
  agent_log_candidate_pair("checking", pair);
}

static int agent_pair_is_selectable(Agent* agent, IceCandidatePair* pair) {
  return pair != NULL && pair->state == ICE_CANDIDATE_STATE_SUCCEEDED &&
         (agent->mode == AGENT_MODE_CONTROLLING || pair->nominated);
}

static int agent_select_succeeded_pair(Agent* agent, uint32_t now) {
  if (!agent_pair_is_selectable(agent, agent->nominated_pair)) {
    return -1;
  }

  agent_log_candidate_pair("selected", agent->nominated_pair);
  agent->selected_pair = agent->nominated_pair;
  agent->consent_last_received_ms = now;
  agent->consent_last_send_ms = now;
  return 0;
}

void agent_clear_candidates(Agent* agent) {
  if (agent->turn.allocated) {
    agent_turn_refresh(agent, 0);
  }
  agent_delete_nat_pmp_mapping(agent);
  memset(agent->remote_ufrag, 0, sizeof(agent->remote_ufrag));
  memset(agent->remote_upwd, 0, sizeof(agent->remote_upwd));
  agent->local_candidates_count = 0;
  agent->remote_candidates_count = 0;
  agent->candidate_pairs_num = 0;
  agent->selected_pair = NULL;
  agent->nominated_pair = NULL;
  agent->consent_last_received_ms = 0;
  agent->consent_last_send_ms = 0;
  memset(agent->consent_transaction_ids, 0,
         sizeof(agent->consent_transaction_ids));
  agent->consent_requests_sent = 0;
  agent->consent_responses_received = 0;
  agent->consent_transaction_next = 0;
  memset(&agent->turn, 0, sizeof(agent->turn));
  memset(&agent->nat_pmp, 0, sizeof(agent->nat_pmp));
}

int agent_create(Agent* agent) {
  int ret;

  memset(agent, 0, sizeof(*agent));
  agent->udp_sockets[0].fd = -1;
  agent->udp_sockets[1].fd = -1;
  if ((ret = udp_socket_open(&agent->udp_sockets[0], AF_INET, 0)) < 0) {
    LOGE("Failed to create UDP socket.");
    return ret;
  }
  LOGI("create IPv4 UDP socket: %d", agent->udp_sockets[0].fd);

#if CONFIG_IPV6
  if ((ret = udp_socket_open(&agent->udp_sockets[1], AF_INET6, 0)) < 0) {
    LOGE("Failed to create IPv6 UDP socket.");
    udp_socket_close(&agent->udp_sockets[0]);
    return ret;
  }
  LOGI("create IPv6 UDP socket: %d", agent->udp_sockets[1].fd);
#endif

  agent_clear_candidates(agent);
  return 0;
}

void agent_destroy(Agent* agent) {
  if (agent->turn.allocated) {
    agent_turn_refresh(agent, 0);
    agent->turn.allocated = 0;
  }
  agent_delete_nat_pmp_mapping(agent);
  udp_socket_close(&agent->udp_sockets[0]);

#if CONFIG_IPV6
  udp_socket_close(&agent->udp_sockets[1]);
#endif
}

static int agent_socket_recv(Agent* agent, Address* addr, uint8_t* buf,
                             int len) {
  int ret = -1;
  int i = 0;
  int maxfd = -1;
  fd_set rfds;
  struct timeval tv;
  int addr_type[] = { AF_INET,
#if CONFIG_IPV6
                      AF_INET6,
#endif
  };

  tv.tv_sec = 0;
  tv.tv_usec = AGENT_POLL_TIMEOUT * 1000;
  FD_ZERO(&rfds);

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    if (agent->udp_sockets[i].fd > maxfd) {
      maxfd = agent->udp_sockets[i].fd;
    }
    if (agent->udp_sockets[i].fd >= 0) {
      FD_SET(agent->udp_sockets[i].fd, &rfds);
    }
  }

  ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0) {
    LOGE("select error");
  } else if (ret == 0) {
    // timeout
  } else {
    for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
      if (FD_ISSET(agent->udp_sockets[i].fd, &rfds)) {
        ret = udp_socket_recvfrom(&agent->udp_sockets[i], addr, buf, len);
        break;
      }
    }
  }

  return ret;
}

static int agent_socket_send(Agent* agent, Address* addr, const uint8_t* buf, int len) {
  switch (addr->family) {
    case AF_INET6:
      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);
    case AF_INET:
    default:
      return udp_socket_sendto(&agent->udp_sockets[0], addr, buf, len);
  }
  return -1;
}

static int agent_recv_stun_response(Agent* agent, Address* serv_addr,
                                    StunMessage* request,
                                    StunMessage* response) {
  StunHeader* request_header = (StunHeader*)request->buf;
  Address source_addr;
  int i;
  int ret;

  for (i = 0; i < AGENT_STUN_RECV_MAXTIMES; i++) {
    ret = agent_socket_recv(agent, &source_addr, response->buf,
                            sizeof(response->buf));
    if (ret <= 0) {
      continue;
    }
    if (!addr_equal(&source_addr, serv_addr) ||
        stun_probe(response->buf, ret) != 0) {
      continue;
    }
    if (memcmp(((StunHeader*)response->buf)->transaction_id,
               request_header->transaction_id,
               sizeof(request_header->transaction_id)) != 0) {
      continue;
    }
    response->size = ret;
    if (stun_parse_msg_buf(response) < 0) {
      continue;
    }
    return ret;
  }
  return 0;
}

static int agent_create_host_addr(Agent* agent) {
  int i, j;
  const char* iface_prefx[] = {CONFIG_IFACE_PREFIX};
  IceCandidate* ice_candidate;
  int addr_type[] = { AF_INET,
#if CONFIG_IPV6
                      AF_INET6,
#endif
  };

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    for (j = 0; j < sizeof(iface_prefx) / sizeof(iface_prefx[0]); j++) {
      ice_candidate = agent->local_candidates + agent->local_candidates_count;
      // only copy port and family to addr of ice candidate
      ice_candidate_create(ice_candidate, agent->local_candidates_count,
                           ICE_CANDIDATE_TYPE_HOST,
                           &agent->udp_sockets[i].bind_addr, UINT16_MAX);
      // if resolve host addr, add to local candidate
      if (ports_get_host_addr(&ice_candidate->addr, iface_prefx[j])) {
        agent->local_candidates_count++;
      }
    }
  }

  return 0;
}

static IceCandidate* agent_find_ipv4_host_candidate(Agent* agent) {
  int i;

  for (i = 0; i < agent->local_candidates_count; i++) {
    if (agent->local_candidates[i].type == ICE_CANDIDATE_TYPE_HOST &&
        agent->local_candidates[i].addr.family == AF_INET) {
      return &agent->local_candidates[i];
    }
  }
  return NULL;
}

static int agent_create_nat_pmp_addr(Agent* agent) {
  char addr_string[ADDRSTRLEN];
  IceCandidate* host = agent_find_ipv4_host_candidate(agent);
  IceCandidate* candidate;
  Address gateway;
  Address external_addr;
  uint32_t lifetime;
  uint16_t internal_port = agent->udp_sockets[0].bind_addr.port;

  if (host == NULL ||
      agent->local_candidates_count >= AGENT_MAX_CANDIDATES ||
      !ports_get_default_gateway(&gateway)) {
    return -1;
  }

  addr_set_port(&gateway, NAT_PMP_PORT);
  if (nat_pmp_map(&gateway, internal_port, internal_port,
                  AGENT_NAT_PMP_LIFETIME, &external_addr, &lifetime) < 0) {
    LOGD("NAT-PMP mapping is unavailable");
    return -1;
  }

  candidate = &agent->local_candidates[agent->local_candidates_count];
  ice_candidate_create(candidate, agent->local_candidates_count,
                       ICE_CANDIDATE_TYPE_SRFLX, &external_addr,
                       UINT16_MAX);
  candidate->raddr = host->addr;
  agent->local_candidates_count++;
  agent->nat_pmp.gateway_addr = gateway.sin.sin_addr.s_addr;
  agent->nat_pmp.internal_port = internal_port;
  agent->nat_pmp.external_port = external_addr.port;
  addr_to_string(&external_addr, addr_string, sizeof(addr_string));
  LOGI("NAT-PMP srflx candidate gathered: %s:%d, lifetime=%u seconds",
       addr_string, external_addr.port, (unsigned int)lifetime);
  return 0;
}

static void agent_delete_nat_pmp_mapping(Agent* agent) {
  Address external_addr;
  Address gateway;
  uint32_t lifetime;

  if (agent->nat_pmp.gateway_addr == 0) {
    return;
  }

  memset(&gateway, 0, sizeof(gateway));
  addr_set_family(&gateway, AF_INET);
  gateway.sin.sin_addr.s_addr = agent->nat_pmp.gateway_addr;
  addr_set_port(&gateway, NAT_PMP_PORT);
  nat_pmp_map(&gateway, agent->nat_pmp.internal_port,
              0, 0, &external_addr, &lifetime);
  memset(&agent->nat_pmp, 0, sizeof(agent->nat_pmp));
}

static int agent_create_stun_addr(Agent* agent, Address* serv_addr) {
  StunMessage* request = &agent->send_msg;
  StunMessage* response = &agent->recv_msg;
  IceCandidate* candidate;
  int ret;

  stun_msg_create(request, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);
  ret = agent_socket_send(agent, serv_addr, request->buf, request->size);
  if (ret < 0) {
    LOGE("Failed to send STUN Binding Request.");
    return ret;
  }
  ret = agent_recv_stun_response(agent, serv_addr, request, response);
  if (ret <= 0) {
    LOGD("Failed to receive STUN Binding Response.");
    return ret;
  }
  if (response->stunclass != STUN_CLASS_RESPONSE ||
      response->stunmethod != STUN_METHOD_BINDING ||
      agent->local_candidates_count >= AGENT_MAX_CANDIDATES) {
    LOGE("Invalid STUN Binding Response.");
    return -1;
  }
  candidate = &agent->local_candidates[agent->local_candidates_count];
  ice_candidate_create(candidate, agent->local_candidates_count,
                       ICE_CANDIDATE_TYPE_SRFLX, &response->mapped_addr,
                       UINT16_MAX - 1);
  agent->local_candidates_count++;
  LOGI("STUN srflx candidate gathered");
  return ret;
}

static int agent_turn_add_auth(Agent* agent, StunMessage* msg) {
  if (stun_msg_write_attr(msg, STUN_ATTR_TYPE_USERNAME,
                          strlen(agent->turn.username),
                          (char*)agent->turn.username) < 0 ||
      stun_msg_write_attr(msg, STUN_ATTR_TYPE_REALM,
                          strlen(agent->turn.realm),
                          agent->turn.realm) < 0 ||
      stun_msg_write_attr(msg, STUN_ATTR_TYPE_NONCE,
                          strlen(agent->turn.nonce),
                          agent->turn.nonce) < 0) {
    return -1;
  }
  return stun_msg_finish(msg, STUN_CREDENTIAL_LONG_TERM,
                         agent->turn.credential,
                         strlen(agent->turn.credential));
}

static void agent_turn_request_sent(Agent* agent, StunMessage* msg,
                                    StunMethod method) {
  agent->turn.request_method = method;
  agent->turn.request_sent_ms = ports_get_epoch_time();
  memcpy(agent->turn.request_transaction_id,
         ((StunHeader*)msg->buf)->transaction_id,
         sizeof(agent->turn.request_transaction_id));
}

static int agent_turn_create_permission(Agent* agent, Address* peer) {
  StunMessage* msg = &agent->recv_msg;
  int ret;

  stun_msg_create(msg, STUN_CLASS_REQUEST |
                           STUN_METHOD_CREATE_PERMISSION);
  if (stun_msg_write_xor_address(msg, STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                                 peer) < 0 ||
      agent_turn_add_auth(agent, msg) < 0) {
    return -1;
  }
  ret = agent_socket_send(agent, &agent->turn.server, msg->buf, msg->size);
  if (ret < 0) {
    return ret;
  }
  agent_turn_request_sent(agent, msg, STUN_METHOD_CREATE_PERMISSION);
  return 0;
}

static int agent_turn_channel_bind(Agent* agent, Address* peer) {
  StunMessage* msg = &agent->recv_msg;
  uint16_t channel_number = htons(AGENT_TURN_CHANNEL_NUMBER);
  uint32_t channel_attr = 0;
  int ret;

  memcpy(&channel_attr, &channel_number, sizeof(channel_number));
  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_CHANNEL_BIND);
  if (stun_msg_write_attr(msg, STUN_ATTR_TYPE_CHANNEL_NUMBER,
                          sizeof(channel_attr), (char*)&channel_attr) < 0 ||
      stun_msg_write_xor_address(msg, STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                                 peer) < 0 ||
      agent_turn_add_auth(agent, msg) < 0) {
    return -1;
  }
  ret = agent_socket_send(agent, &agent->turn.server, msg->buf, msg->size);
  if (ret >= 0) {
    agent_turn_request_sent(agent, msg, STUN_METHOD_CHANNEL_BIND);
  }
  return ret;
}

static int agent_turn_refresh(Agent* agent, uint32_t lifetime_seconds) {
  StunMessage* msg = &agent->recv_msg;
  uint32_t lifetime = htonl(lifetime_seconds);
  int ret;

  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_REFRESH);
  if (stun_msg_write_attr(msg, STUN_ATTR_TYPE_LIFETIME,
                          sizeof(lifetime), (char*)&lifetime) < 0 ||
      agent_turn_add_auth(agent, msg) < 0) {
    return -1;
  }
  ret = agent_socket_send(agent, &agent->turn.server,
                          msg->buf, msg->size);
  if (ret >= 0) {
    agent_turn_request_sent(agent, msg, STUN_METHOD_REFRESH);
  }
  return ret;
}

static int agent_turn_allocate(Agent* agent, int authenticated,
                               StunMessage* response) {
  uint32_t requested_transport = htonl(17U << 24);
  StunMessage* request = &agent->send_msg;
  int ret;

  stun_msg_create(request, STUN_CLASS_REQUEST | STUN_METHOD_ALLOCATE);
  if (stun_msg_write_attr(request, STUN_ATTR_TYPE_REQUESTED_TRANSPORT,
                          sizeof(requested_transport),
                          (char*)&requested_transport) < 0) {
    return -1;
  }
  if (authenticated && agent_turn_add_auth(agent, request) < 0) {
    return -1;
  }
  ret = agent_socket_send(agent, &agent->turn.server,
                          request->buf, request->size);
  if (ret < 0) {
    return ret;
  }
  return agent_recv_stun_response(agent, &agent->turn.server,
                                  request, response);
}

static int agent_create_turn_addr(Agent* agent, Address* serv_addr,
                                  const char* username,
                                  const char* credential) {
  StunMessage* response = &agent->recv_msg;
  IceCandidate* candidate;
  int ret;

  if (agent->turn.allocated) {
    LOGW("Only one UDP TURN allocation is supported");
    return -1;
  }
  if (username == NULL || credential == NULL || username[0] == '\0' ||
      credential[0] == '\0' ||
      strlen(username) >= sizeof(agent->send_msg.username)) {
    LOGE("TURN credentials are missing or username is too long");
    return -1;
  }
  agent->turn.server = *serv_addr;
  agent->turn.username = username;
  agent->turn.credential = credential;
  ret = agent_turn_allocate(agent, 0, response);
  if (ret <= 0) {
    LOGD("Failed to receive TURN Allocate Response.");
    return ret;
  }
  if (response->stunclass != STUN_CLASS_ERROR ||
      response->stunmethod != STUN_METHOD_ALLOCATE ||
      (response->error_code != 401 && response->error_code != 438) ||
      response->realm[0] == '\0' || response->nonce[0] == '\0') {
    LOGE("Invalid TURN Allocate challenge.");
    return -1;
  }
  snprintf(agent->turn.realm, sizeof(agent->turn.realm), "%s",
           response->realm);
  snprintf(agent->turn.nonce, sizeof(agent->turn.nonce), "%s",
           response->nonce);
  ret = agent_turn_allocate(agent, 1, response);
  if (ret <= 0) {
    LOGD("Failed to receive authenticated TURN Allocate Response.");
    return ret;
  }
  if (response->stunclass != STUN_CLASS_RESPONSE ||
      response->stunmethod != STUN_METHOD_ALLOCATE ||
      stun_msg_is_valid_long_term(response->buf, response->size,
                                  agent->turn.username,
                                  agent->turn.realm,
                                  agent->turn.credential) < 0 ||
      agent->local_candidates_count >= AGENT_MAX_CANDIDATES) {
    LOGE("Invalid TURN Allocate Response.");
    return -1;
  }
  candidate = &agent->local_candidates[agent->local_candidates_count];
  ice_candidate_create(candidate, agent->local_candidates_count,
                       ICE_CANDIDATE_TYPE_RELAY,
                       &response->relayed_addr, UINT16_MAX);
  candidate->raddr = response->mapped_addr;
  agent->local_candidates_count++;
  agent->turn.lifetime = response->lifetime != 0
                             ? response->lifetime
                             : AGENT_TURN_DEFAULT_LIFETIME;
  agent->turn.last_refresh_ms = ports_get_epoch_time();
  agent->turn.allocated = 1;
  LOGI("TURN allocation created, lifetime=%u seconds",
       (unsigned int)agent->turn.lifetime);
  return ret;
}

static int agent_turn_send_indication(Agent* agent, Address* peer,
                                      const uint8_t* buf, int len) {
  StunMessage* msg = &agent->recv_msg;
  int ret;

  if (len < 0 || len > CONFIG_MTU) {
    return -1;
  }
  stun_msg_create(msg, STUN_CLASS_INDICATION | STUN_METHOD_SEND);
  if (stun_msg_write_xor_address(msg,
                                 STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                                 peer) < 0 ||
      stun_msg_write_attr(msg, STUN_ATTR_TYPE_DATA, len,
                          (char*)buf) < 0) {
    return -1;
  }
  ret = agent_socket_send(agent, &agent->turn.server, msg->buf, msg->size);
  return ret < 0 ? ret : len;
}

static int agent_turn_send_channel_data(Agent* agent, const uint8_t* buf,
                                        int len) {
  uint8_t* packet = agent->recv_msg.buf;
  uint16_t value;
  int padded_len;
  int ret;

  if (len < 0 || len > CONFIG_MTU) {
    return -1;
  }
  value = htons(AGENT_TURN_CHANNEL_NUMBER);
  memcpy(packet, &value, sizeof(value));
  value = htons((uint16_t)len);
  memcpy(packet + 2, &value, sizeof(value));
  memcpy(packet + 4, buf, len);
  padded_len = ALIGN32(len);
  if (padded_len > len) {
    memset(packet + 4 + len, 0, padded_len - len);
  }
  ret = agent_socket_send(agent, &agent->turn.server, packet,
                          padded_len + 4);
  return ret < 0 ? ret : len;
}

static void agent_turn_maintain(Agent* agent, Address* peer) {
  uint32_t now = ports_get_epoch_time();
  uint32_t refresh_interval = (uint32_t)
      (((uint64_t)agent->turn.lifetime * 1000U * 4U) / 5U);

  if (refresh_interval == 0) {
    refresh_interval = AGENT_TURN_DEFAULT_LIFETIME * 800U;
  }
  if (!addr_equal(&agent->turn.peer, peer)) {
    agent->turn.peer = *peer;
    agent->turn.permission_active = 0;
    agent->turn.channel_bound = 0;
    agent->turn.last_permission_ms = 0;
    agent->turn.last_channel_bind_ms = 0;
    if (agent->turn.request_method == STUN_METHOD_CREATE_PERMISSION ||
        agent->turn.request_method == STUN_METHOD_CHANNEL_BIND) {
      agent->turn.request_method = 0;
    }
  }
  if (agent->turn.request_method != 0 &&
      now - agent->turn.request_sent_ms >= AGENT_TURN_REQUEST_TIMEOUT_MS) {
    agent->turn.request_method = 0;
  }
  if (agent->turn.request_method != 0) {
    return;
  }
  if (now - agent->turn.last_refresh_ms >= refresh_interval) {
    agent_turn_refresh(agent, agent->turn.lifetime);
    return;
  }
  if (!agent->turn.permission_active ||
      now - agent->turn.last_permission_ms >=
          AGENT_TURN_PERMISSION_REFRESH_MS) {
    agent_turn_create_permission(agent, peer);
    return;
  }
  if (agent->turn.channel_bound &&
      now - agent->turn.last_channel_bind_ms >=
          AGENT_TURN_CHANNEL_REFRESH_MS) {
    agent_turn_channel_bind(agent, peer);
  }
}

static int agent_turn_send(Agent* agent, Address* peer,
                           const uint8_t* buf, int len) {
  int selected_peer;

  if (!agent->turn.allocated) {
    return -1;
  }
  agent_turn_maintain(agent, peer);
  if (!agent->turn.permission_active) {
    return -1;
  }
  selected_peer = agent->selected_pair != NULL &&
                  agent->selected_pair->local->type ==
                      ICE_CANDIDATE_TYPE_RELAY &&
                  addr_equal(&agent->selected_pair->remote->addr, peer);
  if (selected_peer && !agent->turn.channel_bound &&
      agent->turn.request_method == 0) {
    agent_turn_channel_bind(agent, peer);
  }
  if (agent->turn.channel_bound) {
    return agent_turn_send_channel_data(agent, buf, len);
  }
  return agent_turn_send_indication(agent, peer, buf, len);
}

void agent_gather_candidate(Agent* agent, const char* urls, const char* username, const char* credential) {
  char* pos;
  int port;
  char hostname[64];
  char addr_string[ADDRSTRLEN];
  int i;
  int addr_type[1] = {AF_INET};  // ipv6 no need stun
  Address resolved_addr;
  memset(hostname, 0, sizeof(hostname));

  if (urls == NULL) {
    agent_create_host_addr(agent);
    agent_create_nat_pmp_addr(agent);
    return;
  }

  if ((pos = strstr(urls + 5, ":")) == NULL) {
    LOGE("Invalid URL");
    return;
  }

  port = atoi(pos + 1);
  if (port <= 0) {
    LOGE("Cannot parse port");
    return;
  }

  snprintf(hostname, pos - urls - 5 + 1, "%s", urls + 5);

  for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {
    if (ports_resolve_addr(hostname, &resolved_addr) == 0) {
      addr_set_port(&resolved_addr, port);
      addr_to_string(&resolved_addr, addr_string, sizeof(addr_string));
      LOGI("Resolved stun/turn server %s:%d", addr_string, port);

      if (strncmp(urls, "stun:", 5) == 0) {
        LOGD("Create stun addr");
        agent_create_stun_addr(agent, &resolved_addr);
      } else if (strncmp(urls, "turn:", 5) == 0) {
        if (agent->turn.allocated) {
          LOGW("Only one UDP TURN server is supported; ignoring %s", urls);
        } else {
          LOGD("Create turn addr");
          agent_create_turn_addr(agent, &resolved_addr, username, credential);
        }
      }
    }
  }
}

void agent_create_ice_credential(Agent* agent) {
  memset(agent->local_ufrag, 0, sizeof(agent->local_ufrag));
  memset(agent->local_upwd, 0, sizeof(agent->local_upwd));

  utils_random_string(agent->local_ufrag, 4);
  utils_random_string(agent->local_upwd, 24);
}

void agent_get_local_description(Agent* agent, char* description, int length) {
  for (int i = 0; i < agent->local_candidates_count; i++) {
    ice_candidate_to_description(&agent->local_candidates[i], description + strlen(description), length - strlen(description));
  }

  // remove last \n
  description[strlen(description)] = '\0';
  LOGD("local description:\n%s", description);
}

int agent_send(Agent* agent, const uint8_t* buf, int len) {
  IceCandidatePair* pair = agent->selected_pair != NULL
                               ? agent->selected_pair
                               : agent->nominated_pair;
  int ret;

  if (pair == NULL) {
    return -1;
  }
  if (pair->local->type == ICE_CANDIDATE_TYPE_RELAY) {
    EDGERTC_STATS_BEGIN(start);
    ret = agent_turn_send(agent, &pair->remote->addr, buf, len);
    EDGERTC_STATS_END(EDGERTC_STAT_AGENT_SEND_TURN, start);
    return ret;
  }
  EDGERTC_STATS_BEGIN(start);
  ret = agent_socket_send(agent, &pair->remote->addr, buf, len);
  EDGERTC_STATS_END(EDGERTC_STAT_AGENT_SEND_SOCKET, start);
  return ret;
}

static void agent_create_binding_response(Agent* agent, StunMessage* msg, Address* addr) {
  int size = 0;
  char username[584];
  char mapped_address[32];
  uint8_t mask[16];
  StunHeader* header;
  stun_msg_create(msg, STUN_CLASS_RESPONSE | STUN_METHOD_BINDING);
  header = (StunHeader*)msg->buf;
  memcpy(header->transaction_id, agent->transaction_id, sizeof(header->transaction_id));
  snprintf(username, sizeof(username), "%s:%s", agent->local_ufrag, agent->remote_ufrag);
  *((uint32_t*)mask) = htonl(MAGIC_COOKIE);
  memcpy(mask + 4, agent->transaction_id, sizeof(agent->transaction_id));
  size = stun_set_mapped_address(mapped_address, mask, addr);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS, size, mapped_address);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_USERNAME, strlen(username), username);
  stun_msg_finish(msg, STUN_CREDENTIAL_SHORT_TERM, agent->local_upwd, strlen(agent->local_upwd));
}

static void agent_create_binding_request(Agent* agent, StunMessage* msg, int consent) {
  uint64_t tie_breaker = 0;  // always be controlled
  IceCandidatePair* pair = consent ? agent->selected_pair : agent->nominated_pair;
  uint32_t priority = htonl(pair->local->priority);
  StunHeader* header;
  // send binding request
  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);
  header = (StunHeader*)msg->buf;
  if (!consent && agent->nominated_pair->conncheck == 0) {
    memcpy(agent->conncheck_transaction_id, header->transaction_id,
           sizeof(agent->conncheck_transaction_id));
  } else if (!consent) {
    memcpy(header->transaction_id, agent->conncheck_transaction_id,
           sizeof(header->transaction_id));
  }
  char username[584];
  memset(username, 0, sizeof(username));
  snprintf(username, sizeof(username), "%s:%s", agent->remote_ufrag, agent->local_ufrag);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_USERNAME, strlen(username), username);
  stun_msg_write_attr(msg, STUN_ATTR_TYPE_PRIORITY, sizeof(priority), (char*)&priority);
  if (agent->mode == AGENT_MODE_CONTROLLING) {
    if (!consent) {
      stun_msg_write_attr(msg, STUN_ATTR_TYPE_USE_CANDIDATE, 0, NULL);
    }
    stun_msg_write_attr(msg, STUN_ATTR_TYPE_ICE_CONTROLLING, 8, (char*)&tie_breaker);
  } else {
    stun_msg_write_attr(msg, STUN_ATTR_TYPE_ICE_CONTROLLED, 8, (char*)&tie_breaker);
  }
  stun_msg_finish(msg, STUN_CREDENTIAL_SHORT_TERM, agent->remote_upwd, strlen(agent->remote_upwd));
}

void agent_process_stun_request(Agent* agent, StunMessage* stun_msg,
                                Address* addr, int via_turn) {
  char addr_string[ADDRSTRLEN];
  size_t local_ufrag_len = strlen(agent->local_ufrag);
  size_t remote_ufrag_len = strlen(agent->remote_ufrag);
  StunMessage* msg = &agent->send_msg;
  StunHeader* header;
  IceCandidatePair* pair = NULL;
  int migrate_selected_pair;
  int send_ret;
  switch (stun_msg->stunmethod) {
    case STUN_METHOD_BINDING:
      addr_to_string(addr, addr_string, sizeof(addr_string));
      if (stun_msg_is_valid(stun_msg->buf, stun_msg->size, agent->local_upwd) == 0) {
        if (strlen(stun_msg->username) != local_ufrag_len + remote_ufrag_len + 1 ||
            strncmp(stun_msg->username, agent->local_ufrag, local_ufrag_len) != 0 ||
            stun_msg->username[local_ufrag_len] != ':' ||
            strcmp(stun_msg->username + local_ufrag_len + 1, agent->remote_ufrag) != 0) {
          LOGW("ICE rejected Binding Request from %s:%d: username mismatch",
               addr_string, addr->port);
          break;
        }
        LOGD("ICE accepted Binding Request from %s:%d",
             addr_string, addr->port);
        migrate_selected_pair =
            agent->selected_pair != NULL &&
            agent->mode == AGENT_MODE_CONTROLLED &&
            stun_msg->use_candidate &&
            !addr_equal(addr, &agent->selected_pair->remote->addr);
        if (agent->selected_pair == NULL || migrate_selected_pair) {
          pair = agent_queue_triggered_check(agent, stun_msg, addr, via_turn);
          if (pair != NULL && agent->mode == AGENT_MODE_CONTROLLED &&
              stun_msg->use_candidate) {
            pair->nominated = 1;
          }
        }
        header = (StunHeader*)stun_msg->buf;
        memcpy(agent->transaction_id, header->transaction_id, sizeof(header->transaction_id));
        agent_create_binding_response(agent, msg, addr);
        send_ret = via_turn
                       ? agent_turn_send(agent, addr, msg->buf, msg->size)
                       : agent_socket_send(agent, addr, msg->buf, msg->size);
        if (send_ret < 0) {
          LOGW("ICE failed to send Binding Response to %s:%d", addr_string, addr->port);
        } else if (migrate_selected_pair && pair != NULL) {
          pair->state = ICE_CANDIDATE_STATE_SUCCEEDED;
          agent->nominated_pair = pair;
          agent_select_succeeded_pair(agent, ports_get_epoch_time());
        }
        if (agent->selected_pair != NULL &&
            addr_equal(addr, &agent->selected_pair->remote->addr)) {
          agent->consent_last_received_ms = ports_get_epoch_time();
        }
      } else {
        LOGW("ICE rejected Binding Request from %s:%d: authentication failed", addr_string, addr->port);
      }
      break;
    default:
      break;
  }
}

void agent_process_stun_response(Agent* agent, StunMessage* stun_msg, Address* addr) {
  char expected_addr[ADDRSTRLEN];
  char received_addr[ADDRSTRLEN];
  StunHeader* header = (StunHeader*)stun_msg->buf;
  switch (stun_msg->stunmethod) {
    case STUN_METHOD_BINDING:
      addr_to_string(addr, received_addr, sizeof(received_addr));
      if (stun_msg_is_valid(stun_msg->buf, stun_msg->size, agent->remote_upwd) != 0) {
        LOGW("ICE rejected Binding Response from %s:%d: authentication failed", received_addr, addr->port);
        break;
      }
      if (agent->selected_pair != NULL &&
          addr_equal(addr, &agent->selected_pair->remote->addr) &&
          agent_take_consent_transaction(agent, header->transaction_id)) {
        agent->consent_last_received_ms = ports_get_epoch_time();
        agent->consent_responses_received++;
        LOGD("ICE received consent response from %s:%d", received_addr, addr->port);
        break;
      }
      if (agent->nominated_pair == NULL ||
          agent->nominated_pair->state != ICE_CANDIDATE_STATE_INPROGRESS) {
        LOGD("ICE ignored Binding Response from %s:%d: no check in progress",
             received_addr, addr->port);
        break;
      }
      if (!addr_equal(addr, &agent->nominated_pair->remote->addr)) {
        addr_to_string(&agent->nominated_pair->remote->addr, expected_addr, sizeof(expected_addr));
        LOGW("ICE Binding Response source mismatch: received=%s:%d, expected=%s:%d",
             received_addr, addr->port,
             expected_addr, agent->nominated_pair->remote->addr.port);
        break;
      }
      if (memcmp(header->transaction_id, agent->conncheck_transaction_id,
                 sizeof(header->transaction_id)) != 0) {
        LOGD("ICE ignored Binding Response from %s:%d: transaction mismatch",
             received_addr, addr->port);
        break;
      }
      LOGI("ICE received Binding Response from %s:%d", received_addr, addr->port);
      agent->nominated_pair->state = ICE_CANDIDATE_STATE_SUCCEEDED;
      break;
    default:
      break;
  }
}

static void agent_process_turn_response(Agent* agent, StunMessage* msg) {
  StunHeader* header = (StunHeader*)msg->buf;
  uint32_t now = ports_get_epoch_time();

  if (msg->stunmethod != agent->turn.request_method ||
      memcmp(header->transaction_id, agent->turn.request_transaction_id,
             sizeof(header->transaction_id)) != 0) {
    return;
  }
  agent->turn.request_method = 0;
  if (msg->stunclass == STUN_CLASS_RESPONSE) {
    switch (msg->stunmethod) {
      case STUN_METHOD_REFRESH:
        if (msg->lifetime != 0) {
          agent->turn.lifetime = msg->lifetime;
        }
        agent->turn.last_refresh_ms = now;
        break;
      case STUN_METHOD_CREATE_PERMISSION:
        agent->turn.permission_active = 1;
        agent->turn.last_permission_ms = now;
        break;
      case STUN_METHOD_CHANNEL_BIND:
        agent->turn.channel_bound = 1;
        agent->turn.last_channel_bind_ms = now;
        break;
      default:
        break;
    }
    return;
  }
  if (msg->stunclass != STUN_CLASS_ERROR) {
    return;
  }
  if (msg->error_code == 438 && msg->nonce[0] != '\0') {
    snprintf(agent->turn.nonce, sizeof(agent->turn.nonce), "%s",
             msg->nonce);
    if (msg->realm[0] != '\0') {
      snprintf(agent->turn.realm, sizeof(agent->turn.realm), "%s",
               msg->realm);
    }
  }
  switch (msg->stunmethod) {
    case STUN_METHOD_REFRESH:
      agent->turn.last_refresh_ms = 0;
      break;
    case STUN_METHOD_CREATE_PERMISSION:
      agent->turn.permission_active = 0;
      agent->turn.channel_bound = 0;
      break;
    case STUN_METHOD_CHANNEL_BIND:
      agent->turn.channel_bound = 0;
      agent->turn.last_channel_bind_ms = 0;
      break;
    default:
      break;
  }
  LOGW("TURN request failed: method=0x%04x, error=%d",
       (unsigned int)msg->stunmethod, msg->error_code);
}

static int agent_unwrap_turn_data(Agent* agent, Address* source,
                                  StunMessage* msg, int len, Address* peer,
                                  int* via_turn) {
  uint8_t* buf = msg->buf;
  uint16_t channel_number;
  uint16_t data_length;

  if (!agent->turn.allocated ||
      !addr_equal(source, &agent->turn.server)) {
    return len;
  }
  if (len >= 4 && (buf[0] & 0xc0) == 0x40) {
    memcpy(&channel_number, buf, sizeof(channel_number));
    memcpy(&data_length, buf + 2, sizeof(data_length));
    channel_number = ntohs(channel_number);
    data_length = ntohs(data_length);
    if (!agent->turn.channel_bound ||
        channel_number != AGENT_TURN_CHANNEL_NUMBER ||
        data_length > len - 4) {
      return 0;
    }
    memmove(buf, buf + 4, data_length);
    *peer = agent->turn.peer;
    *via_turn = 1;
    return data_length;
  }
  if (stun_probe(buf, len) != 0 || len > (int)sizeof(msg->buf)) {
    return 0;
  }
  msg->size = len;
  if (stun_parse_msg_buf(msg) < 0) {
    return 0;
  }
  if (msg->stunclass == STUN_CLASS_INDICATION &&
      msg->stunmethod == STUN_METHOD_DATA && msg->data != NULL &&
      msg->data_size <= (size_t)len) {
    memmove(buf, msg->data, msg->data_size);
    *peer = msg->peer_addr;
    *via_turn = 1;
    return msg->data_size;
  }
  if (msg->stunclass != STUN_CLASS_RESPONSE &&
      msg->stunclass != STUN_CLASS_ERROR) {
    return 0;
  }
  if (stun_msg_is_valid_long_term(msg->buf, msg->size,
                                  agent->turn.username,
                                  agent->turn.realm,
                                  agent->turn.credential) < 0) {
    LOGW("Discarding unauthenticated TURN response");
    return 0;
  }
  agent_process_turn_response(agent, msg);
  return 0;
}

int agent_recv(Agent* agent, uint8_t* buf, int len, int* packet_consumed) {
  int ret = -1;
  int recv_len;
  int via_turn = 0;
  StunMessage* stun_msg = &agent->recv_msg;
  uint8_t* recv_buf;
  Address addr;
  char addr_string[ADDRSTRLEN];

  if (buf == NULL || len <= 0) {
    return -1;
  }
  if (packet_consumed != NULL) {
    *packet_consumed = 0;
  }
  recv_buf = agent->turn.allocated ? stun_msg->buf : buf;
  recv_len = agent->turn.allocated ? (int)sizeof(stun_msg->buf) : len;
  ret = agent_socket_recv(agent, &addr, recv_buf, recv_len);
  if (ret > 0) {
    LOGD("ICE received UDP packet from %s:%d, len=%d",
         addr_to_string(&addr, addr_string, sizeof(addr_string)) ?
             addr_string : "?",
         addr.port, ret);
    if (packet_consumed != NULL) {
      *packet_consumed = 1;
    }
    if (agent->turn.allocated) {
      ret = agent_unwrap_turn_data(agent, &addr, stun_msg, ret, &addr,
                                   &via_turn);
      if (ret > len) {
        LOGW("Discarding received packet larger than caller buffer: %d > %d",
             ret, len);
        return 0;
      }
      if (ret > 0) {
        memcpy(buf, stun_msg->buf, ret);
      }
    }
  }
  if (ret > 0 && stun_probe(buf, ret) == 0) {
    addr_to_string(&addr, addr_string, sizeof(addr_string));
    if (buf != stun_msg->buf) {
      memcpy(stun_msg->buf, buf, ret);
    }
    stun_msg->size = ret;
    if (stun_parse_msg_buf(stun_msg) < 0) {
      LOGW("ICE discarded malformed STUN packet from %s:%d, len=%d",
           addr_string, addr.port, ret);
      return 0;
    }
    LOGD("ICE received STUN packet from %s:%d, class=0x%04x, method=0x%04x",
         addr_string, addr.port, (unsigned int)stun_msg->stunclass,
         (unsigned int)stun_msg->stunmethod);
    switch (stun_msg->stunclass) {
      case STUN_CLASS_REQUEST:
        agent_process_stun_request(agent, stun_msg, &addr, via_turn);
        break;
      case STUN_CLASS_RESPONSE:
        agent_process_stun_response(agent, stun_msg, &addr);
        break;
      case STUN_CLASS_ERROR: {
        LOGW("ICE received STUN error response from %s:%d, method=0x%04x, error=%d",
             addr_string, addr.port, (unsigned int)stun_msg->stunmethod,
             stun_msg->error_code);
        if (agent->nominated_pair != NULL &&
            agent->nominated_pair->state == ICE_CANDIDATE_STATE_INPROGRESS &&
            addr_equal(&addr, &agent->nominated_pair->remote->addr) &&
            memcmp(((StunHeader*)stun_msg->buf)->transaction_id,
                   agent->conncheck_transaction_id,
                   sizeof(agent->conncheck_transaction_id)) == 0) {
          agent->nominated_pair->state = ICE_CANDIDATE_STATE_FAILED;
        }
      } break;
      default:
        break;
    }
    ret = 0;
  }
  return ret;
}

int agent_add_remote_candidate(Agent* agent, char* description, char* end) {
  IceCandidate candidate = {0};
  int i;

  if (ice_candidate_from_description(&candidate, description, end) != 0) {
    return -1;
  }

  for (i = 0; i < agent->remote_candidates_count; i++) {
    if (agent_candidate_identity_equal(&agent->remote_candidates[i],
                                       &candidate)) {
      return 0;
    }
  }

  if (agent->remote_candidates_count >= AGENT_MAX_CANDIDATES) {
    LOGW("ICE remote candidate limit reached");
    return -1;
  }

  agent->remote_candidates[agent->remote_candidates_count++] = candidate;
  return 0;
}

void agent_set_remote_description(Agent* agent, char* description) {
  /*
  a=ice-ufrag:Iexb
  a=ice-pwd:IexbSoY7JulyMbjKwISsG9
  a=candidate:1 1 UDP 1 36.231.28.50 38143 typ srflx
  */
  LOGD("Set remote description:\n%s", description);

  char* line_start = description;
  char* line_end = NULL;

  while ((line_end = strstr(line_start, "\r\n")) != NULL) {
    if (strncmp(line_start, "a=ice-ufrag:", strlen("a=ice-ufrag:")) == 0) {
      strncpy(agent->remote_ufrag, line_start + strlen("a=ice-ufrag:"), line_end - line_start - strlen("a=ice-ufrag:"));

    } else if (strncmp(line_start, "a=ice-pwd:", strlen("a=ice-pwd:")) == 0) {
      strncpy(agent->remote_upwd, line_start + strlen("a=ice-pwd:"), line_end - line_start - strlen("a=ice-pwd:"));

    } else if (strncmp(line_start, "a=candidate:", strlen("a=candidate:")) == 0) {
      agent_add_remote_candidate(agent, line_start, line_end);
    }

    line_start = line_end + 2;
  }

  LOGD("remote ufrag: %s", agent->remote_ufrag);
  LOGD("remote upwd: %s", agent->remote_upwd);
}

void agent_update_candidate_pairs(Agent* agent) {
  int i, j, k;

  for (i = 0; i < agent->local_candidates_count; i++) {
    for (j = 0; j < agent->remote_candidates_count; j++) {
      if (agent->local_candidates[i].addr.family == agent->remote_candidates[j].addr.family) {
        IceCandidatePair* pair = agent_find_candidate_pair(
            agent, &agent->local_candidates[i], &agent->remote_candidates[j]);
        uint64_t priority;

        if (pair != NULL) {
          continue;
        }
        priority = agent_candidate_pair_priority(
            agent, &agent->local_candidates[i], &agent->remote_candidates[j]);
        pair = agent_find_equivalent_candidate_pair(
            agent, &agent->local_candidates[i], &agent->remote_candidates[j]);
        if (pair != NULL) {
          if (priority > pair->priority) {
            pair->local = &agent->local_candidates[i];
            pair->remote = &agent->remote_candidates[j];
            pair->priority = priority;
          }
          continue;
        }
        if (agent->candidate_pairs_num >= AGENT_MAX_CANDIDATE_PAIRS) {
          LOGW("ICE candidate pair limit reached");
          return;
        }
        pair = &agent->candidate_pairs[agent->candidate_pairs_num];

        pair->local = &agent->local_candidates[i];
        pair->remote = &agent->remote_candidates[j];
        pair->priority = priority;
        pair->state = ICE_CANDIDATE_STATE_FROZEN;
        pair->nominated = 0;
        agent->candidate_pairs_num++;
      }
    }
  }

  for (i = 1; i < agent->candidate_pairs_num; i++) {
    IceCandidatePair pair = agent->candidate_pairs[i];
    for (k = i; k > 0 && agent->candidate_pairs[k - 1].priority < pair.priority; k--) {
      agent->candidate_pairs[k] = agent->candidate_pairs[k - 1];
    }
    agent->candidate_pairs[k] = pair;
  }
  LOGD("candidate pairs num: %d", agent->candidate_pairs_num);
}

int agent_connectivity_check(Agent* agent) {
  char addr_string[ADDRSTRLEN];
  StunMessage* msg = &agent->send_msg;
  uint32_t now;

  if (agent->nominated_pair == NULL ||
      (agent->nominated_pair->state != ICE_CANDIDATE_STATE_INPROGRESS &&
       agent->nominated_pair->state != ICE_CANDIDATE_STATE_SUCCEEDED)) {
    LOGI("nominated pair is not in progress");
    return -1;
  }

  now = ports_get_epoch_time();

  if (agent_select_succeeded_pair(agent, now) == 0) {
    return 0;
  }

  if (agent->nominated_pair->state == ICE_CANDIDATE_STATE_INPROGRESS &&
      (agent->nominated_pair->conncheck == 0 ||
       now - agent->conncheck_last_send_ms >= AGENT_CONNCHECK_INTERVAL_MS)) {
    addr_to_string(&agent->nominated_pair->remote->addr, addr_string, sizeof(addr_string));
    LOGD("send binding request to remote ip: %s, port: %d", addr_string, agent->nominated_pair->remote->addr.port);
    agent_create_binding_request(agent, msg, 0);
    if ((agent->nominated_pair->local->type == ICE_CANDIDATE_TYPE_RELAY
             ? agent_turn_send(agent, &agent->nominated_pair->remote->addr,
                               msg->buf, msg->size)
             : agent_socket_send(agent,
                                 &agent->nominated_pair->remote->addr,
                                 msg->buf, msg->size)) < 0) {
      LOGW("ICE failed to send Binding Request to %s:%d",
           addr_string, agent->nominated_pair->remote->addr.port);
    }
    agent->conncheck_last_send_ms = now;
    agent->nominated_pair->conncheck++;
  }

  agent_recv(agent, msg->buf, sizeof(msg->buf), NULL);

  return agent_select_succeeded_pair(agent, now);
}

int agent_consent_check(Agent* agent) {
  char addr_string[ADDRSTRLEN];
  StunMessage* msg = &agent->send_msg;
  uint32_t now = ports_get_epoch_time();

  if (agent->selected_pair == NULL) {
    return -1;
  }

  if (now - agent->consent_last_send_ms >= CONFIG_KEEPALIVE_INTERVAL) {
    agent_create_binding_request(agent, msg, 1);
    if (agent_send(agent, msg->buf, msg->size) < 0) {
      addr_to_string(&agent->selected_pair->remote->addr,
                     addr_string, sizeof(addr_string));
      LOGW("ICE failed to send consent request to %s:%d",
           addr_string, agent->selected_pair->remote->addr.port);
    } else {
      agent_store_consent_transaction(
          agent, ((StunHeader*)msg->buf)->transaction_id);
      agent->consent_requests_sent++;
    }
    agent->consent_last_send_ms = now;
  }

  if (now - agent->consent_last_received_ms > CONFIG_KEEPALIVE_TIMEOUT) {
    LOGW("ICE consent timeout: idle=%u ms, requests=%u, responses=%u",
         (unsigned int)(now - agent->consent_last_received_ms),
         (unsigned int)agent->consent_requests_sent,
         (unsigned int)agent->consent_responses_received);
    return -1;
  }
  return 0;
}

int agent_select_candidate_pair(Agent* agent) {
  int i;
  uint32_t now = ports_get_epoch_time();
  IceCandidatePair* waiting_pair = NULL;
  IceCandidatePair* succeeded_pair = NULL;

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state != ICE_CANDIDATE_STATE_SUCCEEDED) {
      continue;
    }
    if (agent_pair_is_selectable(agent, &agent->candidate_pairs[i])) {
      agent->nominated_pair = &agent->candidate_pairs[i];
      return 0;
    }
    if (succeeded_pair == NULL ||
        agent->candidate_pairs[i].priority > succeeded_pair->priority) {
      succeeded_pair = &agent->candidate_pairs[i];
    }
  }

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_INPROGRESS) {
      agent->nominated_pair = &agent->candidate_pairs[i];
      if (now - agent->conncheck_start_ms < AGENT_CONNCHECK_TIMEOUT_MS) {
        return 0;
      }
      LOGW("ICE candidate pair timed out after %u ms (%d requests sent)",
           (unsigned int)(now - agent->conncheck_start_ms),
           agent->candidate_pairs[i].conncheck);
      agent->candidate_pairs[i].state = ICE_CANDIDATE_STATE_FAILED;
    }
  }

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_WAITING &&
        (waiting_pair == NULL || agent->candidate_pairs[i].priority > waiting_pair->priority)) {
      waiting_pair = &agent->candidate_pairs[i];
    }
  }
  if (waiting_pair != NULL) {
    agent_start_candidate_pair(agent, waiting_pair, now);
    return 0;
  }

  for (i = 0; i < agent->candidate_pairs_num; i++) {
    if (agent->candidate_pairs[i].state == ICE_CANDIDATE_STATE_FROZEN) {
      agent_start_candidate_pair(agent, &agent->candidate_pairs[i], now);
      return 0;
    }
  }

  if (succeeded_pair != NULL) {
    agent->nominated_pair = succeeded_pair;
    return 0;
  }

  // all candidate pairs are failed
  return -1;
}
