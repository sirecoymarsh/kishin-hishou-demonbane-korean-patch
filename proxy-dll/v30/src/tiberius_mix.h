// Easy/Normal balanced DUEL: at most two Star Vampires before ONRYOU or a 20-second combat-time gap.
// Observe real motion starts; filter only unstarted native AI proposals. Actor execution is untouched.
#pragma once
struct ImMixState {
    BYTE* actor; BYTE* ai;
    float elapsed,lastStarAt;
    unsigned stars;
    int lastKind,lastFrame;
    bool blockedLogged;
};
static BYTE* imMixModule;
static bool imMixEnabled;
static ImMixState imMixState[2];
typedef void(__thiscall* ImMixSenseFn)(BYTE*,DWORD);
typedef bool(__thiscall* ImMixRequestFn)(BYTE*,const char*,int);
static BYTE* ImMixActor(BYTE* ai,int* side) {
    if(!imMixEnabled || !duelBalance || duelDifficulty<0 || duelDifficulty>1 || balScene<0 || balScene>=1000)return NULL;
    if(*(void**)ai!=imMixModule+0xe7dfc)return NULL; // native CAiBeruze only
    BYTE* a=*(BYTE**)(ai+4);if(!a)return NULL;
    int s=*(int*)(a+0x724);if(s<0 || s>1 || (s==0 && !duelAutoFight))return NULL;
    if(*(int*)(a+0x558)<=0)return NULL;
    const char* def=HvDefinition(a);if(!def)return NULL;
    char path[512];if(strcpy_s(path,def))return NULL;
    _strlwr_s(path);for(char* p=path;*p;++p)if(*p=='\\')*p='/';
    if(!strstr(path,"/character/im/") || !strstr(path,"/data/imix/"))return NULL;
    *side=s;return a;
}
static void ImMixWithdraw(BYTE* a) {
    // Native 0x30880 lays out three buffer banks, each five entries at stride 0x30.
    for(int bank=0;bank<3;++bank)for(int i=0;i<5;++i){
        BYTE** p=(BYTE**)(a+0x2b8+bank*0x30+i*8);BYTE* m=*p;
        if(m && !_stricmp(*(const char**)m,"S_VANPIRE")){*p=NULL;*(DWORD*)(p+1)=0;}
    }
    for(int slot=0;slot<3;++slot){
        BYTE** p=(BYTE**)(a+0x378+slot*8);BYTE* m=*p;
        if(m && !_stricmp(*(const char**)m,"S_VANPIRE")){*p=NULL;*(DWORD*)(p+1)=0;}
    }
}
static int ImMixKind(const char* name) {
    if(!name)return 0;
    if(!_stricmp(name,"S_VANPIRE"))return 1;
    if(!_stricmp(name,"ONRYOU"))return 2;
    return 0;
}
static void __fastcall ImMixSense(BYTE* ai,void*,DWORD dt) {
    ((ImMixSenseFn)(imMixModule+0x62580))(ai,dt);
    int side=0;BYTE* a=ImMixActor(ai,&side);if(!a)return;
    ImMixState& s=imMixState[side];
    if(s.actor!=a || s.ai!=ai){ZeroMemory(&s,sizeof s);s.actor=a;s.ai=ai;s.lastStarAt=-20.0f;s.lastFrame=-1;
        Log("IM_MIX_ACTOR scene=%ld tier=%ld auto=%ld side=%d actor=%p def=%s",balScene,duelDifficulty,duelAutoFight,side,a,HvDefinition(a));}
    // CAiBase::Sense stores its native float delta at +0x164. Paused fights do not advance it.
    float step=*(float*)(ai+0x164);if(_finite(step) && step>0.0f && step<5.0f)s.elapsed+=step;
    BYTE* motion=*(BYTE**)(a+0x398);int frame=motion?*(int*)(motion+0x54):-1;
    int kind=ImMixKind(HvName(a));
    if(kind && (kind!=s.lastKind || frame<s.lastFrame)) {
        if(kind==1) {
            float gap=s.elapsed-s.lastStarAt;
            if(s.stars>=2 && gap>=20.0f)s.stars=0;
            ++s.stars;s.lastStarAt=s.elapsed;s.blockedLogged=false;
            Log("IM_MIX_STAR time=%.3f side=%d count=%u gap=%.3f frame=%d mp=%d hp=%d violation=%d",
                s.elapsed,side,s.stars,gap,frame,*(int*)(a+0x55c),*(int*)(a+0x558),(int)(s.stars>2));
        } else {
            Log("IM_MIX_ONRYOU time=%.3f side=%d previous_stars=%u frame=%d mp=%d",s.elapsed,side,s.stars,frame,*(int*)(a+0x55c));
            s.stars=0;s.blockedLogged=false;
        }
    }
    s.lastKind=kind;s.lastFrame=frame;
    bool allowed=s.stars<2 || s.elapsed-s.lastStarAt>=20.0f;
    if(!allowed)ImMixWithdraw(a);
    ((HvSetFn)(imMixModule+0x30e50))(a,90,allowed && *(int*)(a+0x55c)>=10 ? 1 : 0);
    if(!allowed && !s.blockedLogged){
        Log("IM_MIX_GATE time=%.3f side=%d count=%u remaining=%.3f reason=automatic_DEF_and_AI",s.elapsed,side,s.stars,20.0f-(s.elapsed-s.lastStarAt));
        s.blockedLogged=true;
    }
}
// 11-03 v13: the asks go through AiAsk (im_onryou_vs_mm.h) - TAIZAI against Lord Byakhee becomes ONRYOU there
static bool __fastcall ImMixRequest(BYTE* a,void*,const char* name,int slot) {
    if(!imMixEnabled || !name || _stricmp(name,"S_VANPIRE"))return AiAsk(imMixModule,a,name,slot);
    int side=*(int*)(a+0x724);if(side<0 || side>1)return AiAsk(imMixModule,a,name,slot);
    ImMixState& s=imMixState[side];int checkedSide=0;
    if(s.actor!=a || !s.ai || ImMixActor(s.ai,&checkedSide)!=a || checkedSide!=side ||
        s.stars<2 || s.elapsed-s.lastStarAt>=20.0f)return AiAsk(imMixModule,a,name,slot);
    // Only the pending Star Vampire proposals are withdrawn. Native execution remains in charge.
    ImMixWithdraw(a);
    bool ready=HvFree(HvName(a)) && *(int*)(a+0x55c)>=80 && *(int*)(a+0x3cc)==0;
    if(!s.blockedLogged){
        Log("IM_MIX_BLOCK time=%.3f side=%d count=%u remaining=%.3f mp=%d motion=%s replace=%s",
            s.elapsed,side,s.stars,20.0f-(s.elapsed-s.lastStarAt),*(int*)(a+0x55c),HvName(a),ready?"ONRYOU":"native_other_or_wait");
        s.blockedLogged=true;
    }
    if(ready){
        if(slot==0)Log("IM_MIX_REPLACE time=%.3f side=%d mp=%d next=ONRYOU",s.elapsed,side,*(int*)(a+0x55c));
        return AiAsk(imMixModule,a,"ONRYOU",slot);
    }
    return false; // the original CAiBase::Request records rejection for this slot
}
static bool ImMixCall(BYTE* at,BYTE* original,void* hook,bool enable) {
    if(at[0]!=0xe8)return false;
    BYTE* target=at+5+*(LONG*)(at+1);
    if(target!=original && target!=(BYTE*)hook)return false;
    BYTE* desired=enable?(BYTE*)hook:original;if(target==desired)return true;
    BYTE patch[5]={0xe8,0,0,0,0};*(LONG*)(patch+1)=(LONG)(desired-(at+5));
    return WriteCode(at,patch,sizeof patch);
}
static void ApplyImMix(BYTE* obj,HMODULE module,LONG input) {
    imMixModule=(BYTE*)module;imMixEnabled=false;ZeroMemory(imMixState,sizeof imMixState);
    BYTE* b=imMixModule;
    bool eligible=duelBalance && duelDifficulty>=0 && duelDifficulty<=1 && balScene>=0 && balScene<1000;
    int loaded=*(int*)(obj+0x18),destination=-1;
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack523_tiberius_mix.dat",gameDir);
    if(eligible && GetFileAttributesW(pack)!=INVALID_FILE_ATTRIBUTES)
        for(unsigned i=0;i<_countof(imMixRoutes);++i)if(imMixRoutes[i].source==loaded && imMixRoutes[i].automatic==(int)duelAutoFight){destination=imMixRoutes[i].dest;break;}
    bool enabled=destination>=30000;
    imMixInput=input;imMixDestination=-1;
    const BYTE senseHead[]={0x83,0xec,0x10,0x56,0x8b,0xf1,0x8b,0x4e,0x04};
    const BYTE deltaStore[]={0x89,0x86,0x64,0x01,0x00,0x00};
    if(memcmp(b+0x62580,senseHead,sizeof senseHead) || memcmp(b+0x625a2,deltaStore,sizeof deltaStore)){
        Log("IM_MIX_DISABLED native fingerprint mismatch");return;
    }
    bool sense=ImMixCall(b+0x5d2c1,b+0x62580,(void*)ImMixSense,enabled);
    bool request=ImMixCall(b+0x6201a,b+0x351b0,(void*)ImMixRequest,enabled);
    if(!sense || !request){
        ImMixCall(b+0x5d2c1,b+0x62580,(void*)ImMixSense,false);
        ImMixCall(b+0x6201a,b+0x351b0,(void*)ImMixRequest,false);
        Log("IM_MIX_DISABLED native call identity mismatch");return;
    }
    imMixEnabled=enabled;
    if(enabled){*(int*)(obj+0x18)=destination;imMixDestination=destination;}
    Log("IM_MIX_ROUTE input=%ld source=%d destination=%d",input,loaded,destination);
    Log("IM_MIX_SCOPE enabled=%d balance=%ld tier=%ld auto=%ld scene=%ld max_stars=2 gap_seconds=20",
        (int)enabled,duelBalance,duelDifficulty,duelAutoFight,balScene);
}
