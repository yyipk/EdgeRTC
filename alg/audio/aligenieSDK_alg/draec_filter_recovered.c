#include "aec_recovered.h"

void NlsFe_DrAecFilter(CpxFp16_T *pInBuf, const int micPos,
                       const int refPos, CpxFloat_T *pData,
                       const int micNum, const int refNum,
                       const int freqBinNum, CpxFloat_T *pDrAecPath,
                       const int micTaps, const int refTaps,
                       const int drDelay, const float aecAmp)
{
    int micBufLen = micTaps + drDelay;
    int dataBinBufLen = micBufLen + refTaps;
    int pathBinBufLen = (micTaps + refTaps) * micNum;
    int micStartPos = 0;
    int freqIdx;

    if (micPos < micTaps)
        micStartPos = drDelay + micPos;

    for (freqIdx = 0; freqIdx < freqBinNum; ++freqIdx) {
        CpxFloat_T *pTmpPath;
        CpxFloat_T *pTmpData;
        CpxFp16_T *pTmpIn;
        int chIdx;

        pTmpData = pData + freqIdx;
        pTmpIn = pInBuf + micPos;
        for (chIdx = 0; chIdx < micNum; ++chIdx) {
            pTmpIn->r = AEC_FP16_FROM_FLOAT(pTmpData->r);
            pTmpIn->i = AEC_FP16_FROM_FLOAT(pTmpData->i);
            ++pTmpIn;
            pTmpData += freqBinNum;
        }
        pTmpIn = pInBuf + micBufLen + refPos;
        for (chIdx = 0; chIdx < refNum; ++chIdx) {
            pTmpIn->r = AEC_FP16_FROM_FLOAT(pTmpData->r);
            pTmpIn->i = AEC_FP16_FROM_FLOAT(pTmpData->i);
            ++pTmpIn;
            pTmpData += freqBinNum;
        }
        pTmpData = pData + freqIdx;
        pTmpPath = pDrAecPath + freqIdx * pathBinBufLen;

        for (chIdx = 0; chIdx < micNum; ++chIdx) {
            CpxFloat_T reverbEchoVal = { 0.0f, 0.0f };
            CpxFp16_T *pInRefBuf = pInBuf + micBufLen;
            int i, k;

            for (i = 0, k = 0;
                i + micStartPos < micBufLen && k < micTaps; ++i, ++k) {
                CpxFloat_T d, w;
                d.r = AEC_FP16_VALUE(pInBuf[i + micStartPos].r);
                d.i = AEC_FP16_VALUE(pInBuf[i + micStartPos].i);
                w = *pTmpPath++;
                reverbEchoVal.r += d.r * w.r - d.i * w.i;
                reverbEchoVal.i += d.r * w.i + d.i * w.r;
            }
            for (i = 0; i < micPos && k < micTaps; ++i, ++k) {
                CpxFloat_T d, w;
                d.r = AEC_FP16_VALUE(pInBuf[i].r);
                d.i = AEC_FP16_VALUE(pInBuf[i].i);
                w = *pTmpPath++;
                reverbEchoVal.r += d.r * w.r - d.i * w.i;
                reverbEchoVal.i += d.r * w.i + d.i * w.r;
            }
            for (i = refPos, k = micTaps; i < refTaps; ++i, ++k) {
                CpxFloat_T d, w;
                d.r = AEC_FP16_VALUE(pInRefBuf[i].r);
                d.i = AEC_FP16_VALUE(pInRefBuf[i].i);
                w = *pTmpPath++;
                reverbEchoVal.r += d.r * w.r - d.i * w.i;
                reverbEchoVal.i += d.r * w.i + d.i * w.r;
            }
            for (i = 0; i < refPos; ++i) {
                CpxFloat_T d, w;
                d.r = AEC_FP16_VALUE(pInRefBuf[i].r);
                d.i = AEC_FP16_VALUE(pInRefBuf[i].i);
                w = *pTmpPath++;
                reverbEchoVal.r += d.r * w.r - d.i * w.i;
                reverbEchoVal.i += d.r * w.i + d.i * w.r;
            }
            pTmpData->r = (pTmpData->r - reverbEchoVal.r) * aecAmp;
            pTmpData->i = (pTmpData->i - reverbEchoVal.i) * aecAmp;
            pTmpData += freqBinNum;
        }
        pInBuf += dataBinBufLen;
    }
}
