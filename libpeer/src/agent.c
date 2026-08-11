#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "agent.h"
#include "base64.h"
#include "ice.h"
#include "ports.h"
#include "socket.h"
#include "stun.h"
#include "utils.h"

#define AGENT_POLL_TIMEOUT 1
#define AGENT_CONNCHECK_TIMEOUT_MS 1000
#define AGENT_CONNCHECK_INTERVAL_MS 100
#define AGENT_STUN_RECV_MAXTIMES 1000

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

static IceCandidate* agent_find_local_host_candidate(Agent* agent, Address* addr) {
  int i;

  for (i = 0; i < agent->local_candidates_count; i++) {
    if (agent->local_candidates[i].type == ICE_CANDIDATE_TYPE_HOST &&
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

static IceCandidatePair* agent_queue_triggered_check(Agent* agent, StunMessage* stun_msg, Address* addr) {
  char addr_string[ADDRSTRLEN];
  IceCandidate* local = agent_find_local_host_candidate(agent, addr);
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
    ice_candidate_create(remote, agent->remote_candidates_count, ICE_CANDIDATE_TYPE_PRFLX, addr);
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
  memset(agent->remote_ufrag, 0, sizeof(agent->remote_ufrag));
  memset(agent->remote_upwd, 0, sizeof(agent->remote_upwd));
  agent->local_candidates_count = 0;
  agent->remote_candidates_count = 0;
  agent->candidate_pairs_num = 0;
  agent->selected_pair = NULL;
  agent->nominated_pair = NULL;
  agent->consent_last_received_ms = 0;
  agent->consent_last_send_ms = 0;
  memset(agent->consent_transaction_id, 0, sizeof(agent->consent_transaction_id));
}

int agent_create(Agent* agent) {
  int ret;
  if ((ret = udp_socket_open(&agent->udp_sockets[0], AF_INET, 0)) < 0) {
    LOGE("Failed to create UDP socket.");
    return ret;
  }
  LOGI("create IPv4 UDP socket: %d", agent->udp_sockets[0].fd);

#if CONFIG_IPV6
  if ((ret = udp_socket_open(&agent->udp_sockets[1], AF_INET6, 0)) < 0) {
    LOGE("Failed to create IPv6 UDP socket.");
    return ret;
  }
  LOGI("create IPv6 UDP socket: %d", agent->udp_sockets[1].fd);
#endif

  agent_clear_candidates(agent);
  return 0;
}

void agent_destroy(Agent* agent) {
  if (agent->udp_sockets[0].fd > 0) {
    udp_socket_close(&agent->udp_sockets[0]);
  }

#if CONFIG_IPV6
  if (agent->udp_sockets[1].fd > 0) {
    udp_socket_close(&agent->udp_sockets[1]);
  }
#endif
}

static int agent_socket_recv(Agent* agent, Address* addr, uint8_t* buf, int len) {
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
    for (i = 0; i < 2; i++) {
      if (FD_ISSET(agent->udp_sockets[i].fd, &rfds)) {
        memset(buf, 0, len);
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
                                    StunMessage* request, StunMessage* response) {
  int i;
  int ret;
  Address source_addr;
  StunHeader* request_header = (StunHeader*)request->buf;

  for (i = 0; i < AGENT_STUN_RECV_MAXTIMES; i++) {
    ret = agent_socket_recv(agent, &source_addr, response->buf, sizeof(response->buf));
    if (ret <= 0) {
      continue;
    }
    if (!addr_equal(&source_addr, serv_addr) || stun_probe(response->buf, ret) != 0) {
      continue;
    }

    StunHeader* response_header = (StunHeader*)response->buf;
    if (memcmp(response_header->transaction_id, request_header->transaction_id,
               sizeof(request_header->transaction_id)) != 0) {
      continue;
    }

    response->size = ret;
    stun_parse_msg_buf(response);
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
      ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_HOST,
                           &agent->udp_sockets[i].bind_addr);
      // if resolve host addr, add to local candidate
      if (ports_get_host_addr(&ice_candidate->addr, iface_prefx[j])) {
        agent->local_candidates_count++;
      }
    }
  }

  return 0;
}

static int agent_create_stun_addr(Agent* agent, Address* serv_addr) {
  int ret = -1;
  Address bind_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
  memset(&send_msg, 0, sizeof(send_msg));
  memset(&recv_msg, 0, sizeof(recv_msg));

  stun_msg_create(&send_msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);

  if (ret == -1) {
    LOGE("Failed to send STUN Binding Request.");
    return ret;
  }

  ret = agent_recv_stun_response(agent, serv_addr, &send_msg, &recv_msg);
  if (ret <= 0) {
    LOGD("Failed to receive STUN Binding Response.");
    return ret;
  }

  if (recv_msg.stunclass != STUN_CLASS_RESPONSE || recv_msg.stunmethod != STUN_METHOD_BINDING) {
    LOGE("Invalid STUN Binding Response.");
    return -1;
  }
  memcpy(&bind_addr, &recv_msg.mapped_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_SRFLX, &bind_addr);
  return ret;
}

static int agent_create_turn_addr(Agent* agent, Address* serv_addr, const char* username, const char* credential) {
  int ret = -1;
  uint32_t attr = ntohl(0x11000000);
  Address turn_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
  memset(&recv_msg, 0, sizeof(recv_msg));
  memset(&send_msg, 0, sizeof(send_msg));
  stun_msg_create(&send_msg, STUN_METHOD_ALLOCATE);
  stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REQUESTED_TRANSPORT, sizeof(attr), (char*)&attr);  // UDP
  stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_USERNAME, strlen(username), (char*)username);

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);
  if (ret == -1) {
    LOGE("Failed to send TURN Binding Request.");
    return -1;
  }

  ret = agent_recv_stun_response(agent, serv_addr, &send_msg, &recv_msg);
  if (ret <= 0) {
    LOGD("Failed to receive TURN Allocate Response.");
    return ret;
  }

  if (recv_msg.stunclass == STUN_CLASS_ERROR && recv_msg.stunmethod == STUN_METHOD_ALLOCATE) {
    memset(&send_msg, 0, sizeof(send_msg));
    stun_msg_create(&send_msg, STUN_METHOD_ALLOCATE);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REQUESTED_TRANSPORT, sizeof(attr), (char*)&attr);  // UDP
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_USERNAME, strlen(username), (char*)username);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_NONCE, strlen(recv_msg.nonce), recv_msg.nonce);
    stun_msg_write_attr(&send_msg, STUN_ATTR_TYPE_REALM, strlen(recv_msg.realm), recv_msg.realm);
    stun_msg_finish(&send_msg, STUN_CREDENTIAL_LONG_TERM, credential, strlen(credential));
  } else {
    LOGE("Invalid TURN Binding Response.");
    return -1;
  }

  ret = agent_socket_send(agent, serv_addr, send_msg.buf, send_msg.size);
  if (ret < 0) {
    LOGE("Failed to send TURN Binding Request.");
    return -1;
  }

  memset(&recv_msg, 0, sizeof(recv_msg));
  ret = agent_recv_stun_response(agent, serv_addr, &send_msg, &recv_msg);
  if (ret <= 0) {
    LOGD("Failed to receive TURN Allocate Response.");
    return ret;
  }

  if (recv_msg.stunclass != STUN_CLASS_RESPONSE || recv_msg.stunmethod != STUN_METHOD_ALLOCATE) {
    LOGE("Invalid TURN Allocate Response.");
    return -1;
  }
  memcpy(&turn_addr, &recv_msg.relayed_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_RELAY, &turn_addr);
  return ret;
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
        LOGD("Create turn addr");
        agent_create_turn_addr(agent, &resolved_addr, username, credential);
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
  return agent_socket_send(agent, &agent->nominated_pair->remote->addr, buf, len);
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
  if (consent) {
    memcpy(agent->consent_transaction_id, header->transaction_id,
           sizeof(agent->consent_transaction_id));
  } else if (agent->nominated_pair->conncheck == 0) {
    memcpy(agent->conncheck_transaction_id, header->transaction_id,
           sizeof(agent->conncheck_transaction_id));
  } else {
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

void agent_process_stun_request(Agent* agent, StunMessage* stun_msg, Address* addr) {
  char addr_string[ADDRSTRLEN];
  size_t local_ufrag_len = strlen(agent->local_ufrag);
  size_t remote_ufrag_len = strlen(agent->remote_ufrag);
  StunMessage msg;
  StunHeader* header;
  IceCandidatePair* pair;
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
        header = (StunHeader*)stun_msg->buf;
        memcpy(agent->transaction_id, header->transaction_id, sizeof(header->transaction_id));
        agent_create_binding_response(agent, &msg, addr);
        if (agent_socket_send(agent, addr, msg.buf, msg.size) < 0) {
          LOGW("ICE failed to send Binding Response to %s:%d", addr_string, addr->port);
        }
        if (agent->selected_pair != NULL &&
            addr_equal(addr, &agent->selected_pair->remote->addr)) {
          agent->consent_last_received_ms = ports_get_epoch_time();
        }
        if (agent->selected_pair == NULL) {
          pair = agent_queue_triggered_check(agent, stun_msg, addr);
          if (pair != NULL && agent->mode == AGENT_MODE_CONTROLLED &&
              stun_msg->use_candidate) {
            pair->nominated = 1;
          }
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
          memcmp(header->transaction_id, agent->consent_transaction_id,
                 sizeof(header->transaction_id)) == 0) {
        agent->consent_last_received_ms = ports_get_epoch_time();
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

int agent_recv(Agent* agent, uint8_t* buf, int len, int* packet_consumed) {
  int ret = -1;
  StunMessage stun_msg;
  Address addr;
  if (packet_consumed != NULL) {
    *packet_consumed = 0;
  }
  if ((ret = agent_socket_recv(agent, &addr, buf, len)) > 0 && stun_probe(buf, len) == 0) {
    if (packet_consumed != NULL) {
      *packet_consumed = 1;
    }
    memcpy(stun_msg.buf, buf, ret);
    stun_msg.size = ret;
    stun_parse_msg_buf(&stun_msg);
    switch (stun_msg.stunclass) {
      case STUN_CLASS_REQUEST:
        agent_process_stun_request(agent, &stun_msg, &addr);
        break;
      case STUN_CLASS_RESPONSE:
        agent_process_stun_response(agent, &stun_msg, &addr);
        break;
      case STUN_CLASS_ERROR: {
        char addr_string[ADDRSTRLEN];
        addr_to_string(&addr, addr_string, sizeof(addr_string));
        LOGW("ICE received STUN error response from %s:%d, method=0x%04x, error=%d",
             addr_string, addr.port, (unsigned int)stun_msg.stunmethod,
             stun_msg.error_code);
        if (agent->nominated_pair != NULL &&
            agent->nominated_pair->state == ICE_CANDIDATE_STATE_INPROGRESS &&
            addr_equal(&addr, &agent->nominated_pair->remote->addr) &&
            memcmp(((StunHeader*)stun_msg.buf)->transaction_id,
                   agent->conncheck_transaction_id,
                   sizeof(agent->conncheck_transaction_id)) == 0) {
          agent->nominated_pair->state = ICE_CANDIDATE_STATE_FAILED;
        }
      } break;
      default:
        break;
    }
    ret = 0;
  } else if (ret > 0 && packet_consumed != NULL) {
    *packet_consumed = 1;
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
    if (strcmp(agent->remote_candidates[i].foundation,
               candidate.foundation) == 0) {
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
  // Please set gather candidates before set remote description
  for (i = 0; i < agent->local_candidates_count; i++) {
    for (j = 0; j < agent->remote_candidates_count; j++) {
      if (agent->local_candidates[i].addr.family == agent->remote_candidates[j].addr.family) {
        IceCandidatePair* pair = &agent->candidate_pairs[agent->candidate_pairs_num];

        pair->local = &agent->local_candidates[i];
        pair->remote = &agent->remote_candidates[j];
        pair->priority = agent_candidate_pair_priority(agent, pair->local, pair->remote);
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
  uint8_t buf[1400];
  uint32_t now;
  StunMessage msg;

  if (agent->nominated_pair == NULL ||
      (agent->nominated_pair->state != ICE_CANDIDATE_STATE_INPROGRESS &&
       agent->nominated_pair->state != ICE_CANDIDATE_STATE_SUCCEEDED)) {
    LOGI("nominated pair is not in progress");
    return -1;
  }

  memset(&msg, 0, sizeof(msg));
  now = ports_get_epoch_time();

  if (agent_select_succeeded_pair(agent, now) == 0) {
    return 0;
  }

  if (agent->nominated_pair->state == ICE_CANDIDATE_STATE_INPROGRESS &&
      (agent->nominated_pair->conncheck == 0 ||
       now - agent->conncheck_last_send_ms >= AGENT_CONNCHECK_INTERVAL_MS)) {
    addr_to_string(&agent->nominated_pair->remote->addr, addr_string, sizeof(addr_string));
    LOGD("send binding request to remote ip: %s, port: %d", addr_string, agent->nominated_pair->remote->addr.port);
    agent_create_binding_request(agent, &msg, 0);
    if (agent_socket_send(agent, &agent->nominated_pair->remote->addr, msg.buf, msg.size) < 0) {
      LOGW("ICE failed to send Binding Request to %s:%d",
           addr_string, agent->nominated_pair->remote->addr.port);
    }
    agent->conncheck_last_send_ms = now;
    agent->nominated_pair->conncheck++;
  }

  agent_recv(agent, buf, sizeof(buf), NULL);

  return agent_select_succeeded_pair(agent, now);
}

int agent_consent_check(Agent* agent) {
  char addr_string[ADDRSTRLEN];
  uint32_t now = ports_get_epoch_time();
  StunMessage msg;

  if (agent->selected_pair == NULL) {
    return -1;
  }

  if (now - agent->consent_last_send_ms >= CONFIG_KEEPALIVE_INTERVAL) {
    memset(&msg, 0, sizeof(msg));
    agent_create_binding_request(agent, &msg, 1);
    if (agent_socket_send(agent, &agent->selected_pair->remote->addr,
                          msg.buf, msg.size) < 0) {
      addr_to_string(&agent->selected_pair->remote->addr,
                     addr_string, sizeof(addr_string));
      LOGW("ICE failed to send consent request to %s:%d",
           addr_string, agent->selected_pair->remote->addr.port);
    }
    agent->consent_last_send_ms = now;
  }

  return now - agent->consent_last_received_ms > CONFIG_KEEPALIVE_TIMEOUT ? -1 : 0;
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
