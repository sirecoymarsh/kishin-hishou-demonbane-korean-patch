// Generated from native-verified COLL matrices/half extents; predictive actor pose plus live target nodes.
#pragma once
struct HmGeomDef {const char* node;float m[16];float half[3];};
struct HmGeomBox {float c[3];float edge[3][3];};
static void HmGeomMultiply(const float* a,const float* b,float* out) {
    for(int i=0;i<4;++i)for(int j=0;j<4;++j) {
        out[i*4+j]=0;for(int k=0;k<4;++k)out[i*4+j]+=a[i*4+k]*b[k*4+j];
    }
}
static void HmGeomMake(const float* m,const float* half,HmGeomBox* b) {
    for(int k=0;k<3;++k) {b->c[k]=m[12+k];for(int i=0;i<3;++i)b->edge[i][k]=m[i*4+k]*half[i];}
}
static float HmGeomDot(const float* a,const float* b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void HmGeomCross(const float* a,const float* b,float* o) {
    o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];
}
static bool HmGeomSeparates(const HmGeomBox& a,const HmGeomBox& b,const float* axis) {
    if(HmGeomDot(axis,axis)<1e-10f)return false;
    float delta[3]={b.c[0]-a.c[0],b.c[1]-a.c[1],b.c[2]-a.c[2]},r=0;
    for(int i=0;i<3;++i)r+=fabsf(HmGeomDot(a.edge[i],axis))+fabsf(HmGeomDot(b.edge[i],axis));
    return fabsf(HmGeomDot(delta,axis))>r+1e-4f;
}
static bool HmGeomOverlap(const HmGeomBox& a,const HmGeomBox& b) {
    float axis[3];
    for(int i=0;i<3;++i) {
        HmGeomCross(a.edge[i],a.edge[(i+1)%3],axis);if(HmGeomSeparates(a,b,axis))return false;
        HmGeomCross(b.edge[i],b.edge[(i+1)%3],axis);if(HmGeomSeparates(a,b,axis))return false;
        for(int j=0;j<3;++j) {HmGeomCross(a.edge[i],b.edge[j],axis);if(HmGeomSeparates(a,b,axis))return false;}
    }
    return true;
}
static const HmGeomDef hmGeomPayload={"attack",{-2.82129971e-07f,0.0f,-1.0f,0.0f,0.0f,1.0f,0.0f,0.0f,1.0f,0.0f,-2.82129971e-07f,0.0f,0.0f,0.0f,0.0f,1.0f},{25.0f,25.0f,25.0f}};
static const HmGeomDef hmGeomRM[]={
{"collid_body",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,-0.180000305f,255.555573f,-19.1966496f,1.0f},{41.1777191f,31.9081192f,41.7563553f}},
{"collid_neck",{-1.0f,0.0f,-8.74227766e-08f,0.0f,0.0f,1.0f,0.0f,0.0f,8.74227766e-08f,0.0f,-1.0f,0.0f,0.0f,-3.75f,0.0f,1.0f},{26.25f,22.5f,60.0f}},
{"collid_neck3",{-1.0f,0.0f,-8.74227766e-08f,0.0f,0.0f,1.0f,0.0f,0.0f,8.74227766e-08f,0.0f,-1.0f,0.0f,0.0f,-3.75f,0.0f,1.0f},{26.25f,22.5f,60.0f}},
{"collid_neck2",{-1.0f,0.0f,-8.74227766e-08f,0.0f,0.0f,1.0f,0.0f,0.0f,8.74227766e-08f,0.0f,-1.0f,0.0f,0.0f,-3.75f,0.0f,1.0f},{26.25f,22.5f,60.0f}},
{"collid_central",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,0.0f,-1.125f,0.0f,1.0f},{139.5f,225.0f,488.460022f}},
};
static const HmGeomDef hmGeomSM[]={
{"collid_bodyA",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,0.0f,314.400024f,7.5f,1.0f},{18.0f,14.0999994f,81.9000092f}},
{"collid_bodyD",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,2.52629089f,146.940018f,57.0337448f,1.0f},{102.140999f,150.617599f,90.0900116f}},
{"collid_central",{-4.37113883e-08f,4.37113883e-08f,-1.0f,0.0f,1.0f,1.91068568e-15f,-4.37113883e-08f,0.0f,0.0f,-1.0f,-4.37113883e-08f,0.0f,0.0f,-1.125f,0.0f,1.0f},{139.5f,225.0f,488.460022f}},
{"collid_central2",{1.91068547e-15f,-1.0f,-4.37113883e-08f,0.0f,-4.37113883e-08f,-4.37113883e-08f,1.0f,0.0f,-1.0f,0.0f,-4.37113883e-08f,0.0f,0.0f,105.0f,-313.125f,1.0f},{86.25f,170.625f,131.25f}},
{"collid_central3",{1.91068547e-15f,-1.0f,-4.37113883e-08f,0.0f,-4.37113883e-08f,-4.37113883e-08f,1.0f,0.0f,-1.0f,0.0f,-4.37113883e-08f,0.0f,0.0f,-82.875f,-253.125f,1.0f},{101.25f,114.375f,131.25f}},
};
static bool HmMeleeContact(const HmFrame& f,unsigned step) {
    BYTE* muzzle=((MmFind_t)(hmModule+0x15a00))(f.actor,"attack");if(!muzzle)return false;
    // The verified COMBO/RAI/KAMA launch attachment transform is invariant.
    // Use the engine's own world frame instead of assuming DMO and actor frames coincide.
    float aw[16];HmGeomBox a;float* mw=((MmWorld_t)(hmModule+0x15930))(muzzle);
    HmGeomMultiply(hmGeomPayload.m,mw,aw);HmGeomMake(aw,hmGeomPayload.half,&a);
    const HmGeomDef* defs=hmSource==732?hmGeomRM:hmGeomSM;
    const int count=hmSource==732?(int)_countof(hmGeomRM):(int)_countof(hmGeomSM);
    for(int i=0;i<count;++i) {
        BYTE* node=((MmFind_t)(hmModule+0x15a00))(f.target,defs[i].node);if(!node)continue;
        float bw[16];HmGeomBox b;float* world=((MmWorld_t)(hmModule+0x15930))(node);
        HmGeomMultiply(defs[i].m,world,bw);HmGeomMake(bw,defs[i].half,&b);
        if(HmGeomOverlap(a,b)) {
            Log("HM_CONTACT source=%d step=%u hit=%s attack=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f",
                hmSource,step,defs[i].node,a.c[0],a.c[1],a.c[2],b.c[0],b.c[1],b.c[2]);
            return true;
        }
    }
    return false;
}
