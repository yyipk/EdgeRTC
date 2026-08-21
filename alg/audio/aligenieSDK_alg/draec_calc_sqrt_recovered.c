#include "aec_recovered.h"

float NlsFe_DrAecCalcSqrt(CpxFp16_T *in, CpxFloat_T *data,
                          int freqBinNum, int chNum, float aecAmpInv)
{
    float sqrtPhi = 0.0f;
    int chIdx;

    for (chIdx = 0; chIdx < chNum;) {
        float xsq = CpxFp16_value(in->r);
        float ysq = CpxFp16_value(in->i);

        xsq = xsq * xsq + ysq * ysq;
        ysq = (data->r * data->r + data->i * data->i) * aecAmpInv;
        data += freqBinNum;
        if (ysq <= xsq) xsq = ysq;
        ++chIdx;
        sqrtPhi += xsq;
        ++in;
    }
    return sqrtPhi;
}
