#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "nat_pmp.h"

int main(void) {
  const uint8_t external_address_response[] = {
      0x00, 0x80, 0x00, 0x00, 0x00, 0x11,
      0x4c, 0x4b, 0xc0, 0xa8, 0x1f, 0x7f,
  };
  const uint8_t mapping_response[] = {
      0x00, 0x81, 0x00, 0x00, 0x00, 0x11, 0x4c, 0x4b,
      0x9c, 0x40, 0x9c, 0x41, 0x00, 0x00, 0x1c, 0x20,
  };
  const uint8_t expected_mapping_request[] = {
      0x00, 0x01, 0x00, 0x00, 0x9c, 0x40,
      0x9c, 0x41, 0x00, 0x00, 0x1c, 0x20,
  };
  const uint8_t expected_external_address_request[] = {0x00, 0x00};
  uint8_t request[NAT_PMP_UDP_MAPPING_REQUEST_SIZE];
  uint32_t address;
  uint32_t lifetime;
  uint16_t external_port;

  nat_pmp_write_external_address_request(request);
  assert(memcmp(request, expected_external_address_request,
                sizeof(expected_external_address_request)) == 0);
  assert(nat_pmp_read_external_address_response(
             external_address_response, sizeof(external_address_response),
             &address) == 0);
  assert(memcmp(&address, external_address_response + 8,
                sizeof(address)) == 0);
  assert(nat_pmp_read_udp_mapping_response(
             mapping_response, sizeof(mapping_response), 40000,
             &external_port, &lifetime) == 0);
  assert(external_port == 40001);
  assert(lifetime == 7200);

  nat_pmp_write_udp_mapping_request(request, 40000, 40001, 7200);
  assert(memcmp(request, expected_mapping_request, sizeof(request)) == 0);
  nat_pmp_write_udp_mapping_request(request, 40000, 0, 0);
  assert(request[6] == 0 && request[7] == 0);
  assert(request[8] == 0 && request[9] == 0 &&
         request[10] == 0 && request[11] == 0);
  assert(nat_pmp_read_udp_mapping_response(
             mapping_response, sizeof(mapping_response), 40002,
             &external_port, &lifetime) < 0);
  return 0;
}
