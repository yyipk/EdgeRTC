#ifndef WEBRTC_LWIP_CHKSUM_COPY_H
#define WEBRTC_LWIP_CHKSUM_COPY_H

#include <stdint.h>

uint16_t msp_lwip_chksum_copy(void *destination, const void *source,
                              uint16_t length);

#endif
