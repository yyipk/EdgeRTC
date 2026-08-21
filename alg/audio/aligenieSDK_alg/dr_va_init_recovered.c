#include "aec_recovered.h"

void NlsFe_DrVaInit(CpxFloat_T *pVa, const FP16IfEn_T *pIu,
                    const CpxFloat_T *pDelaynUpdate, int filterLen,
                    float factor)
{
    int i, j;

    for (i = 0; i < filterLen; ++i) {
        CpxFloat_T tmpc;
        float iu_r;
        float iu_i;

        tmpc.r = 0.0f;
        tmpc.i = 0.0f;

        for (j = 0; j < i; ++j) {
            iu_r = CpxFp16_value(*pIu++);
            iu_i = CpxFp16_value(*pIu++);

            tmpc.r += iu_r * pDelaynUpdate[j].r -
                      iu_i * pDelaynUpdate[j].i;
            tmpc.i += iu_r * pDelaynUpdate[j].i +
                      iu_i * pDelaynUpdate[j].r;
        }
        iu_r = CpxFp16_value(*pIu++);
        tmpc.r += iu_r * pDelaynUpdate[j].r;
        tmpc.i += iu_r * pDelaynUpdate[j].i;

        pVa[i].r = tmpc.r * factor;
        pVa[i].i = tmpc.i * factor;
    }
}
