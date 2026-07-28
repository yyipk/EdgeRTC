#ifndef PEER_SIGNALING_H_
#define PEER_SIGNALING_H_

#include <stddef.h>

#include "peer_connection.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DISABLE_PEER_SIGNALING

#define PEER_SIGNALING_DEVICE_ID_LENGTH 12
#define PEER_SIGNALING_DEVICE_ID_SIZE   (PEER_SIGNALING_DEVICE_ID_LENGTH + 1)

int peer_signaling_connect(const char* url, const char* token, PeerConnection* pc);

void peer_signaling_disconnect();

int peer_signaling_loop();

int peer_signaling_call(const char* device_id);

int peer_signaling_hangup();

int peer_signaling_get_device_id(char* device_id, size_t size);

#endif  // DISABLE_PEER_SIGNALING

#ifdef __cplusplus
}
#endif

#endif  // PEER_SIGNALING_H_
