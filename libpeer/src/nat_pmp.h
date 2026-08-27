#ifndef NAT_PMP_H_
#define NAT_PMP_H_

#include <stdint.h>

#include "address.h"

#define NAT_PMP_PORT 5351
#define NAT_PMP_EXTERNAL_ADDRESS_REQUEST_SIZE 2
#define NAT_PMP_EXTERNAL_ADDRESS_RESPONSE_SIZE 12
#define NAT_PMP_UDP_MAPPING_REQUEST_SIZE 12
#define NAT_PMP_UDP_MAPPING_RESPONSE_SIZE 16

void nat_pmp_write_external_address_request(uint8_t request[NAT_PMP_EXTERNAL_ADDRESS_REQUEST_SIZE]);

int nat_pmp_read_external_address_response(const uint8_t* response,
                                           int size,
                                           uint32_t* address);

void nat_pmp_write_udp_mapping_request(
    uint8_t request[NAT_PMP_UDP_MAPPING_REQUEST_SIZE],
    uint16_t internal_port, uint16_t external_port, uint32_t lifetime);

int nat_pmp_read_udp_mapping_response(const uint8_t* response, int size,
                                      uint16_t internal_port,
                                      uint16_t* external_port,
                                      uint32_t* lifetime);

int nat_pmp_map(const Address* gateway, uint16_t internal_port,
                uint16_t suggested_external_port, uint32_t lifetime,
                Address* external_addr, uint32_t* granted_lifetime);

#endif  // NAT_PMP_H_
