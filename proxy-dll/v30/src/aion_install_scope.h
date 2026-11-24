#pragma once
#include "aion_scope.h"
// Installation boundary only. Reuse R19 algorithms on the one confirmed ordered route.
static bool AionInstallScope(bool balanced,int tier,int control,int source,const AionRoute* r,int side)
{
    return balanced && tier==1 && control==1 && source==845 && r &&
        r->tier==1 && r->control==1 && r->source==845 &&
        r->loaded==7354 && r->destination==9109 && side==1;
}
