/* Recovered from libmit_rtos.a norm_u32.o. */
#include "ns_c_recovered.h"

int TSpl_NormU32(uint32_t value)
{
    int zeros = 0;

    if (value == 0)
        return 0;
    if ((value & UINT32_C(0xffff0000)) == 0)
        zeros = 16;
    if ((value << zeros & UINT32_C(0xff000000)) == 0)
        zeros += 8;
    if ((value << zeros & UINT32_C(0xf0000000)) == 0)
        zeros += 4;
    if ((value << zeros & UINT32_C(0xc0000000)) == 0)
        zeros += 2;
    if ((value << zeros & UINT32_C(0x80000000)) == 0)
        ++zeros;
    return zeros;
}
