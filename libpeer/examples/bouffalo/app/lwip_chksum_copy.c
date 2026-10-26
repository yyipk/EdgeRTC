#include "lwip_chksum_copy.h"

#include <stdint.h>

typedef uint16_t checksum_u16_alias_t __attribute__((may_alias));
typedef uint32_t checksum_u32_alias_t __attribute__((may_alias));

static inline uint32_t checksum_add_u32(uint32_t sum, uint32_t value)
{
    return sum + (value & UINT32_C(0xffff)) + (value >> 16);
}

uint16_t msp_lwip_chksum_copy(void *destination, const void *source,
                              uint16_t length)
{
    uint8_t *dst = destination;
    const uint8_t *src = source;
    uint32_t sum = 0;

    if ((((uintptr_t)dst | (uintptr_t)src) & 3U) == 0U) {
        while (length >= 16U) {
            const checksum_u32_alias_t *src32 =
                (const checksum_u32_alias_t *)(const void *)src;
            checksum_u32_alias_t *dst32 =
                (checksum_u32_alias_t *)(void *)dst;
            uint32_t word0 = src32[0];
            uint32_t word1 = src32[1];
            uint32_t word2 = src32[2];
            uint32_t word3 = src32[3];

            dst32[0] = word0;
            dst32[1] = word1;
            dst32[2] = word2;
            dst32[3] = word3;
            sum = checksum_add_u32(sum, word0);
            sum = checksum_add_u32(sum, word1);
            sum = checksum_add_u32(sum, word2);
            sum = checksum_add_u32(sum, word3);
            src += 16;
            dst += 16;
            length = (uint16_t)(length - 16U);
        }
        while (length >= 4U) {
            uint32_t word =
                *(const checksum_u32_alias_t *)(const void *)src;

            *(checksum_u32_alias_t *)(void *)dst = word;
            sum = checksum_add_u32(sum, word);
            src += 4;
            dst += 4;
            length = (uint16_t)(length - 4U);
        }
    }

    if ((((uintptr_t)dst | (uintptr_t)src) & 1U) == 0U) {
        while (length >= 2U) {
            uint16_t word =
                *(const checksum_u16_alias_t *)(const void *)src;

            *(checksum_u16_alias_t *)(void *)dst = word;
            sum += word;
            src += 2;
            dst += 2;
            length = (uint16_t)(length - 2U);
        }
    } else {
        while (length >= 2U) {
            uint16_t word;
            uint8_t *word_bytes = (uint8_t *)(void *)&word;

            word_bytes[0] = src[0];
            word_bytes[1] = src[1];
            dst[0] = src[0];
            dst[1] = src[1];
            sum += word;
            src += 2;
            dst += 2;
            length = (uint16_t)(length - 2U);
        }
    }

    if (length != 0U) {
        uint16_t tail = 0;

        *(uint8_t *)(void *)&tail = *src;
        *dst = *src;
        sum += tail;
    }

    sum = (sum & UINT32_C(0xffff)) + (sum >> 16);
    sum = (sum & UINT32_C(0xffff)) + (sum >> 16);
    return (uint16_t)sum;
}
