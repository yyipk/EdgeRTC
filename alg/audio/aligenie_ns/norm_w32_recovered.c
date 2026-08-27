/* Recovered from libmit_rtos.a norm_w32.o. */
#include "ns_c_recovered.h"

int TSpl_NormW32(int32_t value)
{
    int zeros = 0;

    if (value <= 0)
        value = ~value;
    if ((value & INT32_C(0xffff8000)) == 0)
        zeros = 16;
    if ((value << zeros & INT32_C(0xff800000)) == 0)
        zeros += 8;
    if ((value << zeros & INT32_C(0xf8000000)) == 0)
        zeros += 4;
    if ((value << zeros & INT32_C(0xe0000000)) == 0)
        zeros += 2;
    if ((value << zeros & INT32_C(0xc0000000)) == 0)
        ++zeros;
    return zeros;
}
