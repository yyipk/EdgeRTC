#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stun.h"

int main(int argc, char* argv[]) {
  static uint8_t rfc5769_request[] = {
      0x00, 0x01, 0x00, 0x58, 0x21, 0x12, 0xa4, 0x42,
      0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86,
      0xfa, 0x87, 0xdf, 0xae, 0x80, 0x22, 0x00, 0x10,
      0x53, 0x54, 0x55, 0x4e, 0x20, 0x74, 0x65, 0x73,
      0x74, 0x20, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74,
      0x00, 0x24, 0x00, 0x04, 0x6e, 0x00, 0x01, 0xff,
      0x80, 0x29, 0x00, 0x08, 0x93, 0x2f, 0xf9, 0xb1,
      0x51, 0x26, 0x3b, 0x36, 0x00, 0x06, 0x00, 0x09,
      0x65, 0x76, 0x74, 0x6a, 0x3a, 0x68, 0x36, 0x76,
      0x59, 0x20, 0x20, 0x20, 0x00, 0x08, 0x00, 0x14,
      0x9a, 0xea, 0xa7, 0x0c, 0xbf, 0xd8, 0xcb, 0x56,
      0x78, 0x1e, 0xf2, 0xb5, 0xb2, 0xd3, 0xf2, 0x49,
      0xc1, 0xb5, 0x71, 0xa2, 0x80, 0x28, 0x00, 0x04,
      0xe5, 0x7a, 0x3b, 0xcf,
  };
  static const uint8_t payload[] = {0x80, 0x60, 0x12, 0x34};
  static const uint8_t short_ipv6_address[] = {
      0, STUN_FAMILY_IPV6, 0, 0, 0, 0, 0, 0};
  Address peer = {0};
  StunMessage message;
  StunMessage parsed = {0};

  assert(stun_msg_is_valid(rfc5769_request, sizeof(rfc5769_request),
                           "VOkJxbRl1RmTxUk/WvJxBt") == 0);
  assert(addr_from_string("192.0.2.10", &peer) == 1);
  addr_set_port(&peer, 5004);
  stun_msg_create(&message, STUN_CLASS_INDICATION | STUN_METHOD_DATA);
  assert(stun_msg_write_xor_address(&message,
                                    STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                                    &peer) == 0);
  assert(stun_msg_write_attr(&message, STUN_ATTR_TYPE_DATA,
                             sizeof(payload), (char*)payload) == 0);

  memcpy(parsed.buf, message.buf, message.size);
  parsed.size = message.size;
  assert(stun_parse_msg_buf(&parsed) == 0);
  assert(parsed.stunclass == STUN_CLASS_INDICATION);
  assert(parsed.stunmethod == STUN_METHOD_DATA);
  assert(addr_equal(&parsed.peer_addr, &peer));
  assert(parsed.data_size == sizeof(payload));
  assert(memcmp(parsed.data, payload, sizeof(payload)) == 0);

  parsed.size--;
  assert(stun_parse_msg_buf(&parsed) < 0);
  parsed.size = message.size + 1;
  assert(stun_parse_msg_buf(&parsed) < 0);

  stun_msg_create(&message, STUN_CLASS_INDICATION | STUN_METHOD_DATA);
  assert(stun_msg_write_attr(&message, STUN_ATTR_TYPE_XOR_PEER_ADDRESS,
                             sizeof(short_ipv6_address),
                             (char*)short_ipv6_address) == 0);
  assert(stun_parse_msg_buf(&message) < 0);
  return 0;
}
