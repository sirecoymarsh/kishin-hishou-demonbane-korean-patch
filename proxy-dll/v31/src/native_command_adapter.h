// Native tactical decision trees are unchanged. This adapter owns dispatch acknowledgement.
#pragma once
#include "duel_command_arbiter.h"
#include "native_command_scope.h"
struct NcFrame { BYTE* ai; BYTE* actor; BYTE* target; bool alive; int nativeEvent; bool idle; };
struct NcState {
    BYTE* actor; DuelCommand command; char motion[80]; int action,index;
    unsigned blockedTicket; DWORD lastTrace; bool started;
};
static BYTE* ncModule;
static NativeCommandFamily ncFamily;
static bool ncEnabled;
static NcState nc;
static bool NcRead(BYTE* ai,NcFrame* f)
{
    if(!ncEnabled || !duelBalance || !duelAutoFight || DuelAiTier()!=1 ||
       GetModuleHandleW(battleModule)!=(HMODULE)ncModule)return false;
    __try {
        const DWORD vt=ncFamily==NC_KRAKEN?0xe8164:0xe80d4;
        if(*(void**)ai!=ncModule+vt)return false;
        BYTE* actor=*(BYTE**)(ai+4);
        const char* def=ncFamily==NC_KRAKEN?"PAIR506P0R1.def":"MM_QM_ANCHOR_C12.def";
        const char* targetDef=ncFamily==NC_KRAKEN?"NG_LIVE_506_AM.def":"QM_MM_NATIVE_C12.def";
        if(!actor || *(int*)(actor+0x724)!=0 || !MmDefinition(actor,def))return false;
        BYTE* manager=*(BYTE**)(ncModule+0x11e3b0);
        if(!manager)return false;
        BYTE* target=((MmTarget_t)(ncModule+0x39d60))(manager,actor);
        if(!target || *(int*)(target+0x724)!=1 || !MmDefinition(target,targetDef))return false;
        f->ai=ai;f->actor=actor;f->target=target;
        f->alive=*(int*)(actor+0x558)>0 && *(int*)(target+0x558)>0;
        f->nativeEvent=((AnParamFn)(ncModule+0x30e90))(actor,1);
        f->idle=((AnIdleFn)(ncModule+0x56c40))(ai);
        if(nc.actor!=actor) {ZeroMemory(&nc,sizeof nc);nc.actor=actor;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static const char* NcName(BYTE* motion) {const char* n=motion?*(const char**)motion:NULL;return n?n:"";}
static unsigned NcQueued(BYTE* actor,const char* name)
{
    unsigned mask=0;
    if(!name || !*name)return 0;
    // Primary body channel: direct and all five entries, not adjacent channels.
    if(!_stricmp(NcName(*(BYTE**)(actor+0x378)),name))mask|=1;
    for(int i=0;i<5;++i)
        if(!_stricmp(NcName(*(BYTE**)(actor+0x2b8+i*8)),name))mask|=2u<<i;
    return mask;
}
static void NcObserve(const NcFrame& f)
{
    if(nc.command.phase==DC_IDLE)return;
    BYTE* mo=*(BYTE**)(f.actor+0x398);const char* name=NcName(mo);
    int frame=mo?*(int*)(mo+0x54):-1;unsigned mask=NcQueued(f.actor,nc.motion);
    DuelCommandObservation o={GetTickCount(),(uintptr_t)f.target,*(int*)(f.actor+0x558),
        f.alive,f.nativeEvent!=0,!_stricmp(name,nc.motion),mask!=0,
        f.idle && AnActualIdle(name) && frame>=2};
    DuelCommandEvent e=DuelCommandObserve(nc.command,o);
    if(e==DC_STARTED)nc.started=true;
    if(e!=DC_NONE)
        Log("NATIVE_ARB_EVENT family=%d t=%lu ticket=%u event=%d phase=%d requested=%s actual=%s frame=%d queued=%u hp=%d issue_hp=%d flag=%d age=%u started=%d action=%d state=%d",
            (int)ncFamily,GetTickCount(),nc.command.ticket,(int)e,(int)nc.command.phase,nc.motion,
            name,frame,mask,o.hp,nc.command.hpAtIssue,f.nativeEvent,o.now-nc.command.issuedAt,
            (int)nc.started,*(int*)(f.ai+0x10),*(int*)(f.ai+0x1c));
    if(e==DC_RECOVERED) {
        // Release only a failed command after native recovery, without changing the native sequence.
        ((AnStateFn)(ncModule+0x61c60))(f.ai,0,0);
        *(int*)(f.ai+0x2c)=0;
    }
}
static bool NcGate(BYTE* ai)
{
    NcFrame f={};if(!NcRead(ai,&f) || !DuelCommandGatesDispatch(nc.command))return false;
    // Actor hit/grab/KO and native 64b20 postprocessing remain outside this dispatch gate.
    *(int*)(ai+0x2c)=0;
    if(nc.command.phase==DC_REQUESTED && f.idle && *(int*)(ai+0x1c)==2 &&
       nc.blockedTicket!=nc.command.ticket) {
        nc.blockedTicket=nc.command.ticket;
        Log("NATIVE_ARB_PREMATURE_FINISH_BLOCKED family=%d t=%lu ticket=%u motion=%s queued=%u",
            (int)ncFamily,GetTickCount(),nc.command.ticket,nc.motion,NcQueued(f.actor,nc.motion));
    }
    return true;
}
static void __fastcall NcRequest(BYTE* ai,void*,int index)
{
    NcFrame f={};
    if(!NcRead(ai,&f) || index<2 || index>=*(int*)(ai+0x6dc)) {
        ((AnIndexFn)(ncModule+0x63430))(ai,index);return;
    }
    if(nc.command.phase!=DC_IDLE) {
        Log("NATIVE_ARB_DUPLICATE_BLOCKED family=%d t=%lu ticket=%u old=%s index=%d phase=%d",
            (int)ncFamily,GetTickCount(),nc.command.ticket,nc.motion,index,(int)nc.command.phase);return;
    }
    // Exact name selected by the unchanged native index table (MSVC small-string layout).
    BYTE* str=ai+0x35c+index*0x1c;
    const char* name=*(unsigned*)(str+0x18)>=16?*(const char**)(str+4):(const char*)(str+4);
    if(!name || strnlen_s(name,sizeof nc.motion)>=sizeof nc.motion || !*name) {
        ((AnIndexFn)(ncModule+0x63430))(ai,index);return;
    }
    if(!DuelCommandBegin(nc.command,GetTickCount(),(uintptr_t)f.target,*(int*)(f.actor+0x558)))return;
    strcpy_s(nc.motion,name);nc.index=index;nc.action=*(int*)(ai+0x10);nc.started=false;
    ((AnIndexFn)(ncModule+0x63430))(ai,index);
    unsigned mask=NcQueued(f.actor,nc.motion);
    *(int*)(ai+0x2c)=0; // Do not let a movement output overwrite a newly accepted technique.
    Log("NATIVE_ARB_SUBMIT family=%d t=%lu ticket=%u motion=%s index=%d action=%d queued=%u hp=%d enemy_hp=%d mp=%d failure=%d",
        (int)ncFamily,GetTickCount(),nc.command.ticket,nc.motion,index,nc.action,mask,
        *(int*)(f.actor+0x558),*(int*)(f.target+0x558),*(int*)(f.actor+0x55c),(int)*(BYTE*)(ai+0x331));
    if(!mask) {
        DuelCommandFail(nc.command,DC_REJECTED);
        Log("NATIVE_ARB_REJECTED family=%d t=%lu ticket=%u motion=%s",(int)ncFamily,GetTickCount(),nc.command.ticket,nc.motion);
    }
}
static void __fastcall NcAttack(BYTE* ai,void*,int action)
{
    if(NcGate(ai))return;
    // Kraken's existing range/height/MP selection runs unchanged, including its index substitutions.
    if(ncFamily==NC_KRAKEN)KrAttackSelect(ai,NULL,action);
    else ((AnIndexFn)(ncModule+0x63cf0))(ai,action);
}
static void __fastcall NcSpecial(BYTE* ai,void*)
{
    if(NcGate(ai))return;
    ((AnPhaseFn)(ncModule+(ncFamily==NC_KRAKEN?0x60af0:0x601f0)))(ai);
}
static void __fastcall NcTick(BYTE* secondary,void*,DWORD dt,DWORD arg)
{
    BYTE* ai=secondary-0x714;NcFrame f={};bool watched=NcRead(ai,&f);
    if(watched)NcObserve(f);
    BYTE* before=watched?*(BYTE**)(f.actor+0x378):NULL;
    // Never skip native sensing, decision branches, animation execution or postprocessing.
    if(ncFamily==NC_KRAKEN)KrTickObserve(secondary,NULL,dt,arg);
    else MmTickHook(secondary,NULL,dt,arg);
    if(watched && nc.command.phase!=DC_IDLE && before!=*(BYTE**)(f.actor+0x378)) {
        Log("NATIVE_ARB_OUTPUT family=%d t=%lu ticket=%u before=%s after=%s action=%d state=%d move=%d",
            (int)ncFamily,GetTickCount(),nc.command.ticket,NcName(before),NcName(*(BYTE**)(f.actor+0x378)),
            *(int*)(ai+0x10),*(int*)(ai+0x1c),*(int*)(ai+0x2c));
    }
}
static void ApplyNativeCommand(BYTE* obj,HMODULE module)
{
    ncEnabled=false;ZeroMemory(&nc,sizeof nc);
    ncFamily=NativeCommandScope(duelBalance!=0,duelAutoFight!=0,(int)DuelAiTier(),(int)balScene,*(int*)(obj+0x18));
    if(ncFamily==NC_NONE)return;
    BYTE* b=(BYTE*)module;bool kr=ncFamily==NC_KRAKEN;
    if((kr && !krObserve) || (!kr && (!mmFixEnabled || mmNormalPlayerOnly)))return;
    void** tick=(void**)(b+(kr?0xe8158:0xe80c8));
    void** attack=(void**)(b+(kr?0xe8174:0xe80e4));
    void* oldTick=kr?(void*)KrTickObserve:(void*)MmTickHook;
    void* oldAttack=kr?(void*)KrAttackSelect:(void*)(b+0x63cf0);
    DWORD req=kr?0x60b64:0x60266,call=kr?0x60ef2:0x6078f,fn=kr?0x60af0:0x601f0;
    if((*tick!=oldTick && *tick!=(void*)NcTick) || (*attack!=oldAttack && *attack!=(void*)NcAttack) ||
       !MmRedirectCall(b+0x63d7a,b+0x63430,(void*)NcRequest,false) ||
       !MmRedirectCall(b+req,b+0x63430,(void*)NcRequest,false) ||
       !MmRedirectCall(b+call,b+fn,(void*)NcSpecial,false)) {
        Log("NATIVE_ARB_DISABLED family=%d byte_or_hook_mismatch",(int)ncFamily);return;
    }
    ncModule=b;
    if(MmRedirectCall(b+0x63d7a,b+0x63430,(void*)NcRequest,true) &&
       MmRedirectCall(b+req,b+0x63430,(void*)NcRequest,true) &&
       MmRedirectCall(b+call,b+fn,(void*)NcSpecial,true) &&
       MmSwapSlot(attack,oldAttack,(void*)NcAttack) && MmSwapSlot(tick,oldTick,(void*)NcTick)) {
        ncEnabled=true;
        Log("NATIVE_ARB_ENABLED version=9 family=%d source=%ld scene=%d tier=%ld side=0 opponent=QM tactics_preserved=1",
            (int)ncFamily,balScene,*(int*)(obj+0x18),duelDifficulty);
    }
}

// A battle can reuse the native module. Unwrap only our own pointers before
// the existing scene-specific installers evaluate their exact expected slots.
static void ResetNativeCommandHooks(HMODULE module)
{
    BYTE* b=(BYTE*)module;ncEnabled=false;
    if(!b)return;
    const DWORD ticks[2]={0xe8158,0xe80c8},attacks[2]={0xe8174,0xe80e4};
    void* oldTicks[2]={(void*)KrTickObserve,(void*)MmTickHook};
    void* oldAttacks[2]={(void*)KrAttackSelect,(void*)(b+0x63cf0)};
    for(int i=0;i<2;++i) {
        void** t=(void**)(b+ticks[i]);void** a=(void**)(b+attacks[i]);
        if(*t==(void*)NcTick)MmSwapSlot(t,(void*)NcTick,oldTicks[i]);
        if(*a==(void*)NcAttack)MmSwapSlot(a,(void*)NcAttack,oldAttacks[i]);
    }
    const DWORD calls[5]={0x63d7a,0x60b64,0x60266,0x60ef2,0x6078f};
    const DWORD originals[5]={0x63430,0x63430,0x63430,0x60af0,0x601f0};
    for(int i=0;i<5;++i) {
        BYTE* c=b+calls[i];LONG relative;memcpy(&relative,c+1,4);
        void* hook=i<3?(void*)NcRequest:(void*)NcSpecial;
        if(*c==0xe8 && c+5+relative==(BYTE*)hook)
            MmRedirectCall(c,(BYTE*)hook,b+originals[i],true);
    }
    ZeroMemory(&nc,sizeof nc);ncModule=b;ncFamily=NC_NONE;
}
