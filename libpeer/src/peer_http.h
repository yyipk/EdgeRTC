#ifndef PEER_HTTP_H_
#define PEER_HTTP_H_

#include <stddef.h>
#include <stdint.h>

int peer_http_post(int secure,
                   const char* host,
                   uint16_t port,
                   const char* path,
                   const char* authorization,
                   const char* body,
                   uint8_t* response_body,
                   size_t response_body_size,
                   uint16_t* status_code);

#endif  // PEER_HTTP_H_
