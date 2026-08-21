#include "aec_recovered.h"





void NlsFe_DrAecUpdatePath( CpxFloat_T *pPath, const CpxFloat_T *pVu, const CpxFloat_T *pData, const int chNum, const int filterLen, const int freqBinNum, const float factor, const float aecAmp)
{
    int chIdx;


    for (chIdx = 0; chIdx < chNum; chIdx++, pData += freqBinNum)
    {
        CpxFloat_T tmpData = *pData;
        int i;
        for (i = 0; i < filterLen; i++)
        {
            CpxFloat_T tmpc;
            tmpc.r = tmpData.r * pVu[i].r - tmpData.i * pVu[i].i;
            tmpc.i = tmpData.r * pVu[i].i + tmpData.i * pVu[i].r;
            tmpc.r *= aecAmp;
            tmpc.i *= aecAmp;
            pPath->r -= tmpc.r * factor;
            pPath->i -= tmpc.i * factor;
            ++pPath;
        }
    }
}
