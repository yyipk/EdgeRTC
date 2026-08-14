#ifndef DISABLE_PEER_SIGNALING
#include "peer_http.h"

#include <string.h>

#include <core_http_client.h>

#include "ssl_transport.h"
#include "utils.h"

int peer_http_post(int secure,
                   const char* host,
                   uint16_t port,
                   const char* path,
                   const char* authorization,
                   const char* body,
                   uint8_t* response_body,
                   size_t response_body_size,
                   uint16_t* status_code) {
  TransportInterface_t transport = {0};
  NetworkContext_t network;
  HTTPRequestInfo_t request_info = {0};
  HTTPRequestHeaders_t request_headers = {0};
  HTTPResponse_t response = {0};
  HTTPStatus_t status;

  (void)secure;
  if (host == NULL || port == 0 || path == NULL || body == NULL ||
      response_body == NULL || response_body_size == 0 || status_code == NULL) {
    return -1;
  }

  transport.recv = ssl_transport_recv;
  transport.send = ssl_transport_send;
  transport.pNetworkContext = &network;
  if (ssl_transport_connect(&network, host, port, NULL) < 0) {
    LOGE("Failed to connect to %s:%u", host, port);
    return -1;
  }

  request_info.pMethod = "POST";
  request_info.methodLen = 4;
  request_info.pHost = host;
  request_info.hostLen = strlen(host);
  request_info.pPath = path;
  request_info.pathLen = strlen(path);
  request_info.reqFlags = HTTP_REQUEST_KEEP_ALIVE_FLAG;
  request_headers.pBuffer = response_body;
  request_headers.bufferLen = response_body_size;

  status = HTTPClient_InitializeRequestHeaders(&request_headers, &request_info);
  if (status == HTTPSuccess) {
    status = HTTPClient_AddHeader(&request_headers,
                                  "Content-Type", strlen("Content-Type"),
                                  "application/sdp", strlen("application/sdp"));
  }
  if (status == HTTPSuccess && authorization != NULL && authorization[0] != '\0') {
    status = HTTPClient_AddHeader(&request_headers,
                                  "Authorization", strlen("Authorization"),
                                  authorization, strlen(authorization));
  }
  if (status == HTTPSuccess) {
    response.pBuffer = response_body;
    response.bufferLen = response_body_size;
    status = HTTPClient_Send(&transport, &request_headers,
                             (uint8_t*)body, strlen(body), &response, 0);
  }
  ssl_transport_disconnect(&network);

  if (status != HTTPSuccess) {
    LOGE("HTTP POST failed: Error=%s.", HTTPClient_strerror(status));
    return -1;
  }
  if (response.pHeaders == NULL || response.pBody == NULL ||
      response.bodyLen >= response_body_size) {
    LOGE("Invalid HTTP response");
    return -1;
  }

  *status_code = response.statusCode;
  memmove(response_body, response.pBody, response.bodyLen);
  response_body[response.bodyLen] = '\0';
  return 0;
}
#endif  // DISABLE_PEER_SIGNALING
