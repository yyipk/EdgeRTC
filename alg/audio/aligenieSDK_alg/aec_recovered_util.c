#include "aec_recovered.h"
#include <math.h>

float Util_db2pow(float db)
{
    return powf(10.0f, db / 10.0f);
}

float Util_randn(int *seed)
{
    float value = 0.0f;
    int i;

    for (i = 0; i < 12; ++i) {
        *seed = (2045 * *seed + 1) % 1048576;
        value += (float)*seed * (1.0f / 1048576.0f);
    }
    return value - 6.0f;
}
