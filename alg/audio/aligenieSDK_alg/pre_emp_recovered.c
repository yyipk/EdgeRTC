#include "aec_frontend_recovered.h"

void FeOpt_PreEmp(FP16IfEn *pDst, float *pSrc, float *pBkVal,
                  float coefVal, int len)
{
    float bkVal = *pBkVal;
    int pcmIdx;

    for (pcmIdx = 0; pcmIdx < len; ++pcmIdx) {
        float pcmVal = pSrc[pcmIdx];
        pDst[pcmIdx] = (FP16IfEn)(pcmVal - bkVal * coefVal);
        bkVal = pcmVal;
    }
    *pBkVal = bkVal;
}
