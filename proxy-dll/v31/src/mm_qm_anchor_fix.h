// MM-QM C12: native AI decisions use the same world lockOn height as its movement.
// Bound to four dedicated balanced MM/QM scenes only; native AI actors on either side.
// No actor parameters, transforms, animation, collision or projectile code writes.
typedef void (__thiscall* MmTick_t)(BYTE*, DWORD, DWORD);
typedef void (__thiscall* MmHeight_t)(BYTE*);
typedef bool (__thiscall* MmApproach_t)(BYTE*,const float*);
typedef float (__thiscall* MmDistance_t)(BYTE*);
typedef float* (__thiscall* MmWorld_t)(BYTE*);
typedef BYTE* (__thiscall* MmTarget_t)(BYTE*, BYTE*);
typedef BYTE* (__thiscall* MmFind_t)(BYTE*, const char*);
static BYTE* mmModule;
static int mmSide;
static const char* mmExpectedDefinition;
static const char* qmExpectedDefinition;
static MmTick_t mmOrigTick, qmOrigTick;
static BYTE* qmPhaseActor;
static DWORD qmPhaseStarted, qmPhaseAdvancedAt, qmTraceAt;
static MmHeight_t mmOrigHeight;
static MmApproach_t mmOrigApproach;
static MmDistance_t mmOrigDistance;
static DWORD mmApproachTraceAt;
static bool mmFixEnabled;
static bool mmNormalPlayerOnly; // normal DUEL scene522, automatic PLAYER1 only
static DWORD mmTraceAt;
struct MmAimFrame {
    BYTE* ai; BYTE* actor; BYTE* target;
    float self[16], enemy[16], muzzle[16];
    float selfRootY, targetRootY, deltaY, horizontal, nativeTolerance;
    bool ready, hasMuzzle;
};
static MmAimFrame* mmCurrent;

static bool MmDefinition(BYTE* actor, const char* suffix)
{
    BYTE* def = *(BYTE**)(actor + 0x16c);
    const char* name = def ? *(const char**)(def + 0x34) : NULL;
    if (!name) return false;
    const size_t len = strnlen_s(name, 512), tail = strlen(suffix);
    return len >= tail && len < 512 && _stricmp(name + len - tail, suffix) == 0;
}
static bool MmMeasure(BYTE* ai, MmAimFrame* out)
{
    if (!mmFixEnabled || GetModuleHandleW(battleModule) != (HMODULE)mmModule) return false;
    __try {
        if (*(void**)ai != mmModule + 0xe80d4) return false;
        BYTE* actor = *(BYTE**)(ai + 4);
        if (!actor || *(int*)(actor + 0x724) != mmSide || *(int*)(actor + 0x558) <= 0 ||
            !MmDefinition(actor, mmExpectedDefinition)) return false;
        BYTE* manager = *(BYTE**)(mmModule + 0x11e3b0);
        if (!manager) return false;
        BYTE* target = ((MmTarget_t)(mmModule + 0x39d60))(manager, actor);
        if (!target || *(int*)(target + 0x724) != 1-mmSide || *(int*)(target + 0x558) <= 0 ||
            !MmDefinition(target, qmExpectedDefinition)) return false;
        BYTE* a = *(BYTE**)(actor + 0xb84);
        BYTE* b = *(BYTE**)(target + 0xb84);
        if (!a || !b) return false;
        MmWorld_t world = (MmWorld_t)(mmModule + 0x15930);
        out->ai=ai; out->actor=actor; out->target=target;
        memcpy(out->self, world(a), sizeof out->self);
        memcpy(out->enemy, world(b), sizeof out->enemy);
        out->selfRootY=world(actor)[13]; out->targetRootY=world(target)[13];
        out->deltaY=out->enemy[13]-out->self[13];
        float dx=out->enemy[12]-out->self[12], dz=out->enemy[14]-out->self[14];
        out->horizontal=sqrtf(dx*dx+dz*dz);
        out->nativeTolerance=*(float*)(ai+0x6f4);
        if (!_finite(out->deltaY) || !_finite(out->horizontal) || out->nativeTolerance != 5.0f) return false;
        out->ready=fabsf(out->deltaY)<out->nativeTolerance;
        BYTE* node=((MmFind_t)(mmModule+0x15a00))(actor,"rifle_Null");
        out->hasMuzzle=node!=NULL;
        if (node) memcpy(out->muzzle,world(node),sizeof out->muzzle);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void __fastcall MmHeightHook(BYTE* ai, void*)
{
    const int previous=*(int*)(ai+0x1c);
    mmOrigHeight(ai);
    // Native initialization/completion/defence/request dispatch remains in charge.
    // Correct only the direction of an existing active height adjustment.
    if (mmCurrent && mmCurrent->ai==ai && !mmCurrent->ready &&
        previous==1 && *(int*)(ai+0x1c)==1) {
        *(int*)(ai+0x24)=-2;
        *(int*)(ai+0x20)=mmCurrent->deltaY>0.0f ? 6 : 7; // native UP_A / DOWN_A
    }
}
static float __fastcall MmDistanceHook(BYTE* ai,void*)
{
    if (mmCurrent && mmCurrent->ai==ai)
        return sqrtf(mmCurrent->horizontal*mmCurrent->horizontal+mmCurrent->deltaY*mmCurrent->deltaY);
    return mmOrigDistance(ai);
}
static bool __fastcall MmApproachHook(BYTE* ai,void*,const float* nativeTarget)
{
    if (!mmCurrent || mmCurrent->ai!=ai) return mmOrigApproach(ai,nativeTarget);
    // Change only the AI's local target vector. Never write an actor transform.
    const MmAimFrame& f=*mmCurrent;
    const float* root=((MmWorld_t)(mmModule+0x15930))(f.actor);
    float goal[4]={root[12]+f.enemy[12]-f.self[12],
                   root[13]+f.enemy[13]-f.self[13],
                   root[14]+f.enemy[14]-f.self[14],1.0f};
    bool done=mmOrigApproach(ai,goal);
    DWORD now=GetTickCount();
    if (now-mmApproachTraceAt>=500) {
        mmApproachTraceAt=now;
        Log("MM_APPROACH_C12 t=%lu xz=%.3f dy=%.3f goal=%.3f,%.3f,%.3f stop=%.3f req=%d/%d done=%d",
            now,f.horizontal,f.deltaY,goal[0],goal[1],goal[2],*(float*)(ai+0x708),
            *(int*)(ai+0x24),*(int*)(ai+0x20),done);
    }
    return done;
}
// Native LOAD phases normally unlock at70% and30% HP. For this DUEL pair,
// allow those same AI decision phases after15/40s, only while fully idle.
// Do not modify HP/MP, action animation, damage, or the native attack dispatcher.
static BYTE* mmPhaseActor;
static DWORD mmPhaseStarted, mmPhaseAdvancedAt;
static void MmAdvanceDuelPhase(const MmAimFrame& f)
{
    DWORD now=GetTickCount();
    if (mmPhaseActor!=f.actor) {mmPhaseActor=f.actor;mmPhaseStarted=now;mmPhaseAdvancedAt=now;}
    int phase=*(int*)(f.ai+0xc);
    DWORD elapsed=now-mmPhaseStarted;
    bool due=(phase==0 && elapsed>=15000) ||
        (phase==1 && elapsed>=40000 && now-mmPhaseAdvancedAt>=15000);
    if (!due || !f.ready || *(int*)(f.ai+0x20)!=0 || *(int*)(f.ai+0x24)!=0) return;
    BYTE* motion=*(BYTE**)(f.actor+0x398);
    const char* name=motion ? *(const char**)motion : NULL;
    if (!name || (_stricmp(name,"STOP_A") && _stricmp(name,"STOP_G") && _stricmp(name,"STOP"))) return;
    typedef bool (__thiscall* Idle_t)(BYTE*);
    if (!((Idle_t)(mmModule+0x56c40))(f.ai)) return;
    typedef void (__thiscall* Phase_t)(BYTE*,int,int);
    ((Phase_t)(mmModule+0x61c10))(f.ai,phase+1,0);
    mmPhaseAdvancedAt=now;
    Log("MM_PHASE_C12 phase=%d->%d elapsed=%lu hp=%d mp=%d idle=%.24s requests=0/0",phase,phase+1,elapsed,
        *(int*)(f.actor+0x558),*(int*)(f.actor+0x55c),name);
}
static void __fastcall MmTickHook(BYTE* secondary, void*, DWORD dt, DWORD arg)
{
    BYTE* ai=secondary-0x714;
    MmAimFrame f={};
    if (!MmMeasure(ai,&f)) { mmOrigTick(secondary,dt,arg); return; }
    if (!mmNormalPlayerOnly) MmAdvanceDuelPhase(f);
    MmAimFrame* previous=mmCurrent;
    mmCurrent=&f;
    // Feed the native root-height branch an AI-only predicate. Its original 5-unit
    // tolerance is evaluated above on current lockOn world positions instead.
    // Restore it even if native code raises; no persistent per-robot stat change.
    *(float*)(ai+0x6f4)=f.ready ? 1000000.0f : 0.0f;
    __try { mmOrigTick(secondary,dt,arg); }
    __finally { *(float*)(ai+0x6f4)=f.nativeTolerance; mmCurrent=previous; }
    DWORD now=GetTickCount();
    if (now-mmTraceAt>=250) {
        mmTraceAt=now;
        Log("MM_DISTANCE_C12 t=%lu root=%.3f lock=%.3f phase=%d",now,mmOrigDistance(ai),sqrtf(f.horizontal*f.horizontal+f.deltaY*f.deltaY),*(int*)(ai+0xc));
        BYTE* motion=*(BYTE**)(f.actor+0x398);
        const char* name=motion ? *(const char**)motion : "";
        Log("MM_AIM_C12 t=%lu roots=%.3f,%.3f lock=%.3f,%.3f dy=%.3f xz=%.3f ready=%d ai=%d/%d req=%d/%d hp=%d,%d mp=%d motion=%.24s frame=%d muzzle=%d,%.3f fwd=%.4f,%.4f,%.4f",
            now,f.selfRootY,f.targetRootY,f.self[13],f.enemy[13],f.deltaY,f.horizontal,f.ready,
            *(int*)(ai+0x10),*(int*)(ai+0x1c),*(int*)(ai+0x24),*(int*)(ai+0x20),
            *(int*)(f.actor+0x558),*(int*)(f.target+0x558),*(int*)(f.actor+0x55c),
            name ? name : "",motion ? *(int*)(motion+0x54) : -1,f.hasMuzzle,f.muzzle[13],
            f.muzzle[8],f.muzzle[9],f.muzzle[10]);
        Log("MM_WORLD_C12 t=%lu self=%.4f,%.4f,%.4f target=%.4f,%.4f,%.4f muzzle=%.4f,%.4f,%.4f right=%.4f,%.4f,%.4f up=%.4f,%.4f,%.4f forward=%.4f,%.4f,%.4f",
            now,f.self[12],f.self[13],f.self[14],f.enemy[12],f.enemy[13],f.enemy[14],
            f.muzzle[12],f.muzzle[13],f.muzzle[14],f.muzzle[0],f.muzzle[1],f.muzzle[2],
            f.muzzle[4],f.muzzle[5],f.muzzle[6],f.muzzle[8],f.muzzle[9],f.muzzle[10]);
    }
}
// MM-specific native phase1 at15s / phase3 at30s. Phase4 retains native HP gate.
static void __fastcall QmTickHook(BYTE* secondary,void*,DWORD dt,DWORD arg)
{
    BYTE* ai=secondary-0x714;
    BYTE* actor=*(BYTE**)(ai+4);
    if(!mmNormalPlayerOnly && mmFixEnabled && GetModuleHandleW(battleModule)==(HMODULE)mmModule &&
       *(void**)ai==mmModule+0xe7ba4 && actor && *(int*)(actor+0x724)==1-mmSide &&
       *(int*)(actor+0x558)>0 && MmDefinition(actor,qmExpectedDefinition)) {
        BYTE* manager=*(BYTE**)(mmModule+0x11e3b0);
        BYTE* target=manager ? ((MmTarget_t)(mmModule+0x39d60))(manager,actor) : NULL;
        if(target && *(int*)(target+0x724)==mmSide && *(int*)(target+0x558)>0 &&
           MmDefinition(target,mmExpectedDefinition)) {
            DWORD now=GetTickCount();
            if(qmPhaseActor!=actor) {qmPhaseActor=actor;qmPhaseStarted=now;qmPhaseAdvancedAt=now;}
            int phase=*(int*)(ai+0xc);DWORD elapsed=now-qmPhaseStarted;
            // Opponent-specific choice: skip the long JUUSIN1->END bind chain,
            // which C10/C11 showed mostly misses MM and resets its phase midway.
            // Use the intact native phase3 decision tree, never force a motion.
            int desired=elapsed>=30000 ? 3 : (elapsed>=15000 ? 1 : 0);
            bool due=phase<desired && now-qmPhaseAdvancedAt>=10000;
            BYTE* motion=*(BYTE**)(actor+0x398);
            const char* name=motion ? *(const char**)motion : NULL;
            typedef bool (__thiscall* Idle_t)(BYTE*);
            if(due && *(int*)(ai+0x20)==0 && *(int*)(ai+0x24)==0 && name &&
               (!_stricmp(name,"STOP_G") || !_stricmp(name,"STOP")) &&
               ((Idle_t)(mmModule+0x56c40))(ai)) {
                typedef void (__thiscall* Phase_t)(BYTE*,int,int);
                ((Phase_t)(mmModule+0x61c10))(ai,desired,0);
                qmPhaseAdvancedAt=now;
                Log("QM_PHASE_C12 phase=%d->%d elapsed=%lu hp=%d idle=%.24s",phase,desired,elapsed,*(int*)(actor+0x558),name);
            }
        }
    }
    qmOrigTick(secondary,dt,arg);
    DWORD now=GetTickCount();
    if(mmFixEnabled && actor==qmPhaseActor && now-qmTraceAt>=250) {
        qmTraceAt=now;
        BYTE* motion=*(BYTE**)(actor+0x398);
        const char* name=motion ? *(const char**)motion : "";
        Log("QM_STATE_C12 t=%lu phase=%d action=%d sequence=%d substate=%d req=%d/%d hp=%d mp=%d motion=%.24s frame=%d",
            now,*(int*)(ai+0xc),*(int*)(ai+0x10),*(int*)(ai+0x14),*(int*)(ai+0x1c),
            *(int*)(ai+0x24),*(int*)(ai+0x20),*(int*)(actor+0x558),*(int*)(actor+0x55c),
            name ? name : "",motion ? *(int*)(motion+0x54) : -1);
    }
}
static bool MmSwapSlot(void** slot, void* expected, void* hook)
{
    if (*slot==hook) return true;
    if (*slot!=expected) return false;
    DWORD old;
    if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) return false;
    InterlockedExchangePointer(slot,hook);
    DWORD unused; VirtualProtect(slot,sizeof(void*),old,&unused);
    return true;
}
// Relocate existing CALL operands only. The opcode and instruction boundary
// remain native; the called function is compiled C++. No trampoline/code cave.
static bool MmRedirectCall(BYTE* call,BYTE* expected,void* hook,bool write)
{
    if (*call!=0xe8) return false;
    LONG relative;memcpy(&relative,call+1,sizeof relative);
    BYTE* target=call+5+relative;
    if (target!=expected && target!=(BYTE*)hook) return false;
    if (!write || target==(BYTE*)hook) return true;
    relative=(LONG)((BYTE*)hook-(call+5));
    DWORD old;
    if (!VirtualProtect(call+1,sizeof relative,PAGE_EXECUTE_READWRITE,&old)) return false;
    memcpy(call+1,&relative,sizeof relative);
    DWORD unused;VirtualProtect(call+1,sizeof relative,old,&unused);
    FlushInstructionCache(GetCurrentProcess(),call,5);
    return true;
}
static void InstallMmAimFix(BYTE* obj, HMODULE module)
{
    mmFixEnabled=false; mmNormalPlayerOnly=false; mmCurrent=NULL; mmPhaseActor=NULL; qmPhaseActor=NULL;
    const int loaded=*(int*)(obj+0x18);
    // Latest user request: repair the reported balance-off player-side MM stall.
    // This is not general opponent specialization; preserve native phases/techniques.
    if (!duelBalance && loaded==522 && duelAutoFight) {
        mmNormalPlayerOnly=true;
        mmSide=0; mmExpectedDefinition="PAIR522P0R1.def"; qmExpectedDefinition="PAIR522P1R1.def";
    } else {
    if (mmQmRoutedTo<0 || mmQmRoutedTo!=loaded) return;   // 11-05 18:5x: sent here this fight (balance on or off)
    switch (loaded) {
    case 5923: mmSide=0; mmExpectedDefinition="MM_QM_ANCHOR_C12.def"; qmExpectedDefinition="QM_MM_NATIVE_C12.def"; break;
    case 5924: mmSide=1; mmExpectedDefinition="MM_QM_P1_C12.def"; qmExpectedDefinition="QM_MM_P0_C12.def"; break;
    case 5925: mmSide=0; mmExpectedDefinition="MM_QM_MANUAL_C12.def"; qmExpectedDefinition="QM_MM_AI_C12.def"; break;
    case 5926: mmSide=1; mmExpectedDefinition="MM_QM_AI_C12.def"; qmExpectedDefinition="QM_MM_MANUAL_C12.def"; break;
    default: return;
    }
    }
    BYTE* b=(BYTE*)module;
    const BYTE tickHead[]={0x8b,0x44,0x24,0x04,0x81,0xec,0x98,0x00,0x00,0x00};
    const BYTE heightHead[]={0x83,0xec,0x64,0x53,0x56,0x8b,0xd9};
    const BYTE qmHead[]={0x8b,0x44,0x24,0x04,0x56,0x57,0x8b,0xf9};
    const BYTE phaseHead[]={0x56,0x8b,0x74,0x24,0x08,0x85,0xf6};
    if (memcmp(b+0x5a5b0,qmHead,sizeof qmHead) || memcmp(b+0x61c10,phaseHead,sizeof phaseHead) || memcmp(b+0x60290,tickHead,sizeof tickHead) || memcmp(b+0x62150,heightHead,sizeof heightHead)) {
        Log("MM_AIM_C12 DISABLED native bytes mismatch"); return;
    }
    void** tick=(void**)(b+0xe80c8);
    void** qtick=(void**)(b+0xe7b98);
    void** height=(void**)(b+0xe80e8);
    void** approach=(void**)(b+0xe8110);
    if ((*qtick!=b+0x5a5b0 && *qtick!=(void*)QmTickHook) || (*tick!=b+0x60290 && *tick!=(void*)MmTickHook) ||
        (*height!=b+0x62150 && *height!=(void*)MmHeightHook) ||
        (*approach!=b+0x622a0 && *approach!=(void*)MmApproachHook) ||
        !MmRedirectCall(b+0x60341,b+0x62540,(void*)MmDistanceHook,false) ||
        !MmRedirectCall(b+0x5ff7d,b+0x62540,(void*)MmDistanceHook,false)) {
        Log("MM_AIM_C12 DISABLED vtable mismatch"); return;
    }
    qmOrigTick=(MmTick_t)(b+0x5a5b0);
    mmModule=b; mmOrigTick=(MmTick_t)(b+0x60290); mmOrigHeight=(MmHeight_t)(b+0x62150);
    mmOrigApproach=(MmApproach_t)(b+0x622a0);mmOrigDistance=(MmDistance_t)(b+0x62540);
    mmTraceAt=0;mmApproachTraceAt=0;
    if ((mmNormalPlayerOnly || MmSwapSlot(qtick,b+0x5a5b0,(void*)QmTickHook)) && MmSwapSlot(approach,b+0x622a0,(void*)MmApproachHook) &&
        MmRedirectCall(b+0x60341,b+0x62540,(void*)MmDistanceHook,true) &&
        MmRedirectCall(b+0x5ff7d,b+0x62540,(void*)MmDistanceHook,true) &&
        MmSwapSlot(height,b+0x62150,(void*)MmHeightHook) && MmSwapSlot(tick,b+0x60290,(void*)MmTickHook)) {
        mmFixEnabled=true;
        if (mmNormalPlayerOnly) Log("MM_PLAYER_NORMAL ENABLED scene=522 side=0 enemy=QM native_phases_preserved=1");
        Log("MM_AIM_C12 ENABLED scene=%d MM_side=%d opponent=QM base=%p tick=%p height=%p",loaded,mmSide,b,*tick,*height);
    } else Log("MM_AIM_C12 DISABLED protection failure");
}

