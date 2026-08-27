/* Recovered from libmit_rtos.a division_operations.o. */
#include "ns_c_recovered.h"

#include <limits.h>

uint32_t TSpl_DivU32U16(uint32_t numerator, uint16_t denominator)
{
    if (denominator == 0)
        return UINT32_MAX;
    return numerator / denominator;
}

int32_t TSpl_DivW32W16(int32_t numerator, int16_t denominator)
{
    if (denominator == 0)
        return INT32_MAX;
    return numerator / denominator;
}

int16_t TSpl_DivW32W16ResW16(int32_t numerator, int16_t denominator)
{
    if (denominator == 0)
        return INT16_MAX;
    return (int16_t)(numerator / denominator);
}
