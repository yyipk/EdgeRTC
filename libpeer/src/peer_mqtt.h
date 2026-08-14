#ifndef PEER_MQTT_H_
#define PEER_MQTT_H_

#include <stddef.h>
#include <stdint.h>

typedef struct PeerMqttContext {
  void* private_data;
} PeerMqttContext;

typedef void (*PeerMqttMessageCallback)(void* user_data,
                                        const char* topic,
                                        size_t topic_len,
                                        const void* payload,
                                        size_t payload_len);

typedef struct PeerMqttConnectOptions {
  const char* host;
  uint16_t port;
  const char* client_id;
  const char* username;
  const char* password;
  uint16_t keep_alive_seconds;
  const char* will_topic;
  const void* will_payload;
  size_t will_payload_len;
  uint32_t timeout_ms;
} PeerMqttConnectOptions;

int peer_mqtt_connect(PeerMqttContext* context,
                      const PeerMqttConnectOptions* options,
                      PeerMqttMessageCallback message_callback,
                      void* user_data);

int peer_mqtt_publish(PeerMqttContext* context,
                      const char* topic,
                      const void* payload,
                      size_t payload_len,
                      int retain);

int peer_mqtt_subscribe(PeerMqttContext* context,
                        const char* topic,
                        int subscribe,
                        uint32_t timeout_ms);

int peer_mqtt_process(PeerMqttContext* context);

int peer_mqtt_disconnect(PeerMqttContext* context);

#endif  // PEER_MQTT_H_
