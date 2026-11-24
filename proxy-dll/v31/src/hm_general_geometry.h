// GM/LM HIT boxes from their own installed DMO and DEF; no boss hull reuse.
#pragma once
static const HmGeomDef hmGeomGM[]={
{"collid_body",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,0.0f,-22.5f,0.0f,1.0f},{18.0f,30.0f,52.5f}},
};
static const HmGeomDef hmGeomLM[]={
{"collid_body",{1.91068547e-15f,-1.0f,-4.37113883e-08f,0.0f,-4.37113883e-08f,-4.37113883e-08f,1.0f,0.0f,-1.0f,0.0f,-4.37113883e-08f,0.0f,0.0f,50.3999977f,1.79999924f,1.0f},{43.2000008f,21.5999985f,32.3999977f}},
};
static bool HmGeneralContact(const HmFrame& f) {
    BYTE* muzzle=((MmFind_t)(hmModule+0x15a00))(f.actor,"attack");if(!muzzle)return false;
    float aw[16];HmGeomBox a;float* mw=((MmWorld_t)(hmModule+0x15930))(muzzle);
    HmGeomMultiply(hmGeomPayload.m,mw,aw);HmGeomMake(aw,hmGeomPayload.half,&a);
    const HmGeomDef* defs=hmSource==720?hmGeomGM:hmGeomLM;
    const int count=hmSource==720?(int)_countof(hmGeomGM):(int)_countof(hmGeomLM);
    for(int i=0;i<count;++i) {
        BYTE* node=((MmFind_t)(hmModule+0x15a00))(f.target,defs[i].node);if(!node)continue;
        float bw[16];HmGeomBox b;float* w=((MmWorld_t)(hmModule+0x15930))(node);
        HmGeomMultiply(defs[i].m,w,bw);HmGeomMake(bw,defs[i].half,&b);
        if(HmGeomOverlap(a,b))return true;
    }
    return false;
}
