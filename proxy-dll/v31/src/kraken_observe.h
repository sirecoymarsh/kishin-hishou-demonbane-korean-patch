#pragma once
static BYTE* krModule;
static MmTick_t krOriginal;
static bool krObserve;
static DWORD krLastLog;
typedef void (__thiscall* KrAttack_t)(BYTE*,int);
static KrAttack_t krAttackOriginal;
static bool krSelectionEnabled;
static void __fastcall KrAttackSelect(BYTE* ai,void*,int action)
{
    // Scope: balanced Normal automatic PLAYER1 Kraken -> Nero, source506/scene7329.
    // Change a native request only before dispatch. Keep every active technique,
    // genuine bind/grab, hit reaction, completion and native MP gate intact.
    __try {
        if(krSelectionEnabled && krObserve && duelBalance && duelAutoFight && DuelAiTier()==1 &&
           GetModuleHandleW(battleModule)==(HMODULE)krModule &&
           *(void**)ai==krModule+0xe8164 && *(int*)(ai+0x1c)==1 &&
           *(int*)(ai+0x10)==action) {
            BYTE* actor=*(BYTE**)(ai+4);
            BYTE* manager=*(BYTE**)(krModule+0x11e3b0);
            BYTE* target=(actor && manager)?((MmTarget_t)(krModule+0x39d60))(manager,actor):NULL;
            BYTE* motion=actor?*(BYTE**)(actor+0x398):NULL;
            const char* name=motion?*(const char**)motion:NULL;
            if(actor && target && name && *(int*)(actor+0x724)==0 && *(int*)(target+0x724)==1 &&
               *(int*)(actor+0x558)>0 && *(int*)(target+0x558)>0 &&
               MmDefinition(actor,"PAIR506P0R1.def") && MmDefinition(target,"NG_LIVE_506_AM.def") &&
               (!_stricmp(name,"STOP_G") || !_stricmp(name,"STOP"))) {
                MmWorld_t world=(MmWorld_t)(krModule+0x15930);
                float self[16],enemy[16];memcpy(self,world(actor),sizeof self);memcpy(enemy,world(target),sizeof enemy);
                float dx=enemy[12]-self[12],dy=enemy[13]-self[13],dz=enemy[14]-self[14];
                float distance=sqrtf(dx*dx+dy*dy+dz*dz);
                int mp=*(int*)(actor+0x55c),selected=action;
                if(_finite(distance) && _finite(dy) && fabsf(dy)>80.0f && distance<=600.0f) {
                    // Observed COMBO1 missed four times at root delta120-141.
                    // ICE and 9HeadDragon already caused HP loss in the baseline.
                    // Native COMBO1 was selected at the arena edge and missed.
                    // R2 ICE alone delayed the fight. Use the observed effective native
                    // 9HeadDragon with ample MP first; keep ICE as the lower-MP choice.
                    // This changes decisions, not skill geometry, power, cost or phase data.
                    if(action==5 && mp>=50) selected=8;
                    else if(action==5 && mp>=30) selected=6;
                    else if(action==2 && mp>=50 && *(BYTE*)(actor+0xb91)==0) selected=8;
                }
                if(selected!=action) {
                    Log("KRAKEN_SELECT_R3 side=0 original=%d selected=%d mp=%d dy=%.2f d3=%.2f state=1 idle=%.20s",
                        action,selected,mp,dy,distance,name);
                    *(int*)(ai+0x10)=selected;action=selected;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("KRAKEN_SELECT_R3 guard unavailable; native request retained");}
    krAttackOriginal(ai,action);
}

static MmApproach_t krApproachOriginal;
static bool krApproachEnabled;
static bool __fastcall KrApproachFinish(BYTE* ai,void*,const float* nativeTarget)
{
    __try {
        if(krApproachEnabled && krObserve && duelBalance && duelAutoFight && DuelAiTier()==1 &&
           GetModuleHandleW(battleModule)==(HMODULE)krModule && *(void**)ai==krModule+0xe8164 &&
           *(int*)(ai+0x10)==1) {
            BYTE* actor=*(BYTE**)(ai+4);BYTE* manager=*(BYTE**)(krModule+0x11e3b0);
            BYTE* target=actor && manager?((MmTarget_t)(krModule+0x39d60))(manager,actor):NULL;
            BYTE* motion=actor?*(BYTE**)(actor+0x398):NULL;const char* name=motion?*(const char**)motion:NULL;
            if(actor && target && name && *(int*)(actor+0x724)==0 && *(int*)(target+0x724)==1 &&
               *(int*)(actor+0x558)>0 && *(int*)(target+0x558)>0 && *(int*)(actor+0x55c)>=30 &&
               MmDefinition(actor,"PAIR506P0R1.def") && MmDefinition(target,"NG_LIVE_506_AM.def") &&
               (!_stricmp(name,"FORWARD_G") || !_stricmp(name,"FORWARD_B") ||
                !_stricmp(name,"STOP_G") || !_stricmp(name,"STOP"))) {
                MmWorld_t world=(MmWorld_t)(krModule+0x15930);float a[16],b[16];
                memcpy(a,world(actor),sizeof a);memcpy(b,world(target),sizeof b);
                float dx=b[12]-a[12],dy=b[13]-a[13],dz=b[14]-a[14];float d=sqrtf(dx*dx+dy*dy+dz*dz);
                if(_finite(d) && _finite(dy) && fabsf(dy)>80.0f && d<=300.0f) {
                    Log("KRAKEN_EFF_APPROACH_DONE d3=%.2f dy=%.2f mp=%d motion=%s native_state=%d",d,dy,*(int*)(actor+0x55c),name,*(int*)(ai+0x1c));
                    // Original 0x609d0 consumes true, resets movement and resumes its original selection.
                    // No attack/actor position/frame/MP writes. Existing R3 selection chooses a reachable attack.
                    return true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("KRAKEN_EFF approach guard unavailable; native path retained");}
    return krApproachOriginal(ai,nativeTarget);
}
static void __fastcall KrTickObserve(BYTE* secondary,void*,DWORD dt,DWORD arg)
{
    krOriginal(secondary,dt,arg);
    if(!krObserve || !duelBalance || !duelAutoFight || GetModuleHandleW(battleModule)!=(HMODULE)krModule)return;
    __try {
        BYTE* ai=secondary-0x714;BYTE* actor=*(BYTE**)(ai+4);
        if(!actor || *(int*)(actor+0x724)!=0 || !MmDefinition(actor,"PAIR506P0R1.def"))return;
        BYTE* manager=*(BYTE**)(krModule+0x11e3b0);if(!manager)return;
        BYTE* target=((MmTarget_t)(krModule+0x39d60))(manager,actor);
        if(!target || !MmDefinition(target,"NG_LIVE_506_AM.def"))return;
        DWORD now=GetTickCount();if(now-krLastLog<250)return;krLastLog=now;
        MmWorld_t world=(MmWorld_t)(krModule+0x15930);float a[16],b[16];memcpy(a,world(actor),sizeof a);memcpy(b,world(target),sizeof b);
        BYTE* motion=*(BYTE**)(actor+0x398);const char* name=motion?*(const char**)motion:NULL;
        float dx=b[12]-a[12],dy=b[13]-a[13],dz=b[14]-a[14];
        Log("KRAKEN_STATE t=%lu side=0 phase=%d action=%d state=%d turn=%d mp_band=%d boundary=%d boundary_next=%d req=%d/%d/%d enabled=%d hp=%d enemy_hp=%d mp=%d motion=%.32s frame=%d self=%.2f,%.2f,%.2f target=%.2f,%.2f,%.2f d3=%.2f xz=%.2f",
            now,*(int*)(ai+0xc),*(int*)(ai+0x10),*(int*)(ai+0x1c),*(int*)(ai+0x14),*(int*)(ai+0x28c),(int)*(BYTE*)(actor+0xb91),(int)*(BYTE*)(actor+0xbd4),
            *(int*)(ai+0x24),*(int*)(ai+0x20),*(int*)(ai+0x28),*(int*)(ai+0x2c),*(int*)(actor+0x558),*(int*)(target+0x558),*(int*)(actor+0x55c),name?name:"",motion?*(int*)(motion+0x54):-1,
            a[12],a[13],a[14],b[12],b[13],b[14],sqrtf(dx*dx+dy*dy+dz*dz),sqrtf(dx*dx+dz*dz));
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("KRAKEN_OBSERVE read unavailable");}
}
static void ApplyKrakenObserve(BYTE* obj,HMODULE module)
{
    krObserve=false;krSelectionEnabled=false;krApproachEnabled=false;krLastLog=0;
    if(!duelBalance || !duelAutoFight || DuelAiTier()!=1 || balScene!=506 || *(int*)(obj+0x18)!=7329)return;
    krModule=(BYTE*)module;krOriginal=(MmTick_t)(krModule+0x60b90);
    if(MmSwapSlot((void**)(krModule+0xe8158),krModule+0x60b90,(void*)KrTickObserve)) {
        krObserve=true;krAttackOriginal=(KrAttack_t)(krModule+0x63cf0);
        // 11-08 user 「R3 꺼 줘」: since v13 lifts Kraken to Nero's lock node, the root height gap R3 tests (> 80) is always there
        // (-285), so R3 swapped COMBO1 for 9HeadDragon from the first seconds at full HP (x3 damage since 11-03: Nero 1500 -> 747
        // in one). Off unless autobattle.ini kraken_select=1 - the native AI keeps its own 9HeadDragon timing (HP 16..25%).
        wchar_t krIni[MAX_PATH];
        swprintf_s(krIni,L"%s\\autobattle.ini",abDir);
        const bool krWantSelect=GetPrivateProfileIntW(L"autobattle",L"kraken_select",0,krIni)!=0;
        krSelectionEnabled=krWantSelect && MmSwapSlot((void**)(krModule+0xe8174),krModule+0x63cf0,(void*)KrAttackSelect);
        krApproachOriginal=(MmApproach_t)(krModule+0x622a0);
        krApproachEnabled=MmSwapSlot((void**)(krModule+0xe81a0),krModule+0x622a0,(void*)KrApproachFinish);
        Log("KRAKEN_EFF_R4 approach=%d native300range completion only",(int)krApproachEnabled);
        Log("KRAKEN_SELECT_R3 enabled=%d (kraken_select=%d, default off since 11-08); source506 scene7329 PLAYER1; native execution unchanged",
            (int)krSelectionEnabled,(int)krWantSelect);
    }
}
