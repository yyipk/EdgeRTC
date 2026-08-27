/* Recovered from libmit_rtos.a add_sat_w16.o. */
#include "ns_c_recovered.h"

#include <limits.h>

int16_t TSpl_AddSatW16(int16_t left, int16_t right)
{
    int32_t sum = (int32_t)left + right;

    sum = sum < -32768 ? -32768 : sum;
    sum = sum >= 32768 ? 32767 : sum;
    return (int16_t)sum;
}
