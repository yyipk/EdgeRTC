/* Recovered from libmit_rtos.a (NS.o). */
#include "ns_c_recovered.h"

#include <string.h>

void NS_defaultParams(const Params *params, NSParams *specparams)
{
    memset(specparams, 0, sizeof(*specparams));
    specparams->ns_algorithm = params->ns_algorithm;
    specparams->numchs = params->nummics;
    specparams->numrefs = params->numrefs;
    specparams->ns_power = params->ns_power;
    specparams->ns_level = params->ns_level;
    specparams->ns_model_base = params->ns_model_base;
}
