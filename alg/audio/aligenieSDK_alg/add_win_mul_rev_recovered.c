#include "aec_frontend_recovered.h"

void FeOpt_AddWinMulRev(FP16IfEn *pDst, float *pSrc,
                        const float *pWin, int len)
{
    int len_1 = len - 1;
    int pcmIdx;

    for (pcmIdx = 0; pcmIdx < len; ++pcmIdx)
        pDst[pcmIdx] = (FP16IfEn)(pSrc[pcmIdx] *
                                  pWin[len_1 - pcmIdx]);
}
