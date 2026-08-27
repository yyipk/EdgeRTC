/* Recovered from libmit_rtos.a norm_w16.o. */
#include "ns_c_recovered.h"

int TSpl_NormW16(int16_t value)
{
    int zeros = 0;

    if (value <= 0)
        value = (int16_t)~value;
    if ((value & 0xff80) == 0)
        zeros = 8;
    if ((value << zeros & 0xf800) == 0)
        zeros += 4;
    if ((value << zeros & 0xe000) == 0)
        zeros += 2;
    if ((value << zeros & 0xc000) == 0)
        ++zeros;
    return zeros;
}
