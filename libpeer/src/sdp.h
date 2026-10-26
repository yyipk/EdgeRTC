#ifndef SDP_H_
#define SDP_H_

#include <string.h>
#include "config.h"

#define SDP_ATTR_LENGTH 128
#define SDP_MAX_MEDIA_COUNT 3
#define SDP_MID_LENGTH 32

#ifndef ICE_LITE
#define ICE_LITE 0
#endif

typedef enum SdpMediaType {
  SDP_MEDIA_NONE = 0,
  SDP_MEDIA_VIDEO,
  SDP_MEDIA_AUDIO,
  SDP_MEDIA_DATACHANNEL,
} SdpMediaType;

typedef struct SdpMedia {
  SdpMediaType type;
  char mid[SDP_MID_LENGTH];
} SdpMedia;

void sdp_append_h264(char* sdp, const char* mid);

void sdp_append_pcma(char* sdp, const char* mid);

void sdp_append_pcmu(char* sdp, const char* mid);

void sdp_append_opus(char* sdp, const char* mid);

/* Advertise the native receiver's actual bounded message capacity. */
void sdp_append_datachannel(char* sdp, const char* mid, size_t max_message_size);

void sdp_create(char* sdp, const SdpMedia* media, size_t media_count);

int sdp_append(char* sdp, const char* format, ...);

void sdp_reset(char* sdp);

#endif  // SDP_H_
