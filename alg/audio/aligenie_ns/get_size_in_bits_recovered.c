/* Recovered from libmit_rtos.a get_size_in_bits.o. */
#include "ns_c_recovered.h"

int16_t TSpl_GetSizeInBits(uint32_t value)
{
    int bits = 0;

    if (value & UINT32_C(0xffff0000))
        bits = 16;
    if ((value >> bits & UINT32_C(0xff00)) != 0)
        bits += 8;
    if ((value >> bits & UINT32_C(0x00f0)) != 0)
        bits += 4;
    if ((value >> bits & UINT32_C(0x000c)) != 0)
        bits += 2;
    if ((value >> bits & UINT32_C(0x0002)) != 0)
        ++bits;
    if ((value >> bits & UINT32_C(0x0001)) != 0)
        ++bits;
    return (int16_t)bits;
}
