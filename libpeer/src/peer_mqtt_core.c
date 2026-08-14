#ifndef DISABLE_PEER_SIGNALING
#include "peer_mqtt.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <core_mqtt.h>

#include "config.h"
#include "ports.h"
#include "ssl_transport.h"
#include "utils.h"

typedef struct PeerMqttCore {
  MQTTContext_t client;
  MQTTFixedBuffer_t fixed_buffer;
  TransportInterface_t transport;
  NetworkContext_t network;
  PeerMqttMessageCallback message_callback;
  void* message_callback_user_data;
  uint8_t buffer[CONFIG_MQTT_BUFFER_SIZE];
  int transport_connected;
  int mqtt_connected;
} PeerMqttCore;

static PeerMqttCore* peer_mqtt_core_get(PeerMqttContext* context) {
  PeerMqttCore* mqtt;

  if (context == NULL) {
    return NULL;
  }
  mqtt = context->private_data;
  if (mqtt == NULL) {
    mqtt = calloc(1, sizeof(*mqtt));
    if (mqtt == NULL) {
      LOGE("Failed to allocate coreMQTT context");
      return NULL;
    }
    context->private_data = mqtt;
  }
  return mqtt;
}

static void peer_mqtt_core_close_transport(PeerMqttCore* mqtt) {
  if (mqtt->transport_connected) {
    ssl_transport_disconnect(&mqtt->network);
    mqtt->transport_connected = 0;
  }
  mqtt->mqtt_connected = 0;
}

static void peer_mqtt_core_event(MQTTContext_t* mqtt_context,
                                 MQTTPacketInfo_t* packet_info,
                                 MQTTDeserializedInfo_t* deserialized_info) {
  PeerMqttCore* mqtt =
      (PeerMqttCore*)((uint8_t*)mqtt_context - offsetof(PeerMqttCore, client));

  if (packet_info == NULL || deserialized_info == NULL) {
    return;
  }

  switch (packet_info->type) {
    case MQTT_PACKET_TYPE_PUBLISH: {
      MQTTPublishInfo_t* publish_info = deserialized_info->pPublishInfo;

      if (publish_info != NULL && mqtt->message_callback != NULL) {
        mqtt->message_callback(mqtt->message_callback_user_data,
                               publish_info->pTopicName,
                               publish_info->topicNameLength,
                               publish_info->pPayload,
                               publish_info->payloadLength);
      }
    } break;
    case MQTT_PACKET_TYPE_SUBACK: {
      MQTTStatus_t status;
      size_t code_count = 0;
      size_t i;
      uint8_t* codes = NULL;

      status = MQTT_GetSubAckStatusCodes(packet_info, &codes, &code_count);
      if (status != MQTTSuccess || code_count != 1) {
        LOGE("Invalid MQTT SUBACK");
        break;
      }
      for (i = 0; i < code_count; i++) {
        if (codes[i] == MQTTSubAckFailure) {
          LOGE("MQTT subscription failed. Please check authorization");
          break;
        }
      }
      if (i == code_count) {
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

int peer_mqtt_connect(PeerMqttContext* context,
                      const PeerMqttConnectOptions* options,
                      PeerMqttMessageCallback message_callback,
                      void* user_data) {
  PeerMqttCore* mqtt;
  MQTTConnectInfo_t connect_info = {0};
  MQTTPublishInfo_t will_info = {0};
  const MQTTPublishInfo_t* will = NULL;
  MQTTStatus_t status;
  bool session_present = false;

  if (options == NULL || options->host == NULL || options->port == 0 ||
      message_callback == NULL) {
    return -1;
  }
  mqtt = peer_mqtt_core_get(context);
  if (mqtt == NULL) {
    return -1;
  }
  if (mqtt->transport_connected) {
    peer_mqtt_disconnect(context);
  }

  if (ssl_transport_connect(&mqtt->network, options->host, options->port,
                            NULL) < 0) {
    LOGE("ssl transport connect failed");
    return -1;
  }
  mqtt->transport_connected = 1;
  mqtt->message_callback = message_callback;
  mqtt->message_callback_user_data = user_data;
  mqtt->transport.recv = ssl_transport_recv;
  mqtt->transport.send = ssl_transport_send;
  mqtt->transport.pNetworkContext = &mqtt->network;
  mqtt->fixed_buffer.pBuffer = mqtt->buffer;
  mqtt->fixed_buffer.size = sizeof(mqtt->buffer);

  status = MQTT_Init(&mqtt->client, &mqtt->transport,
                     ports_get_epoch_time, peer_mqtt_core_event,
                     &mqtt->fixed_buffer);
  if (status != MQTTSuccess) {
    LOGE("MQTT_Init failed: Status=%s.", MQTT_Status_strerror(status));
    peer_mqtt_core_close_transport(mqtt);
    return -1;
  }

  connect_info.cleanSession = true;
  connect_info.pClientIdentifier = options->client_id;
  connect_info.clientIdentifierLength =
      options->client_id != NULL ? strlen(options->client_id) : 0;
  connect_info.pUserName = options->username;
  connect_info.userNameLength =
      options->username != NULL ? strlen(options->username) : 0;
  connect_info.pPassword = options->password;
  connect_info.passwordLength =
      options->password != NULL ? strlen(options->password) : 0;
  connect_info.keepAliveSeconds = options->keep_alive_seconds;

  if (options->will_topic != NULL) {
    will_info.qos = MQTTQoS0;
    will_info.retain = true;
    will_info.pTopicName = options->will_topic;
    will_info.topicNameLength = strlen(options->will_topic);
    will_info.pPayload = options->will_payload;
    will_info.payloadLength = options->will_payload_len;
    will = &will_info;
  }

  status = MQTT_Connect(&mqtt->client, &connect_info, will,
                        options->timeout_ms, &session_present);
  if (status != MQTTSuccess) {
    LOGE("MQTT_Connect failed: Status=%s.", MQTT_Status_strerror(status));
    peer_mqtt_core_close_transport(mqtt);
    return -1;
  }
  mqtt->mqtt_connected = 1;
  return 0;
}

int peer_mqtt_publish(PeerMqttContext* context,
                      const char* topic,
                      const void* payload,
                      size_t payload_len,
                      int retain) {
  PeerMqttCore* mqtt;
  MQTTPublishInfo_t publish_info = {0};
  MQTTStatus_t status;

  if (context == NULL || topic == NULL || payload == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }

  publish_info.qos = MQTTQoS0;
  publish_info.retain = retain;
  publish_info.pTopicName = topic;
  publish_info.topicNameLength = strlen(topic);
  publish_info.pPayload = payload;
  publish_info.payloadLength = payload_len;
  status = MQTT_Publish(&mqtt->client, &publish_info,
                        MQTT_GetPacketId(&mqtt->client));
  if (status != MQTTSuccess) {
    LOGE("MQTT_Publish failed: Status=%s.", MQTT_Status_strerror(status));
    return -1;
  }
  LOGD("MQTT_Publish succeeded.");
  return 0;
}

int peer_mqtt_subscribe(PeerMqttContext* context,
                        const char* topic,
                        int subscribe,
                        uint32_t timeout_ms) {
  PeerMqttCore* mqtt;
  MQTTSubscribeInfo_t subscribe_info = {0};
  MQTTStatus_t status;
  uint16_t packet_id;

  (void)timeout_ms;
  if (context == NULL || topic == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }

  packet_id = MQTT_GetPacketId(&mqtt->client);
  subscribe_info.qos = MQTTQoS0;
  subscribe_info.pTopicFilter = topic;
  subscribe_info.topicFilterLength = strlen(topic);
  LOGI("%s topic %s", subscribe ? "Subscribing" : "Unsubscribing", topic);
  if (subscribe) {
    status = MQTT_Subscribe(&mqtt->client, &subscribe_info, 1, packet_id);
  } else {
    status = MQTT_Unsubscribe(&mqtt->client, &subscribe_info, 1, packet_id);
  }
  if (status != MQTTSuccess) {
    LOGE("MQTT_%s failed: Status=%s.",
         subscribe ? "Subscribe" : "Unsubscribe",
         MQTT_Status_strerror(status));
    return -1;
  }

  status = MQTT_ProcessLoop(&mqtt->client);
  if (status != MQTTSuccess) {
    LOGE("MQTT_ProcessLoop failed: Status=%s.",
         MQTT_Status_strerror(status));
    return -1;
  }
  ssl_transport_set_nonblocking(&mqtt->network, 1);
  return 0;
}

int peer_mqtt_process(PeerMqttContext* context) {
  PeerMqttCore* mqtt;
  MQTTStatus_t status;

  if (context == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }
  status = MQTT_ProcessLoop(&mqtt->client);
  if (status != MQTTSuccess) {
    LOGE("MQTT_ProcessLoop failed: Status=%s.",
         MQTT_Status_strerror(status));
    return -1;
  }
  return 0;
}

int peer_mqtt_disconnect(PeerMqttContext* context) {
  PeerMqttCore* mqtt;
  MQTTStatus_t status = MQTTSuccess;

  if (context == NULL || context->private_data == NULL) {
    return 0;
  }
  mqtt = context->private_data;
  if (mqtt->mqtt_connected) {
    status = MQTT_Disconnect(&mqtt->client);
    if (status != MQTTSuccess) {
      LOGE("Failed to disconnect with broker: %s",
           MQTT_Status_strerror(status));
    }
  }
  peer_mqtt_core_close_transport(mqtt);
  return status == MQTTSuccess ? 0 : -1;
}
#endif  // DISABLE_PEER_SIGNALING
