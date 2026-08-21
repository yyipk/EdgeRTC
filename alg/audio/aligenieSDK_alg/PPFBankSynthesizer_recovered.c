#include "aec_frontend_recovered.h"
#include <stdlib.h>

void FeOpt_RIFFT640_f32(float *pDst, float *pSrc);
void FeOpt_AddWinMacRev(FP16IfEn *pDst, float *pSrc,
                        const float *pWin, int len);
void FeOpt_AddWinMulRev(FP16IfEn *pDst, float *pSrc,
                        const float *pWin, int len);
void FeOpt_DeEmp(short *pDst, FP16IfEn *pSrc, float *pBkVal,
                 float coefVal, int len);
extern const float tFeOpt_PFBA_Win320_1920_Delay960[];

RET_CODE PPFBankSynthesizer_init(int numchs, PPFA_WINDOW_TYPE wtype,
                                 PPFBankSynthesizer **this1)
{
    PPFBankSynthesizer *syn = malloc(sizeof(*syn));
    int i;

    if (!syn)
        return RET_INSUFFICIENT_MEMORY;
    *syn = (PPFBankSynthesizer){0};
    syn->numchs = numchs;

    switch (wtype) {
    case WINDOW_PPFA_PROTO_160_960:
    case WINDOW_PPFA_PROTO_TIANBIAO_160_960:
        syn->blocksize = 160;
        syn->numbins = 160;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1280:
        syn->blocksize = 320;
        syn->numbins = 320;
        syn->numprotos = 4;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1920:
        syn->blocksize = 320;
        syn->numbins = 320;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_320_1920_DELAY960:
        syn->blocksize = 320;
        syn->numbins = 320;
        syn->numprotos = 6;
        break;
    case WINDOW_PPFA_PROTO_480_2880:
        syn->blocksize = 480;
        syn->numbins = 480;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_640_3840:
        syn->blocksize = 640;
        syn->numbins = 640;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_256_1536:
    case WINDOW_PPFA_PROTO_256_1536_DELAY768:
        syn->blocksize = 256;
        syn->numbins = 256;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    case WINDOW_PPFA_PROTO_512_3072:
    case WINDOW_PPFA_PROTO_512_3072_DELAY1536:
        syn->blocksize = 512;
        syn->numbins = 512;
        syn->numprotos = 6;
        return RET_ILLEGAL_PARAMETER;
    default:
        return RET_ILLEGAL_PARAMETER;
    }

    syn->pPostObj = calloc(1, numchs * sizeof(*syn->pPostObj));
    if (!syn->pPostObj)
        return RET_INSUFFICIENT_MEMORY;
    for (i = 0; i < numchs; ++i) {
        syn->pPostObj[i].pSynBuf =
            calloc(1, syn->numprotos * syn->blocksize * sizeof(FP16IfEn));
        if (!syn->pPostObj[i].pSynBuf)
            return RET_INSUFFICIENT_MEMORY;
    }
    *this1 = syn;
    return RET_OK;
}

int PPFBankSynthesizer_getTmpMemSize(PPFBankSynthesizer *this1)
{
    return this1->blocksize * 2 * (int)sizeof(float);
}

void PPFBankSynthesizer_setTmpMem(PPFBankSynthesizer *this1, void *pTmpMem)
{
    this1->pTmpFFTBuf = pTmpMem;
}

void PPFBankSynthesizer_free(PPFBankSynthesizer *this1)
{
    int i;
    for (i = 0; i < this1->numchs; ++i)
        free(this1->pPostObj[i].pSynBuf);
    free(this1->pPostObj);
    free(this1);
}

int PPFBankSynthesizer_numChannels(PPFBankSynthesizer *this1)
{
    return this1->numchs;
}

int PPFBankSynthesizer_blockSize(PPFBankSynthesizer *this1)
{
    return this1->blocksize;
}

int PPFBankSynthesizer_numBins(PPFBankSynthesizer *this1)
{
    return this1->numbins;
}

int PPFBankSynthesizer_delay(PPFBankSynthesizer *this1)
{
    (void)this1;
    return 960;
}

void PPFBankSynthesizer_process(PPFBankSynthesizer *this1, float *input,
                                short *output)
{
    float *pTmpFFTBuf = this1->pTmpFFTBuf;
    int numchs = this1->numchs;
    int blocksize = this1->blocksize;
    int numprotos = this1->numprotos;
    int fftSize = 2 * blocksize;
    int chIdx;

    for (chIdx = 0; chIdx < numchs; ++chIdx) {
        PostObj_T *pPostObj = this1->pPostObj + chIdx;
        float *pFdBuf = pTmpFFTBuf;
        FP16IfEn *pSynBuf = pPostObj->pSynBuf;
        const float *pWin0 = tFeOpt_PFBA_Win320_1920_Delay960 +
                             (numprotos - 1) * blocksize;
        const float *pWin1 = tFeOpt_PFBA_Win320_1920_Delay960 +
                             (numprotos - 2) * blocksize;
        int synBufPos = pPostObj->synBufPos;
        int frmIdx;

        FeOpt_RIFFT640_f32(pFdBuf, input);
        for (frmIdx = 0; frmIdx < (numprotos >> 1) - 1; ++frmIdx) {
            FeOpt_AddWinMacRev(pSynBuf + synBufPos * blocksize,
                               pFdBuf, pWin0,
                               blocksize);
            ++synBufPos;
            if (synBufPos >= numprotos)
                synBufPos = 0;
            FeOpt_AddWinMacRev(pSynBuf + synBufPos * blocksize,
                               pFdBuf + blocksize, pWin1,
                               blocksize);
            ++synBufPos;
            if (synBufPos >= numprotos)
                synBufPos = 0;
            pWin0 = (const float *)((const char *)pWin0 +
                                    blocksize * -(int)(2 * sizeof(float)));
            pWin1 = (const float *)((const char *)pWin1 +
                                    blocksize * -(int)(2 * sizeof(float)));
        }

        FeOpt_AddWinMacRev(pSynBuf + synBufPos * blocksize,
                           pFdBuf,
                           tFeOpt_PFBA_Win320_1920_Delay960 + blocksize,
                           blocksize);
        ++synBufPos;
        if (synBufPos >= numprotos)
            synBufPos = 0;
        FeOpt_AddWinMulRev(pSynBuf + synBufPos * blocksize,
                           pFdBuf + blocksize,
                           tFeOpt_PFBA_Win320_1920_Delay960, blocksize);
        ++synBufPos;
        if (synBufPos >= numprotos)
            synBufPos = 0;

        FeOpt_DeEmp(output, pSynBuf + synBufPos * blocksize,
                    &pPostObj->deEmphBk, 0.9f, blocksize);
        synBufPos = pPostObj->synBufPos;
        ++synBufPos;
        if (synBufPos >= numprotos)
            synBufPos = 0;
        pPostObj->synBufPos = synBufPos;
        output += blocksize;
        input += fftSize;
    }
}
