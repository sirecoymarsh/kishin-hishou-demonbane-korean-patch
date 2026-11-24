// Exact private DEF + opponent + scene + side guards; original execution preserved.
#pragma once
#include "aion_scope.h"
#include "aion_cannon_phase.h"
#include "aion_enemy_cannon_range.h"
#include "aion_post_cannon.h"
#include "aion_install_scope.h"
typedef void (__thiscall* AnPhaseFn)(BYTE*);
typedef void (__thiscall* AnIndexFn)(BYTE*,int);
typedef void (__thiscall* AnMoveFn)(BYTE*,int);
typedef int (__thiscall* AnParamFn)(BYTE*,int);
typedef bool (__thiscall* AnIdleFn)(BYTE*);
typedef void (__thiscall* AnNameFn)(BYTE*,const char*);
typedef void (__thiscall* AnStateFn)(BYTE*,int,int);
static BYTE* anModule;
static const AionRoute* anRoute;
static bool anEnabled;
enum AnMode { AN_NONE,AN_ATTACK,AN_APPROACH,AN_RECOVERY,AN_SETTLING };
struct AnState { BYTE* actor; AnMode mode; AionChoice choice; const char* pending; unsigned sequence; int reserve; float stopRange; DWORD began,lastTrace,lastMoveTrace,groundChoiceAt; const char* groundChoice; AionPostCycle post; };
static AnState anState[2];
struct AnFrame { BYTE* ai;BYTE* actor;BYTE* target;int side;const AionActorRoute* route;float self[16],enemy[16];AionSense sense; };
static bool AnRead(BYTE* ai,AnFrame* f)
{
    if(!anEnabled || !anRoute || !duelBalance || GetModuleHandleW(battleModule)!=(HMODULE)anModule)return false;
    __try {
        if(*(void**)ai!=anModule+0xe81d4)return false;
        BYTE* actor=*(BYTE**)(ai+4);if(!actor)return false;
        int side=*(int*)(actor+0x724);if(side<0 || side>1)return false;
        const AionActorRoute* r=&anRoute->actor[side];
        if(!r->definition || !MmDefinition(actor,r->definition))return false;
        BYTE* manager=*(BYTE**)(anModule+0x11e3b0);if(!manager)return false;
        BYTE* target=((MmTarget_t)(anModule+0x39d60))(manager,actor);
        if(!target || *(int*)(target+0x724)!=1-side || !MmDefinition(target,r->targetDefinition))return false;
        MmWorld_t world=(MmWorld_t)(anModule+0x15930);
        memcpy(f->self,world(actor),sizeof f->self);memcpy(f->enemy,world(target),sizeof f->enemy);
        BYTE* sn=*(BYTE**)(actor+0xb84);BYTE* tn=*(BYTE**)(target+0xb84);
        if(!sn || !tn)return false;
        float sy=world(sn)[13],ty=world(tn)[13];
        float dx=f->enemy[12]-f->self[12],dz=f->enemy[14]-f->self[14];
        AnParamFn get=(AnParamFn)(anModule+0x30e90);
        f->ai=ai;f->actor=actor;f->target=target;f->side=side;f->route=r;
        f->sense.alive=get(actor,100)>0 && get(target,100)>0;
        f->sense.eventFlag=get(actor,1);f->sense.weapon=get(actor,41);f->sense.cannon=get(actor,44);f->sense.mp=get(actor,101);
        f->sense.tier=anRoute->tier;f->sense.distance=sqrtf(dx*dx+dz*dz);f->sense.height=ty-sy;
        f->sense.idle=((AnIdleFn)(anModule+0x56c40))(ai);
        return _finite(f->sense.distance) && _finite(f->sense.height);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static AnState& AnFor(const AnFrame& f)
{
    AnState& s=anState[f.side];
    if(s.actor!=f.actor) {ZeroMemory(&s,sizeof s);s.actor=f.actor;}
    return s;
}
static void AnReset(BYTE* ai) {((AnPhaseFn)(anModule+0x61ce0))(ai);}
static void AnStateSet(BYTE* ai,int action,int substate) {((AnStateFn)(anModule+0x61c60))(ai,action,substate);}
static void AnName(BYTE* ai,const char* name) {((AnNameFn)(anModule+0x62000))(ai,name);}
#include "aion_post_cannon_native.h"
static void AnDecide(BYTE* ai,DWORD original)
{
    AnFrame f={};
    if(!AnRead(ai,&f)) {((AnPhaseFn)(anModule+original))(ai);return;}
    AnState& s=AnFor(f);f.sense.sequence=s.sequence;
    const bool cannonScope=AionInstallScope(duelBalance!=0,(int)DuelAiTier(),duelAutoFight?1:0,balScene,anRoute,f.side);
    if(cannonScope)AnPostObserve(f,s);
    DWORD now=GetTickCount();
    if(now-s.lastTrace>=250) {
        s.lastTrace=now;BYTE* motion=*(BYTE**)(f.actor+0x398);
        const char* name=motion?*(const char**)motion:NULL;
        Log("AION_STATE t=%lu scene=%d side=%d phase=%d action=%d state=%d req=%d/%d hp=%d enemy_hp=%d mp=%d weapon=%d cannon=%d motion=%.32s frame=%d rootY=%.2f enemyY=%.2f xz=%.2f lockDy=%.2f mode=%d",
            now,anRoute->destination,f.side,*(int*)(ai+0xc),*(int*)(ai+0x10),*(int*)(ai+0x1c),*(int*)(ai+0x24),*(int*)(ai+0x20),
            *(int*)(f.actor+0x558),*(int*)(f.target+0x558),f.sense.mp,f.sense.weapon,f.sense.cannon,
            name?name:"",motion?*(int*)(motion+0x54):-1,f.self[13],f.enemy[13],f.sense.distance,f.sense.height,(int)s.mode);
    }
    // A live technique / native hit response owns its full lifecycle.
    if(*(int*)(ai+0x1c)!=0 || !f.sense.idle || !f.sense.alive || f.sense.eventFlag!=0)return;
    AionDecision d=cannonScope ? AionCannonPhaseChoose(f.route->policy,f.sense,*(int*)(f.actor+0x558)) : AionChoose(f.route->policy,f.sense);
    if(cannonScope) {
        d=AionEnemyCannonRange(anRoute,f.side,f.sense,*(int*)(f.actor+0x558),d);
        if(AnPostEligible(f,s) && !AnPostGroundSafe(f,AnPostMotion(f)))return;
        d=AionPostChoose(anRoute,f.side,s.post,f.sense,*(int*)(f.actor+0x558),d);
    }
    if(d.choice==AION_HOLD)return;
    AnReset(ai);s.choice=d.choice;s.pending=AionMotion(d.choice);s.began=GetTickCount();s.groundChoice=NULL;
    if(s.pending) {s.mode=AN_ATTACK;AnStateSet(ai,9,0);}
    else {
        s.mode=d.choice==AION_MOVE ? AN_APPROACH : AN_RECOVERY;
        s.reserve=d.reserve;s.stopRange=d.approachRange;AnStateSet(ai,1,1); // Native action 1 dispatches 0x61930; action 0 only runs postprocessing.
        *(int*)(ai+0x2c)=0; // AI request enable only, not an actor parameter.
    }
    Log("AION_DECIDE scene=%d tier=%d source=%d side=%d opponent=%s choice=%d motion=%s mp=%d weapon=%d cannon=%d xz=%.2f dy=%.2f sequence=%u",
        anRoute->destination,anRoute->tier,anRoute->source,f.side,f.route->opponent,(int)d.choice,s.pending?s.pending:"wait/move",
        f.sense.mp,f.sense.weapon,f.sense.cannon,f.sense.distance,f.sense.height,s.sequence);
}
static void __fastcall AnPhase0(BYTE* ai,void*) {AnDecide(ai,0x61130);}
static void __fastcall AnPhase1(BYTE* ai,void*) {AnDecide(ai,0x612b0);}
static void __fastcall AnPhase2(BYTE* ai,void*) {AnDecide(ai,0x614e0);}
static void __fastcall AnAttackRequest(BYTE* ai,void*,int index)
{
    AnFrame f={};
    if(index==9 && AnRead(ai,&f)) {
        AnState& s=AnFor(f);
        if(s.mode==AN_ATTACK && s.pending && f.sense.idle && f.sense.alive && f.sense.eventFlag==0) {
            if(!AionCanStart(s.choice,f.route->policy,f.sense)) {
                Log("AION_DEFER side=%d choice=%d mp=%d weapon=%d; dispatch gate changed",f.side,(int)s.choice,f.sense.mp,f.sense.weapon);
                s.pending=NULL;s.mode=AN_NONE;return;
            }
            const char* name=s.pending;s.pending=NULL;AnName(ai,name);++s.sequence;
            Log("AION_REQUEST side=%d motion=%s native_request_failed=%d",f.side,name,(int)*(BYTE*)(ai+0x331));
            return;
        }
        // Do not turn a stale private decision into the borrowed B_SWORD action.
        return;
    }
    ((AnIndexFn)(anModule+0x63430))(ai,index);
}
static bool AnMovable(BYTE* actor,bool idle)
{
    if(idle)return true;
    BYTE* motion=*(BYTE**)(actor+0x398);const char* n=motion?*(const char**)motion:NULL;
    if(!n)return false;
    return !_stricmp(n,"FORWARD") || !_stricmp(n,"FORWARD_G") || !_stricmp(n,"BACK") ||
        !_stricmp(n,"BACK_G") || !_stricmp(n,"LEFT") || !_stricmp(n,"RIGHT") ||
        !_stricmp(n,"LEFT_G") || !_stricmp(n,"RIGHT_G") || !_stricmp(n,"DASH_G") ||
        !_stricmp(n,"RUN") || !_stricmp(n,"TURN_L") || !_stricmp(n,"TURN_R");
}
// R3: only balanced normal automatic PLAYER1 Aion -> Nero.
// Native 0x61790 chooses jump when goal distance > 400 and random < .25.
// This match needs ground approach; native 0x64b20 hit/KO/resource handling remains intact.
static bool AnQmGroundScope(const AnFrame& f)
{
    return anRoute && anRoute->tier==1 && anRoute->control==1 &&
        anRoute->source==714 && anRoute->destination==9083 && f.side==0 &&
        !_stricmp(f.route->opponent,"QM");
}
static bool AnQmGroundMovement(BYTE* ai,const AnFrame& f,AnState& s)
{
    if(!AnQmGroundScope(f) || (s.mode!=AN_APPROACH && s.mode!=AN_RECOVERY && s.mode!=AN_SETTLING))return false;
    if(!f.sense.alive || f.sense.eventFlag!=0)return true;
    BYTE* motion=*(BYTE**)(f.actor+0x398);const char* name=motion?*(const char**)motion:NULL;
    if(!name)return true;
    const bool locomotion=!_stricmp(name,"FORWARD_G") || !_stricmp(name,"BACK_G") ||
        !_stricmp(name,"LMOVE_G") || !_stricmp(name,"RMOVE_G");
    // Jump/landing, hit, scripted transitions and live techniques keep ownership.
    if(!f.sense.idle && !locomotion)return true;
    *(int*)(ai+0x20)=0;*(int*)(ai+0x24)=0;*(int*)(ai+0x28)=0;*(int*)(ai+0x2c)=0;
    if(s.mode==AN_SETTLING) {
        if(f.sense.idle) {s.mode=AN_NONE;s.groundChoice=NULL;AnStateSet(ai,0,0);}
        return true;
    }
    DWORD now=GetTickCount();
    const bool done=s.mode==AN_RECOVERY ? f.sense.mp>=s.reserve : f.sense.distance<=s.stopRange;
    if(done || now-s.began>=6000) {
        AnReset(ai);*(int*)(ai+0x2c)=0;s.mode=AN_SETTLING;s.groundChoice=NULL;
        Log("AION_GROUND_SETTLE side=%d completed=%d mp=%d xz=%.2f motion=%s",f.side,(int)done,f.sense.mp,f.sense.distance,name);
        if(f.sense.idle) {s.mode=AN_NONE;AnStateSet(ai,0,0);}
        else AnName(ai,"STOP");
        return true;
    }
    if(!s.groundChoice || now-s.groundChoiceAt>=1000) {
        s.groundChoice=s.mode==AN_APPROACH || f.sense.distance>820.0f ? "FORWARD_G" :
            f.sense.distance<580.0f ? "BACK_G" : ((s.sequence&1) ? "LMOVE_G" : "RMOVE_G");
        s.groundChoiceAt=now;
    }
    if(_stricmp(name,s.groundChoice)) {
        AnName(ai,s.groundChoice);
        Log("AION_GROUND_REQUEST side=%d mode=%d motion=%s previous=%s xz=%.2f dy=%.2f mp=%d reserve=%d",f.side,(int)s.mode,s.groundChoice,name,f.sense.distance,f.sense.height,f.sense.mp,s.reserve);
    }
    return true;
}

static void __fastcall AnMovement(BYTE* ai,void*,int phase)
{
    AnFrame f={};
    if(!AnRead(ai,&f)) {((AnMoveFn)(anModule+0x61930))(ai,phase);return;}
    AnState& s=AnFor(f);
    if(s.mode==AN_NONE) {((AnMoveFn)(anModule+0x61930))(ai,phase);return;}
    const bool cannonScope=AionInstallScope(duelBalance!=0,(int)DuelAiTier(),duelAutoFight?1:0,balScene,anRoute,f.side);
    if(cannonScope && AnPostMovement(ai,f,s))return;
    if(AnQmGroundMovement(ai,f,s))return;
    if(!f.sense.alive || f.sense.eventFlag!=0 || !AnMovable(f.actor,f.sense.idle))return;
    if(s.mode==AN_RECOVERY) {
        // Preserve STOP's native frame progression/MP recovery, never reissue STOP each tick.
        *(int*)(ai+0x2c)=0;
        const int hp=*(int*)(f.actor+0x558);
        const bool banking=cannonScope && s.reserve==100 && hp>400 && hp<=600;
        if(f.sense.idle && f.sense.mp>=s.reserve && !banking) {s.mode=AN_NONE;AnStateSet(ai,0,0);}
        return;
    }
    if(s.mode==AN_SETTLING) {
        *(int*)(ai+0x2c)=0;
        if(f.sense.idle) {s.mode=AN_NONE;AnStateSet(ai,0,0);}
        return;
    }
    if(s.mode!=AN_APPROACH) {if(f.sense.idle)AnStateSet(ai,0,0);return;}
    if(f.sense.distance<=s.stopRange || GetTickCount()-s.began>=2000) {
        AnReset(ai);*(int*)(ai+0x2c)=0;s.mode=AN_SETTLING;
        if(f.sense.idle) {s.mode=AN_NONE;AnStateSet(ai,0,0);}
        else AnName(ai,"STOP");
        return;
    }
    const float ratio=(f.sense.distance-s.stopRange)/f.sense.distance;
    float goal[4]={f.self[12]+(f.enemy[12]-f.self[12])*ratio,f.self[13],
                   f.self[14]+(f.enemy[14]-f.self[14])*ratio,1.0f};
    // Only the AI goal changes. Native movement still writes its normal requests.
    ((MmApproach_t)(anModule+0x61790))(ai,goal);
    DWORD now=GetTickCount();
    if(now-s.lastMoveTrace>=500) {
        s.lastMoveTrace=now;
        Log("AION_MOVE side=%d xz=%.2f goal=%.2f,%.2f,%.2f req=%d/%d/%d enabled=%d",f.side,f.sense.distance,goal[0],goal[1],goal[2],*(int*)(ai+0x24),*(int*)(ai+0x20),*(int*)(ai+0x28),*(int*)(ai+0x2c));
    }
}
#include "aion_cannon_aim.h"
static bool InstallAion(BYTE* module,const AionRoute* route)
{
    anEnabled=false;anRoute=NULL;ZeroMemory(anState,sizeof anState);anModule=module;ZeroMemory(acPose,sizeof acPose);
    if(!route)return false;
    if(!AionNativeBytesMatch(module)) {Log("AION_DISABLED native function fingerprint mismatch");return false;}
    struct Call {DWORD at,to;void* hook;};
    const Call calls[]={{0x61b2d,0x61130,(void*)AnPhase0},{0x61b07,0x612b0,(void*)AnPhase1},
        {0x61af6,0x614e0,(void*)AnPhase2},{0x63d7a,0x63430,(void*)AnAttackRequest},
        {0x61b70,0x61930,(void*)AnMovement},
        {0x3930a,0x35ba0,(void*)AcBeforePose},{0x393df,0x38d00,(void*)AcBeforeMaterials}};
    const size_t callCount=AionInstallScope(true,route->tier,route->control,route->source,route,1) ? _countof(calls) : _countof(calls)-2;
    for(size_t i=0;i<callCount;++i)
        if(!MmRedirectCall(module+calls[i].at,module+calls[i].to,calls[i].hook,false))return false;
    for(size_t i=0;i<callCount;++i)
        if(!MmRedirectCall(module+calls[i].at,module+calls[i].to,calls[i].hook,true))return false;
    anRoute=route;anEnabled=true;
    Log("AION_ENABLED scene=%d source=%d tier=%d control=%d; original post/physics/stats preserved",route->destination,route->source,route->tier,route->control);
    return true;
}
static void ApplyAionScene(BYTE* obj,HMODULE module)
{
    // Reset scope even if the next fight is story, manual, balance-off, or unrelated.
    anEnabled=false;anRoute=NULL;ZeroMemory(anState,sizeof anState);anModule=(BYTE*)module;ZeroMemory(acPose,sizeof acPose);
    const AionRoute* r=FindAionRoute(duelBalance!=0,(int)DuelAiTier(),duelAutoFight?1:0,balScene,*(int*)(obj+0x18));
    if(!r)return;
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack512_aion_native_ai.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES)return;
    if(InstallAion((BYTE*)module,r))*(int*)(obj+0x18)=r->destination;
}
