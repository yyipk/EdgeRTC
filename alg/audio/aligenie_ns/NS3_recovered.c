/* Recovered from libmit_rtos.a (NS3.o). */
#include "ns_c_recovered.h"

#include <stdlib.h>
#include <string.h>

NS3Para *NS3_Init(int SampleRateHz, int FramePointNum,
                  RET_CODE *ReturnCode)
{
    NsxHandle my_handle = NULL;
    int frame_size = FramePointNum;
    NS3Para *tns3_st = calloc(1, sizeof(*tns3_st));

    if (SampleRateHz != 8000 && SampleRateHz != 16000 &&
        SampleRateHz != 32000 && SampleRateHz != 48000) {
        *ReturnCode = RET_ILLEGAL_PARAMETER;
        return NULL;
    }
    if (FramePointNum == 80 || FramePointNum == 160 ||
        FramePointNum == 320 || FramePointNum == 480) {
        TNRx_Create(&my_handle);
        TNRx_Init(my_handle, SampleRateHz);
        TNRx_set_policy(my_handle, 1);
        tns3_st->frame_size = frame_size;
        *ReturnCode = RET_OK;
        tns3_st->ns_st = my_handle;
        return tns3_st;
    }
    *ReturnCode = RET_ILLEGAL_PARAMETER;
    return NULL;
}

int NS3_Process(NS3Para *st, int16_t *NS3In, int16_t *NS3InH,
                int16_t *NS3Out, int16_t *NS3OutH, int NS3Mode)
{
    TNRx_Process(st->ns_st, NS3In, NS3InH, NS3Out, NS3OutH, NS3Mode);
    return RET_OK;
}

int NS3_Exit(NS3Para *st)
{
    TNRx_Free(st->ns_st);
    free(st);
    return RET_OK;
}

RET_CODE NS3_SetPara(NS3Para *st, const char *ParaName,
                     const char *ParaVal)
{
    int ReturnErrCode = RET_OK;

    if (!st) {
        ReturnErrCode = RET_NOT_INITIALIZED;
    } else if (strcmp(ParaName, "NS_Power") == 0) {
        int tnsMode = atoi(ParaVal);

        TNRx_set_policy(st->ns_st, 1);
        if (TNRx_set_policy(st->ns_st, tnsMode) == -1)
            ReturnErrCode = RET_ILLEGAL_PARAMETER;
    } else if (strcmp(ParaName, "DR_Power") == 0) {
        int dereverbMode = atoi(ParaVal);

        TNRx_set_dereverb(st->ns_st, 0);
        if (TNRx_set_dereverb(st->ns_st, dereverbMode) == -1)
            ReturnErrCode = RET_ILLEGAL_PARAMETER;
    } else {
        ReturnErrCode = RET_ILLEGAL_PARAMETER;
    }

    return ReturnErrCode;
}
