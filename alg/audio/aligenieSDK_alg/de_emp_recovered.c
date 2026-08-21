#include "aec_frontend_recovered.h"

void FeOpt_DeEmp(short *pDst, FP16IfEn *pSrc, float *pBkVal,
                 float coefVal, int len)
{
    float bkVal = *pBkVal;
    int pcmIdx;

    for (pcmIdx = 0; pcmIdx < len; ++pcmIdx) {
        float pcmVal = (float)pSrc[pcmIdx] + bkVal * coefVal;
        bkVal = pcmVal;
        pcmVal *= 32768.0f;
        if (pcmVal < 0.0f)
            pcmVal -= 0.5f;
        else
            pcmVal += 0.5f;
        if (pcmVal < -32768.0f)
            pcmVal = -32768.0f;
        else if (pcmVal > 32767.0f)
            pcmVal = 32767.0f;
        pDst[pcmIdx] = (short)pcmVal;
    }
    *pBkVal = bkVal;
}
