#include "aec_frontend_recovered.h"
#include <stdlib.h>

void FeOpt_RFFT640_GenTab_f32(void);
void FeOpt_PreEmp(FP16IfEn *pDst, float *pSrc, float *pBkVal,
                  float coefVal, int len);
void FeOpt_RFFT640_f32(float *pDst, float *pSrc);
float Util_round(float x);
extern const float tFeOpt_PFBA_Win320_1920_Delay960[];

RET_CODE PPFBankAnalyzer_init(int numchs, PPFA_WINDOW_TYPE wtype,
                              PPFBankAnalyzer **this1)
{
    PPFBankAnalyzer *ana = malloc(sizeof(*ana));
    int i;

    if (!ana)
        return RET_INSUFFICIENT_MEMORY;
    *ana = (PPFBankAnalyzer){0};
    ana->numchs = numchs;

    switch (wtype) {
    case WINDOW_PPFA_PROTO_160_960:
    case WINDOW_PPFA_PROTO_TIANBIAO_160_960:
        ana->blocksize = 160;
        ana->numbins = 160;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1280:
        ana->blocksize = 320;
        ana->numbins = 320;
        ana->numprotos = 4;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1920:
        ana->blocksize = 320;
        ana->numbins = 320;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1920_DELAY960:
        ana->blocksize = 320;
        ana->numbins = 320;
        ana->numprotos = 6;
        break;
    case WINDOW_PPFA_PROTO_480_2880:
        ana->blocksize = 480;
        ana->numbins = 480;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_640_3840:
        ana->blocksize = 640;
        ana->numbins = 640;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_256_1536:
    case WINDOW_PPFA_PROTO_256_1536_DELAY768:
        ana->blocksize = 256;
        ana->numbins = 256;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_512_3072:
    case WINDOW_PPFA_PROTO_512_3072_DELAY1536:
        ana->blocksize = 512;
        ana->numbins = 512;
        ana->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    default:
        return RET_ILLEGAL_PARAMETER;
    }

    ana->pPreObj = calloc(1, numchs * sizeof(*ana->pPreObj));
    if (!ana->pPreObj)
        return RET_INSUFFICIENT_MEMORY;
    for (i = 0; i < numchs; ++i) {
        ana->pPreObj[i].pAnaBuf =
            calloc(1, ana->numprotos * ana->blocksize * sizeof(FP16IfEn));
        if (!ana->pPreObj[i].pAnaBuf)
            return RET_INSUFFICIENT_MEMORY;
    }
    FeOpt_RFFT640_GenTab_f32();
    *this1 = ana;
    return RET_OK;
}

int PPFBankAnalyzer_getTmpMemSize(PPFBankAnalyzer *this1)
{
    return this1->blocksize * 2 * (int)sizeof(float);
}

void PPFBankAnalyzer_setTmpMem(PPFBankAnalyzer *this1, void *pTmpMem)
{
    this1->pTmpFFTBuf = pTmpMem;
}

void PPFBankAnalyzer_free(PPFBankAnalyzer *this1)
{
    int i;
    for (i = 0; i < this1->numchs; ++i)
        free(this1->pPreObj[i].pAnaBuf);
    free(this1->pPreObj);
    free(this1);
}

int PPFBankAnalyzer_numChannels(PPFBankAnalyzer *this1) { return this1->numchs; }
int PPFBankAnalyzer_blockSize(PPFBankAnalyzer *this1) { return this1->blocksize; }
int PPFBankAnalyzer_numBins(PPFBankAnalyzer *this1) { return this1->numbins; }

int PPFBankAnalyzer_f2i(PPFBankAnalyzer *this1, float fs, float f)
{
    int fi = Util_round((2.0f * f * this1->numbins) / fs);
    if (fi < 0)
        fi = 0;
    else if (fi >= this1->numbins)
        fi = this1->numbins - 1;
    return fi;
}

float PPFBankAnalyzer_i2f(PPFBankAnalyzer *this1, float fs, int fi)
{
    return fi * fs / (2.0f * this1->numbins);
}

void PPFBankAnalyzer_process(PPFBankAnalyzer *this1, float *input, float *output)
{
    float *pTmpFFTBuf = this1->pTmpFFTBuf;
    int numchs = this1->numchs;
    int blocksize = this1->blocksize;
    int numprotos = this1->numprotos;
    int fftSize = 2 * blocksize;
    int chIdx;

    for (chIdx = 0; chIdx < numchs; ++chIdx) {
        PreObj_T *pPreObj = this1->pPreObj + chIdx;
        FP16IfEn *pAnaBuf = pPreObj->pAnaBuf;
        int anaBufPos = pPreObj->anaBufPos;
        FP16IfEn *pBuf0, *pBuf1, *pBuf2;
        const float *pWin0, *pWin1, *pWin2;
        int pcmIdx;

        FeOpt_PreEmp(pAnaBuf + anaBufPos * blocksize, input,
                     &pPreObj->preEmphBk, 0.9f, blocksize);

        if (++anaBufPos >= numprotos)
            anaBufPos = 0;
        pBuf0 = pAnaBuf + anaBufPos * blocksize;
        pWin0 = tFeOpt_PFBA_Win320_1920_Delay960;

        anaBufPos += 2;
        if (anaBufPos >= numprotos)
            anaBufPos -= numprotos;
        pBuf1 = pAnaBuf + anaBufPos * blocksize;
        pWin1 = tFeOpt_PFBA_Win320_1920_Delay960 + fftSize;

        anaBufPos += 2;
        if (anaBufPos >= numprotos)
            anaBufPos -= numprotos;
        pBuf2 = pAnaBuf + anaBufPos * blocksize;
        pWin2 = tFeOpt_PFBA_Win320_1920_Delay960 + 2 * fftSize;

        for (pcmIdx = 0; pcmIdx < blocksize; ++pcmIdx)
            pTmpFFTBuf[pcmIdx] = (float)pBuf0[pcmIdx] * pWin0[pcmIdx] +
                                 (float)pBuf1[pcmIdx] * pWin1[pcmIdx] +
                                 (float)pBuf2[pcmIdx] * pWin2[pcmIdx];

        anaBufPos += 3;
        if (anaBufPos >= numprotos)
            anaBufPos -= numprotos;
        pBuf0 = pAnaBuf + anaBufPos * blocksize;
        pWin0 = tFeOpt_PFBA_Win320_1920_Delay960 + blocksize;

        anaBufPos += 2;
        if (anaBufPos >= numprotos)
            anaBufPos -= numprotos;
        pBuf1 = pAnaBuf + anaBufPos * blocksize;
        pWin1 = tFeOpt_PFBA_Win320_1920_Delay960 + blocksize + fftSize;

        anaBufPos += 2;
        if (anaBufPos >= numprotos)
            anaBufPos -= numprotos;
        pBuf2 = pAnaBuf + anaBufPos * blocksize;
        pWin2 = tFeOpt_PFBA_Win320_1920_Delay960 + blocksize + 2 * fftSize;

        for (pcmIdx = 0; pcmIdx < blocksize; ++pcmIdx)
            pTmpFFTBuf[blocksize + pcmIdx] =
                (float)pBuf0[pcmIdx] * pWin0[pcmIdx] +
                (float)pBuf1[pcmIdx] * pWin1[pcmIdx] +
                (float)pBuf2[pcmIdx] * pWin2[pcmIdx];

        FeOpt_RFFT640_f32(output, pTmpFFTBuf);
        anaBufPos = pPreObj->anaBufPos;
        ++anaBufPos;
        if (anaBufPos >= numprotos)
            anaBufPos = 0;
        pPreObj->anaBufPos = anaBufPos;
        output += fftSize;
        input += blocksize;
    }
}

RET_CODE PPFBankAnalyzer_block2WindowType16k(BLOCK_TYPE btype,
                                              PPFA_WINDOW_TYPE *wtype)
{
    switch (btype) {
    case BLOCK_DEFAULT:
    case BLOCK_20MS_LD:
        *wtype = WINDOW_PPFA_PROTO_320_1920_DELAY960;
        return RET_OK;
    case BLOCK_10MS:
        *wtype = WINDOW_PPFA_PROTO_160_960;
        break;
    case BLOCK_10MS_LD:
        *wtype = WINDOW_PPFA_PROTO_TIANBIAO_160_960;
        break;
    case BLOCK_16MS:
        *wtype = WINDOW_PPFA_PROTO_256_1536;
        break;
    case BLOCK_16MS_LD:
        *wtype = WINDOW_PPFA_PROTO_256_1536_DELAY768;
        break;
    case BLOCK_20MS:
        *wtype = WINDOW_PPFA_PROTO_320_1920;
        break;
    default:
        return RET_ILLEGAL_PARAMETER;
    }
    return RET_ILLEGAL_PARAMETER;
}
