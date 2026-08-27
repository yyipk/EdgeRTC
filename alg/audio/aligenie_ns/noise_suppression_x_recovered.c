/* Recovered from libmit_rtos.a (noise_suppression_x.o). */
#include "ns_c_recovered.h"

#include <stdlib.h>

int TNRx_Create(NsxHandle *nsxInst)
{
    NsxHandle inst = malloc(sizeof(*inst));

    *nsxInst = inst;
    if (!inst)
        return -1;
    inst->initFlag = 0;
    return 0;
}

int TNRx_Free(NsxHandle nsxInst)
{
    Delete_TDereverb_x_Params(&nsxInst->pTDPar);
    free(nsxInst);
    return 0;
}

int TNRx_Init(NsxHandle nsxInst, uint32_t fs)
{
    return TNRx_InitCore(nsxInst, fs);
}

int TNRx_set_policy(NsxHandle nsxInst, int mode)
{
    return TNRx_set_policy_core(nsxInst, mode);
}

int TNRx_set_dereverb(NsxHandle nsxInst, int mode)
{
    return TNRx_set_dereverb_core(nsxInst, mode);
}

int TNRx_Process(NsxHandle nsxInst, int16_t *speechFrame,
                 int16_t *speechFrameHB, int16_t *outFrame,
                 int16_t *outFrameHB, int tns_mode)
{
    return TNRx_ProcessCore(nsxInst, speechFrame, speechFrameHB, outFrame,
                            outFrameHB, tns_mode);
}
