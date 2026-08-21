#include "aec_recovered.h"

void NlsFe_DrAecSetNoiseBuf(CpxFp16_T *pInBuf, const int micPos,
                            const int refPos, const int fiOffset,
                            const int fiLen, const int drDelay,
                            CpxFloat_T *pDelaynUpdate, const int micTaps,
                            const int refTaps,
                            const float *pTmpRandn)
{
    int micBufLen = micTaps + drDelay;
    int dataBinBufLen = micBufLen + refTaps;
    int micStartPos = 0;
    int freqIdx;

    if (micPos < micTaps)
        micStartPos = drDelay + micPos;

    for (freqIdx = fiOffset; freqIdx < fiOffset + fiLen; ++freqIdx) {
        CpxFp16_T *pTmpIn = pInBuf + freqIdx * dataBinBufLen;
        CpxFp16_T *pTmpInRef;
        const float *pRandn = pTmpRandn;
        int i, k;

        for (i = 0, k = 0;
             i + micStartPos < micBufLen && k < micTaps; ++i, ++k) {
            float randnVal = *pRandn++;
            pDelaynUpdate->r =
                AEC_FP16_VALUE(pTmpIn[i + micStartPos].r) + randnVal;
            pDelaynUpdate->i =
                AEC_FP16_VALUE(pTmpIn[i + micStartPos].i);
            ++pDelaynUpdate;
        }
        for (i = 0; i < micPos && k < micTaps; ++i, ++k) {
            float randnVal = *pRandn++;
            pDelaynUpdate->r = AEC_FP16_VALUE(pTmpIn[i].r) + randnVal;
            pDelaynUpdate->i = AEC_FP16_VALUE(pTmpIn[i].i);
            ++pDelaynUpdate;
        }
        pTmpInRef = pTmpIn + micBufLen;
        for (i = refPos; i < refTaps; ++i) {
            float randnVal = *pRandn++;
            pDelaynUpdate->r = AEC_FP16_VALUE(pTmpInRef[i].r) + randnVal;
            pDelaynUpdate->i = AEC_FP16_VALUE(pTmpInRef[i].i);
            ++pDelaynUpdate;
        }
        for (i = 0; i < refPos; ++i) {
            float randnVal = *pRandn++;
            pDelaynUpdate->r =
                AEC_FP16_VALUE(pTmpInRef[i].r) + randnVal;
            pDelaynUpdate->i = AEC_FP16_VALUE(pTmpInRef[i].i);
            ++pDelaynUpdate;
        }
    }
}
