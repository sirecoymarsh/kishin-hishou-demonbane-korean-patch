#pragma once
#include "aion_policy.h"
struct AionActorRoute {const char* definition;const char* targetDefinition;const char* opponent;AionPolicy policy;};
struct AionRoute {int tier,control,source,loaded,destination;AionActorRoute actor[2];};
static const AionRoute aionRoutes[]={
{1,1,714,7350,9083,{{"AN_T1_S714_P0.def","NG_LIVE_714_AM.def","QM",{160.0f,45.0f,950.0f,580.0f,1}},{0,0,0,{0,0,0,0,0}}}},
{1,1,845,7354,9109,{{0,0,0,{0,0,0,0,0}},{"AN_T1_S845_P1.def","NG_LIVE_845_BM.def","QM",{160.0f,45.0f,950.0f,580.0f,1}}}},
};
static const AionRoute* FindAionRoute(bool balanced,int tier,int control,int source,int loaded)
{
    if(!balanced || tier<0 || tier>2 || control<0 || control>1)return 0;
    for(unsigned i=0;i<sizeof(aionRoutes)/sizeof(aionRoutes[0]);++i) {
        const AionRoute& r=aionRoutes[i];
        if(r.tier==tier && r.control==control && r.source==source && r.loaded==loaded)return &r;
    }
    return 0;
}
