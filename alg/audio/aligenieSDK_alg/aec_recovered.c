/*
 * Recovered from libmit_rtos.a (DRAEC.o, AEC.o).
 *
 * The DRAEC kernels are recovered in the companion *_recovered.c files; this
 * unit owns the public parameter and handler ABI.
 */
#include "aec_recovered.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef struct {
    unsigned char pad0[20];
    int nummics;
    int numrefs;
    unsigned char pad1[668];
    AEC_ALGORITHM aec_algorithm;
    int aec_power;
    int aec_power2;
    float aec_level;
    float aec_forget;
    float aec_gain;
} Params;

void AEC_defaultParams(const void *raw, AECParams *out)
{
    const Params *params = raw;
    memset(out, 0, sizeof(*out));
    out->aec_algorithm = params->aec_algorithm;
    out->nummics = params->nummics;
    out->numrefs = params->numrefs;
    out->aec_power = params->aec_power;
    out->aec_power2 = params->aec_power2;
    out->aec_level = params->aec_level;
    out->aec_forget = params->aec_forget;
    out->aec_gain = params->aec_gain;
}

int DRAEC_numMics(void *h) { return ((DRAEC *)h)->nummics; }
int DRAEC_numRefs(void *h) { return ((DRAEC *)h)->numrefs; }
int DRAEC_numBins(void *h) { return ((DRAEC *)h)->numbins; }
int DRAEC_delayBlocks(void *h) { (void)h; return 0; }

int DRAEC_getTmpMemSize(void *h)
{
    DRAEC *d = (DRAEC *)h;
    int taps = d->mtaps + d->rtaps;
    int tmpSize = d->filen * taps;
    tmpSize <<= 3;
    tmpSize += 20 * taps;
    return tmpSize;
}

void DRAEC_setTmpMem(void *h, void *mem)
{
    DRAEC *d = (DRAEC *)h;
    int taps = d->mtaps + d->rtaps;

    if (!mem) return;
    d->pTmpRandn = mem;
    d->pTmpDlUpd = (CpxFloat_T *)(d->pTmpRandn + taps);
    d->pTmpVa = d->pTmpDlUpd + d->filen * taps;
    d->pTmpVu = d->pTmpVa + taps;
}

void DRAEC_free(void *h)
{
    DRAEC *d = (DRAEC *)h;
    free(d->pInBuf);
    free(d->pDrAecPath);
    free(d->pIu);
    free(d);
}
