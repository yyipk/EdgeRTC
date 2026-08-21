#include "aec_recovered.h"
#if defined(__riscv) && defined(__riscv_fsqrt)
#define DR_SQRT(dst, src) __asm__ volatile ("fsqrt.s %0,%1" : "=f"(dst) : "f"(src))
#else
#include <math.h>
#define DR_SQRT(dst, src) ((dst) = sqrtf(src))
#endif
float NlsFe_DrGivRot( FP16IfEn *pIu, const CpxFloat_T *pVa, CpxFloat_T *pVu, const int filterLen, const float factor)
{
    float va0 = 1.0f;
    float va0Sq = 1.0f;
    int       i, j;


    for(i=0;i<filterLen;i++)
    {
        pVu[i].r = 0.0f;
        pVu[i].i = 0.0f;
    }

    for(i=0;i<filterLen;i++)
    {
        CpxFloat_T      sinTheta;
        float           cosTheta;
        float           invVa0;
        float           iu_r;
        float           iu_i;

        cosTheta = va0;
        va0Sq += pVa[i].r * pVa[i].r + pVa[i].i * pVa[i].i;



        DR_SQRT(va0, va0Sq);



        invVa0 = 1.0f / va0;
        cosTheta *= invVa0;
        sinTheta.r = pVa[i].r * invVa0;
        sinTheta.i = -pVa[i].i * invVa0;
        for(j=0;j<i;j++)
        {
            CpxFloat_T tmpVu = pVu[j];
            CpxFloat_T tmpc;

            iu_r = AEC_FP16_VALUE(pIu[0]);
            iu_i = AEC_FP16_VALUE(pIu[1]);
            pIu += 2;
            iu_r *= factor;
            iu_i *= factor;
            tmpc.r = sinTheta.r * iu_r - sinTheta.i * iu_i +
                     cosTheta * tmpVu.r;
            tmpc.i = sinTheta.r * iu_i + sinTheta.i * iu_r +
                     cosTheta * tmpVu.i;
            pVu[j].r = tmpc.r;
            pVu[j].i = tmpc.i;
            tmpc.r = sinTheta.r * tmpVu.r + sinTheta.i * tmpVu.i;
            tmpc.i = sinTheta.r * tmpVu.i - sinTheta.i * tmpVu.r;
            pIu[-2] = AEC_FP16_FROM_FLOAT(cosTheta * iu_r - tmpc.r);
            pIu[-1] = AEC_FP16_FROM_FLOAT(cosTheta * iu_i - tmpc.i);
        }
        {
            CpxFloat_T tmpVu = pVu[j];
            CpxFloat_T tmpc;

            iu_r = AEC_FP16_VALUE(*pIu) * factor;
            tmpc.r = cosTheta * tmpVu.r + sinTheta.r * iu_r;
            tmpc.i = cosTheta * tmpVu.i + sinTheta.i * iu_r;
            pVu[j].r = tmpc.r;
            pVu[j].i = tmpc.i;
            *pIu++ = AEC_FP16_FROM_FLOAT(cosTheta * iu_r -
                                         sinTheta.r * tmpVu.r -
                                         sinTheta.i * tmpVu.i);
        }
    }
    return va0;
}
