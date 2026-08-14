#ifndef DISABLE_PEER_SIGNALING
#include "peer_mqtt.h"

#include <stdlib.h>
#include <string.h>

#include <mqtt.h>

#include "config.h"
#include "ports.h"
#include "ssl_transport.h"
#include "utils.h"

typedef struct PeerMqttPendingMessage PeerMqttPendingMessage;

struct PeerMqttPendingMessage {
  PeerMqttPendingMessage* next;
  size_t topic_len;
  size_t payload_len;
  uint8_t data[];
};

typedef struct PeerMqttExternal {
  struct mqtt_client client;
  NetworkContext_t network;
  uint8_t send_buffer[CONFIG_MQTT_BUFFER_SIZE];
  uint8_t receive_buffer[CONFIG_MQTT_BUFFER_SIZE];
  PeerMqttMessageCallback message_callback;
  void* message_callback_user_data;
  PeerMqttPendingMessage* pending_head;
  PeerMqttPendingMessage* pending_tail;
  int initialized;
  int transport_connected;
  int mqtt_connected;
} PeerMqttExternal;

static PeerMqttExternal* peer_mqtt_external_get(PeerMqttContext* context) {
  PeerMqttExternal* mqtt;

  if (context == NULL) {
    return NULL;
  }
  mqtt = context->private_data;
  if (mqtt == NULL) {
    mqtt = calloc(1, sizeof(*mqtt));
    if (mqtt == NULL) {
      LOGE("Failed to allocate external MQTT context");
      return NULL;
    }
    context->private_data = mqtt;
  }
  return mqtt;
}

static void peer_mqtt_external_clear_pending(PeerMqttExternal* mqtt) {
  PeerMqttPendingMessage* message = mqtt->pending_head;

  while (message != NULL) {
    PeerMqttPendingMessage* next = message->next;
    free(message);
    message = next;
  }
  mqtt->pending_head = NULL;
  mqtt->pending_tail = NULL;
}

static void peer_mqtt_external_queue_message(
    void** state,
    struct mqtt_response_publish* publish) {
  PeerMqttExternal* mqtt = *state;
  PeerMqttPendingMessage* message;
  size_t allocation_size;

  if (mqtt == NULL || publish == NULL) {
    return;
  }

  allocation_size = sizeof(*message) + publish->topic_name_size +
                    publish->application_message_size;
  message = malloc(allocation_size);
  if (message == NULL) {
    LOGE("Failed to queue MQTT message");
    return;
  }

  message->next = NULL;
  message->topic_len = publish->topic_name_size;
  message->payload_len = publish->application_message_size;
  memcpy(message->data, publish->topic_name, message->topic_len);
  memcpy(message->data + message->topic_len,
         publish->application_message, message->payload_len);

  if (mqtt->pending_tail == NULL) {
    mqtt->pending_head = message;
  } else {
    mqtt->pending_tail->next = message;
  }
  mqtt->pending_tail = message;
}

static void peer_mqtt_external_dispatch_pending(PeerMqttExternal* mqtt) {
  PeerMqttPendingMessage* message;

  while ((message = mqtt->pending_head) != NULL) {
    mqtt->pending_head = message->next;
    if (mqtt->pending_head == NULL) {
      mqtt->pending_tail = NULL;
    }
    if (mqtt->message_callback != NULL) {
      mqtt->message_callback(mqtt->message_callback_user_data,
                             (const char*)message->data,
                             message->topic_len,
                             message->data + message->topic_len,
                             message->payload_len);
    }
    free(message);
  }
}

static int peer_mqtt_external_sync(PeerMqttExternal* mqtt) {
  enum MQTTErrors status = mqtt_sync(&mqtt->client);

  peer_mqtt_external_dispatch_pending(mqtt);
  if (status != MQTT_OK) {
    LOGE("External MQTT sync failed: %s", mqtt_error_str(status));
    return -1;
  }
  return 0;
}

static int peer_mqtt_external_wait(PeerMqttExternal* mqtt,
                                   enum MQTTControlPacketType control_type,
                                   uint32_t timeout_ms) {
  uint32_t start = ports_get_epoch_time();

  while (mqtt_mq_find(&mqtt->client.mq, control_type, NULL) != NULL) {
    if (peer_mqtt_external_sync(mqtt) < 0) {
      return -1;
    }
    if (mqtt_mq_find(&mqtt->client.mq, control_type, NULL) == NULL) {
      return 0;
    }
    if ((uint32_t)(ports_get_epoch_time() - start) >= timeout_ms) {
      LOGE("External MQTT acknowledgement timeout: packet=%u",
           (unsigned int)control_type);
      return -1;
    }
    ports_sleep_ms(10);
  }
  return 0;
}

static int peer_mqtt_external_init(PeerMqttExternal* mqtt) {
  enum MQTTErrors status;

  peer_mqtt_external_clear_pending(mqtt);
  if (mqtt->initialized) {
    mqtt_reinit(&mqtt->client, &mqtt->network.ssl,
                mqtt->send_buffer, sizeof(mqtt->send_buffer),
                mqtt->receive_buffer, sizeof(mqtt->receive_buffer));
    MQTT_PAL_MUTEX_LOCK(&mqtt->client.mutex);
  } else {
    status = mqtt_init(&mqtt->client, &mqtt->network.ssl,
                       mqtt->send_buffer, sizeof(mqtt->send_buffer),
                       mqtt->receive_buffer, sizeof(mqtt->receive_buffer),
                       peer_mqtt_external_queue_message);
    if (status != MQTT_OK) {
      LOGE("External MQTT init failed: %s", mqtt_error_str(status));
      return -1;
    }
    mqtt->initialized = 1;
  }

  mqtt->client.publish_response_callback = peer_mqtt_external_queue_message;
  mqtt->client.publish_response_callback_state = mqtt;
  return 0;
}

int peer_mqtt_connect(PeerMqttContext* context,
                      const PeerMqttConnectOptions* options,
                      PeerMqttMessageCallback message_callback,
                      void* user_data) {
  PeerMqttExternal* mqtt;
  enum MQTTErrors status;
  uint8_t flags = MQTT_CONNECT_CLEAN_SESSION;

  if (options == NULL || options->host == NULL || options->port == 0 ||
      message_callback == NULL) {
    return -1;
  }
  mqtt = peer_mqtt_external_get(context);
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
  ssl_transport_set_nonblocking(&mqtt->network, 1);
  mqtt->message_callback = message_callback;
  mqtt->message_callback_user_data = user_data;
  if (peer_mqtt_external_init(mqtt) < 0) {
    ssl_transport_disconnect(&mqtt->network);
    mqtt->transport_connected = 0;
    return -1;
  }

  if (options->will_topic != NULL) {
    flags |= MQTT_CONNECT_WILL_RETAIN;
  }
  status = mqtt_connect(&mqtt->client, options->client_id,
                        options->will_topic, options->will_payload,
                        options->will_payload_len, options->username,
                        options->password, flags,
                        options->keep_alive_seconds);
  if (status != MQTT_OK ||
      peer_mqtt_external_wait(mqtt, MQTT_CONTROL_CONNECT,
                              options->timeout_ms) < 0) {
    if (status != MQTT_OK) {
      LOGE("External MQTT connect failed: %s", mqtt_error_str(status));
    }
    ssl_transport_disconnect(&mqtt->network);
    mqtt->transport_connected = 0;
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
  PeerMqttExternal* mqtt;
  enum MQTTErrors status;

  if (context == NULL || topic == NULL || payload == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }

  status = mqtt_publish(&mqtt->client, topic, payload, payload_len,
                        MQTT_PUBLISH_QOS_0 |
                            (retain ? MQTT_PUBLISH_RETAIN : 0));
  if (status != MQTT_OK) {
    LOGE("External MQTT publish failed: %s", mqtt_error_str(status));
    return -1;
  }
  return peer_mqtt_external_sync(mqtt);
}

int peer_mqtt_subscribe(PeerMqttContext* context,
                        const char* topic,
                        int subscribe,
                        uint32_t timeout_ms) {
  PeerMqttExternal* mqtt;
  enum MQTTErrors status;
  enum MQTTControlPacketType control_type;

  if (context == NULL || topic == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }

  LOGI("%s topic %s", subscribe ? "Subscribing" : "Unsubscribing", topic);
  if (subscribe) {
    status = mqtt_subscribe(&mqtt->client, topic, 0);
    control_type = MQTT_CONTROL_SUBSCRIBE;
  } else {
    status = mqtt_unsubscribe(&mqtt->client, topic);
    control_type = MQTT_CONTROL_UNSUBSCRIBE;
  }
  if (status != MQTT_OK) {
    LOGE("External MQTT %s failed: %s",
         subscribe ? "subscribe" : "unsubscribe", mqtt_error_str(status));
    return -1;
  }
  if (peer_mqtt_external_wait(mqtt, control_type, timeout_ms) < 0) {
    return -1;
  }
  LOGI("MQTT %s succeeded.", subscribe ? "Subscribe" : "Unsubscribe");
  return 0;
}

int peer_mqtt_process(PeerMqttContext* context) {
  PeerMqttExternal* mqtt;

  if (context == NULL) {
    return -1;
  }
  mqtt = context->private_data;
  if (mqtt == NULL || !mqtt->mqtt_connected) {
    return -1;
  }
  return peer_mqtt_external_sync(mqtt);
}

int peer_mqtt_disconnect(PeerMqttContext* context) {
  PeerMqttExternal* mqtt;
  enum MQTTErrors status = MQTT_OK;
  int sync_status = 0;

  if (context == NULL || context->private_data == NULL) {
    return 0;
  }
  mqtt = context->private_data;
  if (mqtt->mqtt_connected) {
    status = mqtt_disconnect(&mqtt->client);
    if (status == MQTT_OK) {
      sync_status = peer_mqtt_external_sync(mqtt);
    } else {
      LOGE("External MQTT disconnect failed: %s", mqtt_error_str(status));
    }
  }
  peer_mqtt_external_clear_pending(mqtt);
  if (mqtt->transport_connected) {
    ssl_transport_disconnect(&mqtt->network);
    mqtt->transport_connected = 0;
  }
  mqtt->mqtt_connected = 0;
  return status == MQTT_OK && sync_status == 0 ? 0 : -1;
}
#endif  // DISABLE_PEER_SIGNALING
