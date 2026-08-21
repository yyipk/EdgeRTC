#include "aec_recovered.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int DRAEC_init(const AECParams *params, DRAEC **handler)
{
    DRAEC *pp = malloc(sizeof(*pp));
    if (!pp) return -95;
    memset((unsigned char *)pp + 8, 0, sizeof(*pp) - 8);
    pp->nummics = params->nummics;
    pp->numrefs = params->numrefs;
    pp->numbins = params->numbins;
    if (params->aec_gain == 0.0f)
        pp->gain_amp = 1.0f;
    else
        pp->gain_amp = sqrtf(Util_db2pow(params->aec_gain));
    float forget = params->aec_forget;
    if (!(forget > 0.0f && forget <= 1.0f))
        forget = 0.995f;
    pp->sqrt1nforget = sqrtf(1.0f - forget);
    pp->isqrtforget = 1.0f / sqrtf(forget);
    int split = (int)params->aec_level;
    if (split <= 0) split = 16;
    if (params->numbins == 256 || params->numbins == 512) {
        if (split <= 1) split = 1;
        else if (split <= 2) split = 2;
        else if (split <= 4) split = 4;
        else if (split <= 8) split = 8;
        else if (split <= 16) split = 16;
        else split = 32;
    } else {
        if (split <= 1) split = 1;
        else if (split <= 2) split = 2;
        else if (split <= 4) split = 4;
        else if (split <= 5) split = 5;
        else if (split <= 8) split = 8;
        else if (split <= 10) split = 10;
        else if (split <= 16) {
            if (params->numbins <= 160)
                split = 20;
            else
                split = 16;
        }
        else split = 20;
    }
    pp->filen = params->numbins / split;
    int rflen = params->aec_power;
    if (rflen <= 0) rflen = 6;
    pp->rtaps = params->numrefs * rflen;
    int mflen = params->aec_power2;
    if (mflen < 0) mflen = rflen - 1;
    pp->mtaps = params->nummics * mflen;
    pp->drDelay = pp->nummics;
    int taps = pp->mtaps + pp->rtaps;
    int micBufLen = pp->mtaps + pp->drDelay;
    int dataBinBufLen = micBufLen + pp->rtaps;
    pp->pInBuf = calloc(1, pp->numbins * dataBinBufLen *
                         sizeof(*pp->pInBuf));
    pp->pDrAecPath = calloc(1, pp->numbins * pp->nummics * taps *
                             sizeof(*pp->pDrAecPath));
    int iuSize = taps * taps;
    pp->pIu = calloc(1, params->numbins * iuSize * sizeof(*pp->pIu));
    if (!pp->pIu) return -95;
    for (int i = 0; i < pp->numbins; ++i) {
        FP16IfEn_T *pTmpIu = pp->pIu + i * iuSize;
        for (int j = 0; j < taps; ++j) {
            pTmpIu += 2 * j;
            *pTmpIu = AEC_FP16_FROM_FLOAT(100.0f);
            ++pTmpIu;
        }
    }
    *handler = pp;
    return 0;
}

void DRAEC_process(void *handler, float *pData)
{
    DRAEC *this1 = handler;
    int nummics = this1->nummics;
    int numrefs = this1->numrefs;
    int numbins = this1->numbins;
    int micPos = this1->micPos;
    int refPos = this1->refPos;
    int micTaps = this1->mtaps;
    int refTaps = this1->rtaps;
    int drDelay = this1->drDelay;
    int i;

    NlsFe_DrAecFilter(this1->pInBuf, micPos, refPos, (CpxFloat_T *)pData,
                      nummics, numrefs, numbins, this1->pDrAecPath,
                      micTaps, refTaps, drDelay, this1->gain_amp);
    for (i = 0; i < micTaps + refTaps; ++i)
        this1->pTmpRandn[i] = Util_randn(&this1->randseed) * 0.001f;
    NlsFe_DrAecSetNoiseBuf(this1->pInBuf, micPos, refPos,
        this1->fioffset, this1->filen, drDelay, this1->pTmpDlUpd,
        micTaps, refTaps, this1->pTmpRandn);
    micPos -= nummics;
    if (micPos < 0)
        micPos += this1->drDelay + micTaps;
    refPos -= numrefs;
    if (refPos < 0) refPos += refTaps;
    this1->micPos = micPos;
    this1->refPos = refPos;
}

void DRAEC_update(void *handler, float *pData)
{
    DRAEC *this1 = handler;
    int nummics = this1->nummics;
    int numbins = this1->numbins;
    int fioffset = this1->fioffset;
    int filen = this1->filen;
    CpxFp16_T *pInBuf = this1->pInBuf;
    float sqrt1nforget = this1->sqrt1nforget;
    float isqrtforget = this1->isqrtforget;
    FP16IfEn_T *pIu = this1->pIu;
    CpxFloat_T *pDrAecPath = this1->pDrAecPath;
    CpxFloat_T *pTmpDlUpd = this1->pTmpDlUpd;
    CpxFloat_T *pTmpVa = this1->pTmpVa;
    CpxFloat_T *pTmpVu = this1->pTmpVu;
    float invgainsq = 1.0f / (this1->gain_amp * this1->gain_amp);
    float invgain = 1.0f / this1->gain_amp;
    int taps = this1->mtaps + this1->rtaps;
    int dataBinBufLen = this1->drDelay + taps;
    int iuSize = taps * taps;
    int micCurPos = this1->micPos + nummics;

    if (micCurPos >= this1->mtaps + this1->drDelay) micCurPos = 0;
    for (int fi = fioffset; fi < fioffset + filen; ++fi) {
        float sqrtphi = NlsFe_DrAecCalcSqrt(
            pInBuf + fi * dataBinBufLen + micCurPos,
            (CpxFloat_T *)pData + fi, numbins, nummics, invgainsq);
        sqrtphi += 0.01f;
        float tmpscale = powf(sqrtphi, -0.45f) * sqrt1nforget;
        float va0;
        NlsFe_DrVaInit(pTmpVa, pIu + fi * iuSize,
                       pTmpDlUpd + (fi - fioffset) * taps, taps,
                       -isqrtforget * tmpscale);
        va0 = NlsFe_DrGivRot(pIu + fi * iuSize,
                             pTmpVa, pTmpVu, taps, isqrtforget);
        NlsFe_DrAecUpdatePath(pDrAecPath + fi * nummics * taps, pTmpVu,
                              (CpxFloat_T *)pData + fi,
                              nummics, taps, numbins,
                              (1.0f / va0) * tmpscale, invgain);
    }
    fioffset += filen;
    if (fioffset >= numbins) fioffset = 0;
    this1->fioffset = fioffset;
}

void DRAEC_mount(void *handler, AEC *api)
{
    api->handler = handler;
    api->init = DRAEC_init;
    api->free = DRAEC_free;
    api->numMics = DRAEC_numMics;
    api->numRefs = DRAEC_numRefs;
    api->numBins = DRAEC_numBins;
    api->delayBlocks = DRAEC_delayBlocks;
    api->process = DRAEC_process;
    api->update = DRAEC_update;
}
