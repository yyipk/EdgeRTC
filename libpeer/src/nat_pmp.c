#include <string.h>
#include <sys/select.h>

#include "nat_pmp.h"
#include "socket.h"

#define NAT_PMP_VERSION 0
#define NAT_PMP_OPCODE_EXTERNAL_ADDRESS 0
#define NAT_PMP_OPCODE_UDP_MAPPING 1
#define NAT_PMP_RESPONSE_FLAG 0x80
#define NAT_PMP_RETRIES 3
#define NAT_PMP_TIMEOUT_MS 250

static void nat_pmp_write_u16(uint8_t* buffer, uint16_t value) {
  value = htons(value);
  memcpy(buffer, &value, sizeof(value));
}

static void nat_pmp_write_u32(uint8_t* buffer, uint32_t value) {
  value = htonl(value);
  memcpy(buffer, &value, sizeof(value));
}

static uint16_t nat_pmp_read_u16(const uint8_t* buffer) {
  uint16_t value;

  memcpy(&value, buffer, sizeof(value));
  return ntohs(value);
}

static uint32_t nat_pmp_read_u32(const uint8_t* buffer) {
  uint32_t value;

  memcpy(&value, buffer, sizeof(value));
  return ntohl(value);
}

void nat_pmp_write_external_address_request(uint8_t request[NAT_PMP_EXTERNAL_ADDRESS_REQUEST_SIZE]) {
  request[0] = NAT_PMP_VERSION;
  request[1] = NAT_PMP_OPCODE_EXTERNAL_ADDRESS;
}

int nat_pmp_read_external_address_response(const uint8_t* response,
                                           int size,
                                           uint32_t* address) {
  if (size < NAT_PMP_EXTERNAL_ADDRESS_RESPONSE_SIZE ||
      response[0] != NAT_PMP_VERSION ||
      response[1] !=
          (NAT_PMP_RESPONSE_FLAG | NAT_PMP_OPCODE_EXTERNAL_ADDRESS) ||
      nat_pmp_read_u16(response + 2) != 0) {
    return -1;
  }

  memcpy(address, response + 8, sizeof(*address));
  return 0;
}

void nat_pmp_write_udp_mapping_request(
    uint8_t request[NAT_PMP_UDP_MAPPING_REQUEST_SIZE],
    uint16_t internal_port, uint16_t external_port, uint32_t lifetime) {
  memset(request, 0, NAT_PMP_UDP_MAPPING_REQUEST_SIZE);
  request[0] = NAT_PMP_VERSION;
  request[1] = NAT_PMP_OPCODE_UDP_MAPPING;
  nat_pmp_write_u16(request + 4, internal_port);
  nat_pmp_write_u16(request + 6, external_port);
  nat_pmp_write_u32(request + 8, lifetime);
}

int nat_pmp_read_udp_mapping_response(const uint8_t* response, int size,
                                      uint16_t internal_port,
                                      uint16_t* external_port,
                                      uint32_t* lifetime) {
  if (size < NAT_PMP_UDP_MAPPING_RESPONSE_SIZE ||
      response[0] != NAT_PMP_VERSION ||
      response[1] != (NAT_PMP_RESPONSE_FLAG | NAT_PMP_OPCODE_UDP_MAPPING) ||
      nat_pmp_read_u16(response + 2) != 0 ||
      nat_pmp_read_u16(response + 8) != internal_port) {
    return -1;
  }

  *external_port = nat_pmp_read_u16(response + 10);
  *lifetime = nat_pmp_read_u32(response + 12);
  return 0;
}

static int nat_pmp_exchange(UdpSocket* socket, const Address* gateway,
                            const uint8_t* request, int request_size,
                            uint8_t response_opcode, uint8_t* response,
                            int response_size) {
  Address server = *gateway;
  Address source;
  fd_set read_fds;
  struct timeval timeout;
  int size;
  int i;

  for (i = 0; i < NAT_PMP_RETRIES; i++) {
    if (udp_socket_sendto(socket, &server, request, request_size) < 0) {
      return -1;
    }

    FD_ZERO(&read_fds);
    FD_SET(socket->fd, &read_fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = NAT_PMP_TIMEOUT_MS * 1000;
    if (select(socket->fd + 1, &read_fds, NULL, NULL, &timeout) <= 0) {
      continue;
    }

    size = udp_socket_recvfrom(socket, &source, response, response_size);
    if (size >= 2 && addr_equal(&source, &server) &&
        response[0] == NAT_PMP_VERSION &&
        response[1] == (NAT_PMP_RESPONSE_FLAG | response_opcode)) {
      return size;
    }
  }
  return -1;
}

int nat_pmp_map(const Address* gateway, uint16_t internal_port,
                uint16_t suggested_external_port, uint32_t lifetime,
                Address* external_addr, uint32_t* granted_lifetime) {
  uint8_t request[NAT_PMP_UDP_MAPPING_REQUEST_SIZE];
  uint8_t response[NAT_PMP_UDP_MAPPING_RESPONSE_SIZE];
  uint32_t external_address;
  uint16_t external_port;
  UdpSocket socket = {.fd = -1};
  int size;
  int ret = -1;

  if (udp_socket_open(&socket, AF_INET, 0) < 0) {
    return -1;
  }

  if (lifetime != 0) {
    nat_pmp_write_external_address_request(request);
    size = nat_pmp_exchange(&socket, gateway, request,
                            NAT_PMP_EXTERNAL_ADDRESS_REQUEST_SIZE,
                            NAT_PMP_OPCODE_EXTERNAL_ADDRESS, response,
                            sizeof(response));
    if (nat_pmp_read_external_address_response(response, size,
                                               &external_address) < 0) {
      goto exit;
    }
  }

  nat_pmp_write_udp_mapping_request(request, internal_port,
                                    suggested_external_port, lifetime);
  size = nat_pmp_exchange(&socket, gateway, request,
                          NAT_PMP_UDP_MAPPING_REQUEST_SIZE,
                          NAT_PMP_OPCODE_UDP_MAPPING, response,
                          sizeof(response));
  if (nat_pmp_read_udp_mapping_response(response, size, internal_port,
                                        &external_port,
                                        granted_lifetime) < 0) {
    goto exit;
  }

  if (lifetime == 0) {
    ret = 0;
    goto exit;
  }

  memset(external_addr, 0, sizeof(*external_addr));
  addr_set_family(external_addr, AF_INET);
  memcpy(&external_addr->sin.sin_addr, &external_address,
         sizeof(external_addr->sin.sin_addr));
  addr_set_port(external_addr, external_port);
  ret = 0;

exit:
  udp_socket_close(&socket);
  return ret;
}
