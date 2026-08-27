/* Recovered from libmit_rtos.a min_max_operations.o. */
#include "ns_c_recovered.h"

#include <limits.h>

int16_t TSpl_MaxAbsValueW16(const int16_t *vector, int16_t length)
{
    int32_t tempMax = 0;
    int32_t absVal;
    int16_t totMax;
    const int16_t *tmpvector = vector;
    int i;

    for (i = 0; i < length; ++i) {
        absVal = tmpvector[i];

        if (absVal < 0)
            absVal = -absVal;
        if (absVal > tempMax)
            tempMax = absVal;
    }
    if (INT16_MAX - 1 >= tempMax)
        totMax = (int16_t)tempMax;
    else
        totMax = INT16_MAX;
    return totMax;
}

int32_t TSpl_MaxAbsValueW32(const int32_t *vector, int16_t length)
{
    int i;
    uint32_t maximum = 0;

    for (i = 0; i < length; ++i) {
        int32_t value = vector[i];
        uint32_t magnitude = value < 0 ? (uint32_t)-value : (uint32_t)value;

        if (magnitude > maximum)
            maximum = magnitude;
    }
    return (int32_t)maximum;
}

int16_t TSpl_MaxValueW16(const int16_t *vector, int16_t length)
{
    int16_t tempMax = vector[0];
    int16_t i;
    const int16_t *tmpvector = vector;

    for (i = 1; i < length; ++i) {
        if (tmpvector[i] > tempMax)
            tempMax = tmpvector[i];
    }
    return tempMax;
}
