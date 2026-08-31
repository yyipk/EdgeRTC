#ifndef DISABLE_PEER_SIGNALING
#include "peer_http.h"

#include <stdio.h>
#include <string.h>

#include <https_client.h>

#include "config.h"
#include "utils.h"

typedef struct PeerHttpResponse {
  uint8_t* body;
  size_t body_size;
  size_t body_len;
  uint16_t status_code;
  int overflow;
} PeerHttpResponse;

static void peer_http_response_callback(struct http_response* response,
                                        enum http_final_call final_data,
                                        void* user_data) {
  PeerHttpResponse* result = user_data;
  size_t available;

  (void)final_data;
  result->status_code = response->http_status_code;
  if (response->body_frag_start == NULL || response->body_frag_len == 0 ||
      result->overflow) {
    return;
  }

  available = result->body_size - result->body_len;
  if (response->body_frag_len >= available) {
    result->overflow = 1;
    return;
  }
  memcpy(result->body + result->body_len,
         response->body_frag_start, response->body_frag_len);
  result->body_len += response->body_frag_len;
  result->body[result->body_len] = '\0';
}

int peer_http_post(int secure,
                   const char* host,
                   uint16_t port,
                   const char* path,
                   const char* authorization,
                   const char* body,
                   uint8_t* response_body,
                   size_t response_body_size,
                   uint16_t* status_code) {
  char url[256];
  char authorization_header[320];
  const char* optional_headers[2] = {NULL, NULL};
  struct https_client_request request = {0};
  PeerHttpResponse response = {
      .body = response_body,
      .body_size = response_body_size,
  };
  int ret;

  if (host == NULL || path == NULL || body == NULL || response_body == NULL ||
      response_body_size == 0 || status_code == NULL ||
      snprintf(url, sizeof(url), "%s://%s:%u%s",
               secure ? "https" : "http", host, port, path) >=
          (int)sizeof(url)) {
    return -1;
  }

  response_body[0] = '\0';
  if (authorization != NULL && authorization[0] != '\0') {
    if (snprintf(authorization_header, sizeof(authorization_header),
                 "Authorization: %s\r\n", authorization) >=
        (int)sizeof(authorization_header)) {
      return -1;
    }
    optional_headers[0] = authorization_header;
  }

  request.method = HTTP_POST;
  request.response = peer_http_response_callback;
  request.url = url;
  request.protocol = "HTTP/1.1";
  request.content_type_value = "application/sdp";
  request.payload = body;
  request.payload_len = strlen(body);
  request.optional_headers =
      optional_headers[0] != NULL ? optional_headers : NULL;

  ret = https_client_request(&request, CONFIG_TLS_READ_TIMEOUT, &response);
  if (ret < 0 || response.overflow) {
    LOGE("External HTTP POST failed: ret=%d overflow=%d", ret, response.overflow);
    return -1;
  }
  *status_code = response.status_code;
  return 0;
}
#endif  // DISABLE_PEER_SIGNALING
