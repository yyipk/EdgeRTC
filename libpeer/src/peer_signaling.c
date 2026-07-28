#ifndef DISABLE_PEER_SIGNALING
#include <assert.h>
#include <ctype.h>
#include <cJSON.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <core_http_client.h>
#include <core_mqtt.h>

#include "base64.h"
#include "config.h"
#include "peer_signaling.h"
#include "ports.h"
#include "ssl_transport.h"
#include "utils.h"

#define KEEP_ALIVE_TIMEOUT_SECONDS 60
#define CONNACK_RECV_TIMEOUT_MS 1000

#define PATH_MAX_LEN 128
#define HOST_MAX_LEN 64
#define URL_MAX_LEN (PATH_MAX_LEN + HOST_MAX_LEN + 8)
#define TOPIC_MAX_LEN URL_MAX_LEN
#define TOKEN_MAX_LEN 256

#define RPC_VERSION "2.0"
#define RPC_METHOD_STATE "state"
#define RPC_METHOD_OFFER "offer"
#define RPC_METHOD_ANSWER "answer"
#define RPC_METHOD_CLOSE "close"

#define DEVICE_SIGNALING_VERSION 1
#define DEVICE_SESSION_MAX_LEN 48
#define DEVICE_TOPIC_SUFFIX "/device/%s/signal"

#define DEVICE_MESSAGE_OFFER "offer"
#define DEVICE_MESSAGE_ANSWER "answer"
#define DEVICE_MESSAGE_BUSY "busy"
#define DEVICE_MESSAGE_HANGUP "hangup"

#define RPC_ERROR_PARSE_ERROR "{\"code\":-32700,\"message\":\"Parse error\"}"
#define RPC_ERROR_INVALID_REQUEST "{\"code\":-32600,\"message\":\"Invalid Request\"}"
#define RPC_ERROR_METHOD_NOT_FOUND "{\"code\":-32601,\"message\":\"Method not found\"}"
#define RPC_ERROR_INVALID_PARAMS "{\"code\":-32602,\"message\":\"Invalid params\"}"
#define RPC_ERROR_INTERNAL_ERROR "{\"code\":-32603,\"message\":\"Internal error\"}"

typedef enum DevicePendingSdp {
  DEVICE_PENDING_SDP_NONE = 0,
  DEVICE_PENDING_SDP_OFFER,
  DEVICE_PENDING_SDP_ANSWER,
} DevicePendingSdp;

typedef struct PeerSignaling {
  MQTTContext_t mqtt_ctx;
  MQTTFixedBuffer_t mqtt_fixed_buf;

  TransportInterface_t transport;
  NetworkContext_t net_ctx;

  union {
    uint8_t mqtt[CONFIG_MQTT_BUFFER_SIZE];
    uint8_t http[CONFIG_HTTP_BUFFER_SIZE];
  } buffer;

  char subtopic[TOPIC_MAX_LEN];
  char pubtopic[TOPIC_MAX_LEN];
  char device_subtopic[TOPIC_MAX_LEN];

  uint16_t packet_id;
  int id;

  int proto;  // 0: MQTT, 1: HTTP
  int port;
  char host[HOST_MAX_LEN];
  char path[PATH_MAX_LEN];
  char token[TOKEN_MAX_LEN];
  char client_id[32];
  char device_id[PEER_SIGNALING_DEVICE_ID_SIZE];
  char direct_peer_id[PEER_SIGNALING_DEVICE_ID_SIZE];
  char direct_session[DEVICE_SESSION_MAX_LEN];
  uint32_t direct_session_counter;
  DevicePendingSdp direct_pending_sdp;
  int direct_publish_status;
  int direct_call_active;
  int direct_call_outgoing;

  PeerConnection* pc;
  int connected;

} PeerSignaling;

static PeerSignaling g_ps = {0};

static int peer_signaling_device_id_is_valid(const char* device_id) {
  size_t i;

  if (device_id == NULL || strlen(device_id) != PEER_SIGNALING_DEVICE_ID_LENGTH) {
    return 0;
  }
  for (i = 0; i < PEER_SIGNALING_DEVICE_ID_LENGTH; i++) {
    if (!isxdigit((unsigned char)device_id[i])) {
      return 0;
    }
  }
  return 1;
}

static void peer_signaling_copy_device_id(char* destination, const char* source) {
  size_t i;

  for (i = 0; i < PEER_SIGNALING_DEVICE_ID_LENGTH; i++) {
    destination[i] = (char)tolower((unsigned char)source[i]);
  }
  destination[PEER_SIGNALING_DEVICE_ID_LENGTH] = '\0';
}

static int peer_signaling_state_can_negotiate(PeerConnectionState state) {
  return state == PEER_CONNECTION_NEW ||
         state == PEER_CONNECTION_DISCONNECTED ||
         state == PEER_CONNECTION_FAILED ||
         state == PEER_CONNECTION_CLOSED;
}

static void peer_signaling_clear_direct_call(void) {
  memset(g_ps.direct_peer_id, 0, sizeof(g_ps.direct_peer_id));
  memset(g_ps.direct_session, 0, sizeof(g_ps.direct_session));
  g_ps.direct_pending_sdp = DEVICE_PENDING_SDP_NONE;
  g_ps.direct_publish_status = 0;
  g_ps.direct_call_active = 0;
  g_ps.direct_call_outgoing = 0;
}

static int peer_signaling_resolve_token(const char* token, char* username, char* password) {
  char plaintext[TOKEN_MAX_LEN] = {0};
  char* colon;

  if (token == NULL || strlen(token) == 0) {
    LOGW("Invalid token");
    return -1;
  }
  base64_decode(token, strlen(token), (unsigned char*)plaintext, sizeof(plaintext));
  colon = strchr(plaintext, ':');
  if (colon == NULL) {
    LOGW("Invalid token: %s", token);
    return -1;
  }

  strncpy(username, plaintext, colon - plaintext);
  strncpy(password, colon + 1, strlen(colon + 1));
  LOGD("Username: %s, Password: %s", username, password);
  return 0;
}

static int peer_signaling_resolve_url(const char* url, char* host, int* port, char* path) {
  char *port_start, *path_start;
  int proto = 0;

  if (url == NULL || strlen(url) == 0) {
    LOGW("Invalid URL");
    return -1;
  }

  if (strncmp(url, "mqtts://", 8) == 0) {
    *port = 8883;
    url += 8;
  } else if (strncmp(url, "https://", 8) == 0) {
    *port = 443;
    url += 8;
    proto = 1;
  } else if (strncmp(url, "mqtt://", 7) == 0) {
    *port = 1883;
    url += 7;
  } else if (strncmp(url, "http://", 7) == 0) {
    *port = 80;
    url += 7;
    proto = 1;
  } else {
    LOGW("Invalid URL: %s", url);
    return -1;
  }

  port_start = strchr(url, ':');
  path_start = strchr(url, '/');

  if (port_start != NULL && path_start != NULL && port_start < path_start) {
    strncpy(host, url, port_start - url);
    strncpy(path, path_start, strlen(path_start));
    *port = atoi(port_start + 1);
  } else if (port_start == NULL && path_start != NULL) {
    strncpy(host, url, path_start - url);
    strncpy(path, path_start, strlen(path_start));
  } else if (port_start != NULL && path_start == NULL) {
    strncpy(host, url, port_start - url);
    *port = atoi(port_start + 1);
  } else {
    strncpy(host, url, strlen(url));
  }

  LOGI("Host: %s, Port: %d, Path: %s", host, *port, path);
  return proto;
}

static int peer_signaling_mqtt_publish_to(MQTTContext_t* mqtt_ctx,
                                          const char* topic,
                                          const char* message) {
  MQTTStatus_t status;
  MQTTPublishInfo_t pub_info;

  memset(&pub_info, 0, sizeof(pub_info));

  pub_info.qos = MQTTQoS0;
  pub_info.retain = false;
  pub_info.pTopicName = topic;
  pub_info.topicNameLength = strlen(topic);
  pub_info.pPayload = message;
  pub_info.payloadLength = strlen(message);

  status = MQTT_Publish(mqtt_ctx, &pub_info, MQTT_GetPacketId(mqtt_ctx));
  if (status != MQTTSuccess) {
    LOGE("MQTT_Publish failed: Status=%s.", MQTT_Status_strerror(status));
    return -1;
  } else {
    LOGD("MQTT_Publish succeeded.");
  }
  return 0;
}

static void peer_signaling_mqtt_publish(MQTTContext_t* mqtt_ctx, const char* message) {
  peer_signaling_mqtt_publish_to(mqtt_ctx, g_ps.pubtopic, message);
}

static int peer_signaling_publish_device_message(const char* device_id,
                                                  const char* session,
                                                  const char* type,
                                                  const char* sdp) {
  char topic[TOPIC_MAX_LEN];
  char* payload = NULL;
  cJSON* message = NULL;
  int ret = -1;

  if (!g_ps.connected || g_ps.proto != 0 ||
      !peer_signaling_device_id_is_valid(device_id) ||
      session == NULL || session[0] == '\0' || type == NULL ||
      snprintf(topic, sizeof(topic), "%s" DEVICE_TOPIC_SUFFIX,
               g_ps.path, device_id) >= (int)sizeof(topic)) {
    return -1;
  }

  message = cJSON_CreateObject();
  if (message == NULL) {
    return -1;
  }
  cJSON_AddNumberToObject(message, "version", DEVICE_SIGNALING_VERSION);
  cJSON_AddStringToObject(message, "session", session);
  cJSON_AddStringToObject(message, "from", g_ps.device_id);
  cJSON_AddStringToObject(message, "type", type);
  if (sdp != NULL) {
    cJSON_AddStringToObject(message, "sdp", sdp);
  }

  payload = cJSON_PrintUnformatted(message);
  if (payload != NULL) {
    ret = peer_signaling_mqtt_publish_to(&g_ps.mqtt_ctx, topic, payload);
    free(payload);
  }
  cJSON_Delete(message);
  return ret;
}

static void peer_signaling_on_pub_event(const char* msg, size_t size) {
  cJSON *req, *res, *item, *result, *error;
  int id = -1;
  char* payload = NULL;
  PeerConnectionState state;

  req = res = item = result = error = NULL;
  state = peer_connection_get_state(g_ps.pc);
  do {
    req = cJSON_ParseWithLength(msg, size);
    if (!req) {
      error = cJSON_CreateRaw(RPC_ERROR_PARSE_ERROR);
      LOGW("Parse json failed");
      break;
    }

    item = cJSON_GetObjectItem(req, "method");
    if (item == NULL &&
        (cJSON_GetObjectItem(req, "result") != NULL ||
         cJSON_GetObjectItem(req, "error") != NULL)) {
      break;
    }

    item = cJSON_GetObjectItem(req, "id");
    if (!cJSON_IsNumber(item)) {
      error = cJSON_CreateRaw(RPC_ERROR_INVALID_REQUEST);
      LOGW("Cannot find id");
      break;
    }

    id = item->valueint;

    item = cJSON_GetObjectItem(req, "method");
    if (!cJSON_IsString(item)) {
      error = cJSON_CreateRaw(RPC_ERROR_INVALID_REQUEST);
      LOGW("Cannot find method");
      break;
    }

    if (strcmp(item->valuestring, RPC_METHOD_OFFER) == 0) {
      if (g_ps.direct_call_active) {
        error = cJSON_CreateRaw(RPC_ERROR_INTERNAL_ERROR);
        break;
      }
      switch (state) {
        case PEER_CONNECTION_NEW:
        case PEER_CONNECTION_DISCONNECTED:
        case PEER_CONNECTION_FAILED:
        case PEER_CONNECTION_CLOSED: {
          g_ps.id = id;
          peer_connection_create_offer(g_ps.pc);
        } break;
        default: {
          error = cJSON_CreateRaw(RPC_ERROR_INTERNAL_ERROR);
        } break;
      }
    } else if (strcmp(item->valuestring, RPC_METHOD_ANSWER) == 0) {
      item = cJSON_GetObjectItem(req, "params");
      if (!cJSON_IsString(item)) {
        error = cJSON_CreateRaw(RPC_ERROR_INVALID_PARAMS);
        LOGW("Cannot find params");
        break;
      }
      if (g_ps.direct_call_active) {
        error = cJSON_CreateRaw(RPC_ERROR_INTERNAL_ERROR);
      } else {
        peer_connection_set_remote_description(g_ps.pc, item->valuestring,
                                               SDP_TYPE_ANSWER);
      }

    } else if (strcmp(item->valuestring, RPC_METHOD_STATE) == 0) {
      result = cJSON_CreateString(peer_connection_state_to_string(state));

    } else if (strcmp(item->valuestring, RPC_METHOD_CLOSE) == 0) {
      if (g_ps.direct_call_active) {
        error = cJSON_CreateRaw(RPC_ERROR_INTERNAL_ERROR);
      } else {
        peer_connection_close(g_ps.pc);
        result = cJSON_CreateString("");
      }

    } else {
      error = cJSON_CreateRaw(RPC_ERROR_METHOD_NOT_FOUND);
      LOGW("Unsupport method");
    }

  } while (0);

  if (result || error) {
    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", RPC_VERSION);
    cJSON_AddNumberToObject(res, "id", id);

    if (result) {
      cJSON_AddItemToObject(res, "result", result);
    } else if (error) {
      cJSON_AddItemToObject(res, "error", error);
    }

    payload = cJSON_PrintUnformatted(res);

    if (payload) {
      peer_signaling_mqtt_publish(&g_ps.mqtt_ctx, payload);
      free(payload);
    }
    cJSON_Delete(res);
  }

  if (req) {
    cJSON_Delete(req);
  }
}

static void peer_signaling_on_device_event(const char* msg, size_t size) {
  cJSON* request;
  cJSON* version;
  cJSON* session;
  cJSON* from;
  cJSON* type;
  cJSON* sdp;
  PeerConnectionState state;

  request = cJSON_ParseWithLength(msg, size);
  if (request == NULL) {
    LOGW("Invalid device signaling JSON");
    return;
  }

  version = cJSON_GetObjectItem(request, "version");
  session = cJSON_GetObjectItem(request, "session");
  from = cJSON_GetObjectItem(request, "from");
  type = cJSON_GetObjectItem(request, "type");
  sdp = cJSON_GetObjectItem(request, "sdp");
  if (!cJSON_IsNumber(version) || version->valueint != DEVICE_SIGNALING_VERSION ||
      !cJSON_IsString(session) || session->valuestring[0] == '\0' ||
      strlen(session->valuestring) >= sizeof(g_ps.direct_session) ||
      !cJSON_IsString(from) ||
      !peer_signaling_device_id_is_valid(from->valuestring) ||
      strcasecmp(from->valuestring, g_ps.device_id) == 0 ||
      !cJSON_IsString(type)) {
    LOGW("Invalid device signaling message");
    cJSON_Delete(request);
    return;
  }

  state = peer_connection_get_state(g_ps.pc);
  if (g_ps.direct_call_active &&
      (state == PEER_CONNECTION_FAILED || state == PEER_CONNECTION_CLOSED)) {
    peer_signaling_clear_direct_call();
  }

  if (strcmp(type->valuestring, DEVICE_MESSAGE_OFFER) == 0) {
    if (!cJSON_IsString(sdp)) {
      LOGW("Device offer has no SDP");
    } else if (g_ps.direct_call_active ||
               !peer_signaling_state_can_negotiate(state)) {
      peer_signaling_publish_device_message(from->valuestring,
                                            session->valuestring,
                                            DEVICE_MESSAGE_BUSY, NULL);
    } else {
      peer_signaling_copy_device_id(g_ps.direct_peer_id, from->valuestring);
      snprintf(g_ps.direct_session, sizeof(g_ps.direct_session), "%s",
               session->valuestring);
      g_ps.direct_call_active = 1;
      g_ps.direct_call_outgoing = 0;
      g_ps.direct_pending_sdp = DEVICE_PENDING_SDP_ANSWER;
      g_ps.direct_publish_status = -1;
      peer_connection_set_remote_description(g_ps.pc, sdp->valuestring,
                                             SDP_TYPE_OFFER);
      peer_connection_create_answer(g_ps.pc);
      if (g_ps.direct_publish_status < 0) {
        LOGE("Failed to publish device answer");
        peer_connection_close(g_ps.pc);
        peer_signaling_clear_direct_call();
      } else {
        LOGI("Accepted call from %s", g_ps.direct_peer_id);
      }
    }
  } else if (strcmp(type->valuestring, DEVICE_MESSAGE_ANSWER) == 0) {
    if (g_ps.direct_call_active && g_ps.direct_call_outgoing &&
        strcmp(g_ps.direct_peer_id, from->valuestring) == 0 &&
        strcmp(g_ps.direct_session, session->valuestring) == 0 &&
        cJSON_IsString(sdp)) {
      peer_connection_set_remote_description(g_ps.pc, sdp->valuestring,
                                             SDP_TYPE_ANSWER);
      LOGI("Received answer from %s", g_ps.direct_peer_id);
    }
  } else if (strcmp(type->valuestring, DEVICE_MESSAGE_BUSY) == 0) {
    if (g_ps.direct_call_active && g_ps.direct_call_outgoing &&
        strcmp(g_ps.direct_peer_id, from->valuestring) == 0 &&
        strcmp(g_ps.direct_session, session->valuestring) == 0) {
      LOGW("Device %s is busy", g_ps.direct_peer_id);
      peer_connection_close(g_ps.pc);
      peer_signaling_clear_direct_call();
    }
  } else if (strcmp(type->valuestring, DEVICE_MESSAGE_HANGUP) == 0) {
    if (g_ps.direct_call_active &&
        strcmp(g_ps.direct_peer_id, from->valuestring) == 0 &&
        strcmp(g_ps.direct_session, session->valuestring) == 0) {
      LOGI("Call ended by %s", g_ps.direct_peer_id);
      peer_connection_close(g_ps.pc);
      peer_signaling_clear_direct_call();
    }
  } else {
    LOGW("Unsupported device signaling type");
  }

  cJSON_Delete(request);
}

HTTPResponse_t peer_signaling_http_request(const TransportInterface_t* transport_interface,
                                           const char* method,
                                           size_t method_len,
                                           const char* host,
                                           size_t host_len,
                                           const char* path,
                                           size_t path_len,
                                           const char* auth,
                                           size_t auth_len,
                                           const char* body,
                                           size_t body_len) {
  HTTPStatus_t status = HTTPSuccess;
  HTTPRequestInfo_t request_info = {0};
  HTTPResponse_t response = {0};
  HTTPRequestHeaders_t request_headers = {0};

  request_info.pMethod = method;
  request_info.methodLen = method_len;
  request_info.pHost = host;
  request_info.hostLen = host_len;
  request_info.pPath = path;
  request_info.pathLen = path_len;
  request_info.reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;

  request_headers.pBuffer = g_ps.buffer.http;
  request_headers.bufferLen = sizeof(g_ps.buffer.http);

  status = HTTPClient_InitializeRequestHeaders(&request_headers, &request_info);

  if (status == HTTPSuccess) {
    HTTPClient_AddHeader(&request_headers,
                         "Content-Type", strlen("Content-Type"), "application/sdp", strlen("application/sdp"));

    if (auth_len > 0) {
      HTTPClient_AddHeader(&request_headers,
                           "Authorization", strlen("Authorization"), auth, auth_len);
    }

    response.pBuffer = g_ps.buffer.http;
    response.bufferLen = sizeof(g_ps.buffer.http);

    status = HTTPClient_Send(transport_interface,
                             &request_headers, (uint8_t*)body, body ? body_len : 0, &response, 0);

  } else {
    LOGE("Failed to initialize HTTP request headers: Error=%s.", HTTPClient_strerror(status));
  }

  return response;
}

static int peer_signaling_http_post(const char* hostname, const char* path, int port, const char* auth, const char* body) {
  int ret = 0;
  TransportInterface_t trans_if = {0};
  NetworkContext_t net_ctx;
  HTTPResponse_t res;

  trans_if.recv = ssl_transport_recv;
  trans_if.send = ssl_transport_send;
  trans_if.pNetworkContext = &net_ctx;

  assert(port > 0);

  ret = ssl_transport_connect(&net_ctx, hostname, port, NULL);

  if (ret < 0) {
    LOGE("Failed to connect to %s:%d", hostname, port);
    return ret;
  }

  res = peer_signaling_http_request(&trans_if, "POST", 4, hostname, strlen(hostname), path,
                                    strlen(path), auth, strlen(auth), body, strlen(body));

  ssl_transport_disconnect(&net_ctx);

  if (res.pHeaders == NULL) {
    LOGE("Response headers are NULL");
    return -1;
  }

  if (res.pBody == NULL) {
    LOGE("Response body is NULL");
    return -1;
  }

  LOGI(
      "Received HTTP response from %s%s\n"
      "Response Headers: %s\nResponse Status: %u\nResponse Body: %s\n",
      hostname, path, res.pHeaders, res.statusCode, res.pBody);

  if (res.statusCode == 201) {
    peer_connection_set_remote_description(g_ps.pc, (const char*)res.pBody, SDP_TYPE_ANSWER);
  }
  return 0;
}

static void peer_signaling_mqtt_event_cb(MQTTContext_t* mqtt_ctx,
                                         MQTTPacketInfo_t* packet_info,
                                         MQTTDeserializedInfo_t* deserialized_info) {
  MQTTStatus_t status = MQTTSuccess;
  switch (packet_info->type) {
    case MQTT_PACKET_TYPE_PUBLISH: {
      MQTTPublishInfo_t* publish_info = deserialized_info->pPublishInfo;
      size_t device_topic_length = strlen(g_ps.device_subtopic);

      LOGD("MQTT received message: %.*s",
           publish_info->payloadLength, (char*)publish_info->pPayload);
      if (publish_info->topicNameLength == device_topic_length &&
          memcmp(publish_info->pTopicName, g_ps.device_subtopic,
                 device_topic_length) == 0) {
        peer_signaling_on_device_event(publish_info->pPayload,
                                       publish_info->payloadLength);
      } else {
        peer_signaling_on_pub_event(publish_info->pPayload,
                                    publish_info->payloadLength);
      }
    } break;
    case MQTT_PACKET_TYPE_SUBACK: {
      size_t ncodes = 0;
      int i = 0;
      uint8_t* codes = NULL;
      status = MQTT_GetSubAckStatusCodes(packet_info, &codes, &ncodes);

      assert(status == MQTTSuccess);

      assert(ncodes == 1);

      for (i = 0; i < ncodes; i++) {
        if (codes[0] == MQTTSubAckFailure) {
          LOGE("MQTT Subscription failed. Please check authorization");
          break;
        }
      }

      if (i == ncodes) {
        LOGI("MQTT Subscribe succeeded.");
      }
    } break;
    case MQTT_PACKET_TYPE_UNSUBACK:
      LOGI("MQTT Unsubscribe succeeded.");
      break;
    default:
      break;
  }
}

static int peer_signaling_mqtt_connect(const char* hostname, int port) {
  MQTTStatus_t status;
  MQTTConnectInfo_t conn_info;
  bool session_present;
  char username[TOKEN_MAX_LEN] = {0};
  char password[TOKEN_MAX_LEN] = {0};

  g_ps.connected = 0;
  if (ssl_transport_connect(&g_ps.net_ctx, hostname, port, NULL) < 0) {
    LOGE("ssl transport connect failed");
    return -1;
  }

  g_ps.transport.recv = ssl_transport_recv;
  g_ps.transport.send = ssl_transport_send;
  g_ps.transport.pNetworkContext = &g_ps.net_ctx;
  g_ps.mqtt_fixed_buf.pBuffer = g_ps.buffer.mqtt;
  g_ps.mqtt_fixed_buf.size = sizeof(g_ps.buffer.mqtt);
  status = MQTT_Init(&g_ps.mqtt_ctx, &g_ps.transport,
                     ports_get_epoch_time, peer_signaling_mqtt_event_cb, &g_ps.mqtt_fixed_buf);
  if (status != MQTTSuccess) {
    LOGE("MQTT_Init failed: Status=%s.", MQTT_Status_strerror(status));
    ssl_transport_disconnect(&g_ps.net_ctx);
    return -1;
  }

  memset(&conn_info, 0, sizeof(conn_info));

  conn_info.cleanSession = true;

  if (strlen(g_ps.token) > 0) {
    peer_signaling_resolve_token(g_ps.token, username, password);
    conn_info.pUserName = username;
    conn_info.userNameLength = strlen(username);
    conn_info.pPassword = password;
    conn_info.passwordLength = strlen(password);
  }

  if (strlen(g_ps.client_id) > 0) {
    conn_info.pClientIdentifier = g_ps.client_id;
    conn_info.clientIdentifierLength = strlen(g_ps.client_id);
  }

  conn_info.keepAliveSeconds = KEEP_ALIVE_TIMEOUT_SECONDS;

  status = MQTT_Connect(&g_ps.mqtt_ctx,
                        &conn_info, NULL, CONNACK_RECV_TIMEOUT_MS, &session_present);

  if (status != MQTTSuccess) {
    LOGE("MQTT_Connect failed: Status=%s.", MQTT_Status_strerror(status));
    ssl_transport_disconnect(&g_ps.net_ctx);
    return -1;
  }

  LOGI("MQTT_Connect succeeded.");
  g_ps.connected = 1;
  return 0;
}

static int peer_signaling_mqtt_subscribe_topic(const char* topic,
                                                int subscribed) {
  MQTTStatus_t status = MQTTSuccess;
  MQTTSubscribeInfo_t sub_info;

  uint16_t packet_id = MQTT_GetPacketId(&g_ps.mqtt_ctx);

  memset(&sub_info, 0, sizeof(sub_info));
  sub_info.qos = MQTTQoS0;
  sub_info.pTopicFilter = topic;
  sub_info.topicFilterLength = strlen(topic);

  if (subscribed) {
    LOGI("Subscribing topic %s", topic);
    status = MQTT_Subscribe(&g_ps.mqtt_ctx, &sub_info, 1, packet_id);
  } else {
    LOGI("Unsubscribing topic %s", topic);
    status = MQTT_Unsubscribe(&g_ps.mqtt_ctx, &sub_info, 1, packet_id);
  }
  if (status != MQTTSuccess) {
    LOGE("MQTT_Subscribe failed: Status=%s.", MQTT_Status_strerror(status));
    return -1;
  }

  status = MQTT_ProcessLoop(&g_ps.mqtt_ctx);

  if (status != MQTTSuccess) {
    LOGE("MQTT_ProcessLoop failed: Status=%s.", MQTT_Status_strerror(status));
    return -1;
  }

  return 0;
}

static void peer_signaling_onicecandidate(char* description, void* userdata) {
  cJSON* res;
  char* payload;
  if (g_ps.direct_pending_sdp != DEVICE_PENDING_SDP_NONE) {
    const char* type = g_ps.direct_pending_sdp == DEVICE_PENDING_SDP_OFFER ?
                       DEVICE_MESSAGE_OFFER : DEVICE_MESSAGE_ANSWER;

    g_ps.direct_publish_status =
        peer_signaling_publish_device_message(g_ps.direct_peer_id,
                                              g_ps.direct_session,
                                              type, description);
    g_ps.direct_pending_sdp = DEVICE_PENDING_SDP_NONE;
  } else if (g_ps.id > 0) {
    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "jsonrpc", RPC_VERSION);
    cJSON_AddNumberToObject(res, "id", g_ps.id);
    cJSON_AddStringToObject(res, "result", description);
    payload = cJSON_PrintUnformatted(res);
    if (payload) {
      peer_signaling_mqtt_publish(&g_ps.mqtt_ctx, payload);
      free(payload);
    }
    cJSON_Delete(res);
    g_ps.id = 0;
  } else {
    if (strlen(g_ps.token) > 0) {
      char cred[TOKEN_MAX_LEN + 10];
      memset(cred, 0, sizeof(cred));
      snprintf(cred, sizeof(cred), "Bearer %s", g_ps.token);
      peer_signaling_http_post(g_ps.host, g_ps.path, g_ps.port, cred, description);
    } else {
      peer_signaling_http_post(g_ps.host, g_ps.path, g_ps.port, "", description);
    }
  }
}

int peer_signaling_connect(const char* url, const char* token, PeerConnection* pc) {
  uint8_t mac[6];
  char* client_id;
  int ret = 0;

  peer_signaling_clear_direct_call();
  memset(g_ps.device_id, 0, sizeof(g_ps.device_id));
  memset(g_ps.device_subtopic, 0, sizeof(g_ps.device_subtopic));

  if ((g_ps.proto = peer_signaling_resolve_url(url, g_ps.host, &g_ps.port, g_ps.path)) < 0) {
    LOGE("Resolve URL failed");
    return -1;
  }

  if (token && strlen(token) > 0) {
    strncpy(g_ps.token, token, sizeof(g_ps.token));
  }

  g_ps.pc = pc;
  peer_connection_onicecandidate(g_ps.pc, peer_signaling_onicecandidate);

  switch (g_ps.proto) {
    case 0: {  // MQTT
      if (ports_get_mac_addr(mac) > 0) {
        snprintf(g_ps.client_id, sizeof(g_ps.client_id),
                 "bsm-%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(g_ps.device_id, sizeof(g_ps.device_id),
                 "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      } else {
        client_id = strrchr(g_ps.path, '/');
        snprintf(g_ps.client_id, sizeof(g_ps.client_id), "%s", client_id + 1);
      }
      LOGI("MQTT client ID: %s", g_ps.client_id);
      snprintf(g_ps.subtopic, sizeof(g_ps.subtopic), "%s/invoke", g_ps.path);
      snprintf(g_ps.pubtopic, sizeof(g_ps.pubtopic), "%s/result", g_ps.path);
      if (g_ps.device_id[0] != '\0') {
        if (snprintf(g_ps.device_subtopic, sizeof(g_ps.device_subtopic),
                     "%s" DEVICE_TOPIC_SUFFIX,
                     g_ps.path, g_ps.device_id) >=
            (int)sizeof(g_ps.device_subtopic)) {
          LOGE("Device signaling topic is too long");
          return -1;
        }
        LOGI("Device signaling ID: %s", g_ps.device_id);
      }
      ret = peer_signaling_mqtt_connect(g_ps.host, g_ps.port);
      if (ret == 0) {
        ret = peer_signaling_mqtt_subscribe_topic(g_ps.subtopic, 1);
        if (ret == 0 && g_ps.device_subtopic[0] != '\0') {
          ret = peer_signaling_mqtt_subscribe_topic(g_ps.device_subtopic, 1);
          if (ret < 0) {
            peer_signaling_mqtt_subscribe_topic(g_ps.subtopic, 0);
          }
        }
        if (ret < 0) {
          ssl_transport_disconnect(&g_ps.net_ctx);
          g_ps.connected = 0;
        } else {
          ssl_transport_set_nonblocking(&g_ps.net_ctx, 1);
        }
      }
    } break;
    case 1: {  // HTTP
      peer_connection_create_offer(g_ps.pc);
    } break;
    default: {
    } break;
  }

  return ret;
}

void peer_signaling_disconnect() {
  MQTTStatus_t status = MQTTSuccess;

  if (!g_ps.proto && g_ps.connected) {
    if (g_ps.device_subtopic[0] != '\0') {
      peer_signaling_mqtt_subscribe_topic(g_ps.device_subtopic, 0);
    }
    if (!peer_signaling_mqtt_subscribe_topic(g_ps.subtopic, 0)) {
      status = MQTT_Disconnect(&g_ps.mqtt_ctx);
      if (status != MQTTSuccess) {
        LOGE("Failed to disconnect with broker: %s", MQTT_Status_strerror(status));
      }
    }

    ssl_transport_disconnect(&g_ps.net_ctx);
    g_ps.connected = 0;
  }
  g_ps.pc = NULL;
  peer_signaling_clear_direct_call();
  LOGI("Disconnected");
}

int peer_signaling_call(const char* device_id) {
  PeerConnectionState state;

  if (!g_ps.connected || g_ps.proto != 0 || g_ps.pc == NULL ||
      !peer_signaling_device_id_is_valid(device_id) ||
      g_ps.device_id[0] == '\0' ||
      strcasecmp(device_id, g_ps.device_id) == 0) {
    return -1;
  }

  state = peer_connection_get_state(g_ps.pc);
  if (g_ps.direct_call_active ||
      !peer_signaling_state_can_negotiate(state)) {
    return -1;
  }

  peer_signaling_copy_device_id(g_ps.direct_peer_id, device_id);
  g_ps.direct_session_counter++;
  snprintf(g_ps.direct_session, sizeof(g_ps.direct_session),
           "%s-%08lx-%lu", g_ps.device_id,
           (unsigned long)ports_get_epoch_time(),
           (unsigned long)g_ps.direct_session_counter);
  g_ps.direct_call_active = 1;
  g_ps.direct_call_outgoing = 1;
  g_ps.direct_pending_sdp = DEVICE_PENDING_SDP_OFFER;
  g_ps.direct_publish_status = -1;

  peer_connection_create_offer(g_ps.pc);
  if (g_ps.direct_publish_status < 0) {
    LOGE("Failed to publish device offer");
    peer_connection_close(g_ps.pc);
    peer_signaling_clear_direct_call();
    return -1;
  }

  LOGI("Calling device %s", g_ps.direct_peer_id);
  return 0;
}

int peer_signaling_hangup() {
  int ret;

  if (!g_ps.direct_call_active || g_ps.pc == NULL) {
    return -1;
  }

  ret = peer_signaling_publish_device_message(g_ps.direct_peer_id,
                                              g_ps.direct_session,
                                              DEVICE_MESSAGE_HANGUP, NULL);
  peer_connection_close(g_ps.pc);
  peer_signaling_clear_direct_call();
  LOGI("Device call ended");
  return ret;
}

int peer_signaling_get_device_id(char* device_id, size_t size) {
  if (device_id == NULL || size < sizeof(g_ps.device_id) ||
      !g_ps.connected || g_ps.device_id[0] == '\0') {
    return -1;
  }

  memcpy(device_id, g_ps.device_id, sizeof(g_ps.device_id));
  return 0;
}

int peer_signaling_loop() {
  MQTTStatus_t status;

  if (g_ps.proto || !g_ps.connected) {
    return g_ps.proto ? 0 : -1;
  }

  status = MQTT_ProcessLoop(&g_ps.mqtt_ctx);
  if (status != MQTTSuccess) {
    LOGE("MQTT_ProcessLoop failed: Status=%s.", MQTT_Status_strerror(status));
    return -1;
  }
  if (g_ps.direct_call_active &&
      (peer_connection_get_state(g_ps.pc) == PEER_CONNECTION_FAILED ||
       peer_connection_get_state(g_ps.pc) == PEER_CONNECTION_CLOSED)) {
    peer_signaling_clear_direct_call();
  }
  return 0;
}
#endif  // DISABLE_PEER_SIGNALING
