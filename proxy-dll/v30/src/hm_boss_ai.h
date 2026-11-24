// Ambrosius-owned policy. Reuses native request/DEF execution and START initialization,
// not LOAD strategic decisions or its borrowed rifle action table. First scoped pilot only.
#pragma once
struct HmFrame {
    BYTE *ai,*actor,*target,*motion; const char* name;
    int side,frame,category,hp,enemyHp,mp,eventFlag;
    float root[16],targetRoot[16],lock[16],targetLock[16];
    float distance,dy,headingError;
};
struct HmState {
    BYTE* actor; unsigned ticket,cycle; const char* pending; const char* owned;
    int pendingFrame; DWORD pendingAt,lastLog; bool began,charging; int reserve; bool facing; DWORD facingAt;
};
static BYTE* hmModule;
static bool hmEnabled;
static int hmSide,hmSource;
static const char* hmOwnDef;static const char* hmTargetDef;
static MmTick_t hmPreviousTick;
static HmState hmState;
static const char* HmName(BYTE* mo) {return mo ? *(const char**)mo : "";}
static bool HmIs(const char* a,const char* b) {return a && b && !_stricmp(a,b);}
static bool HmLocomotion(const char* n) {
    return n && (!strncmp(n,"STOP",4) || HmIs(n,"CHARGE") || HmIs(n,"FORWARD_A") ||
      HmIs(n,"BACK_A") || HmIs(n,"LMOVE_A") || HmIs(n,"RMOVE_A") || HmIs(n,"UP_A") || HmIs(n,"DOWN_A"));
}
static bool HmRead(BYTE* ai,HmFrame* f) {
    if(!hmEnabled || !duelBalance || !duelAutoFight || DuelAiTier()!=1 ||
       balScene!=hmSource || GetModuleHandleW(battleModule)!=(HMODULE)hmModule)return false;
    __try {
        if(*(void**)ai!=hmModule+0xe80d4)return false;
        BYTE* a=*(BYTE**)(ai+4); if(!a || *(int*)(a+0x724)!=hmSide ||
            !MmDefinition(a,hmOwnDef))return false;
        BYTE* mgr=*(BYTE**)(hmModule+0x11e3b0);
        BYTE* b=mgr ? ((MmTarget_t)(hmModule+0x39d60))(mgr,a):NULL;
        if(!b || *(int*)(b+0x724)!=(1-hmSide) || !MmDefinition(b,hmTargetDef))return false;
        BYTE* an=*(BYTE**)(a+0xb84);BYTE* bn=*(BYTE**)(b+0xb84);if(!an||!bn)return false;
        MmWorld_t w=(MmWorld_t)(hmModule+0x15930);
        f->ai=ai;f->actor=a;f->target=b;f->side=hmSide;f->motion=*(BYTE**)(a+0x398);
        f->name=HmName(f->motion);f->frame=f->motion?*(int*)(f->motion+0x54):0;
        f->category=f->motion?*(int*)(f->motion+0x74):-1;
        AnParamFn get=(AnParamFn)(hmModule+0x30e90);
        f->hp=get(a,100);f->enemyHp=get(b,100);f->mp=get(a,101);f->eventFlag=get(a,1);
        memcpy(f->root,w(a),sizeof f->root);memcpy(f->targetRoot,w(b),sizeof f->targetRoot);
        memcpy(f->lock,w(an),sizeof f->lock);memcpy(f->targetLock,w(bn),sizeof f->targetLock);
        float dx=f->targetRoot[12]-f->root[12],dz=f->targetRoot[14]-f->root[14];
        f->distance=sqrtf(dx*dx+dz*dz);f->dy=f->targetLock[13]-f->lock[13];
        f->headingError=0;
        if(hmSource==732) {
            // Native33470 rotates from actor origin toward the opponent lockOn node.
            // HM runtime root matrices are pure yaw (verified in all1094 R3 samples).
            const float tx=f->targetLock[12]-f->root[12];
            const float tz=f->targetLock[14]-f->root[14];
            const float sideways=tx*f->root[0]+tz*f->root[2];
            const float forward=tx*f->root[8]+tz*f->root[10];
            f->headingError=atan2f(sideways,forward);
        }
        return _finite(f->distance)&&_finite(f->dy)&&_finite(f->headingError);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#include "hm_boss_geometry.h"
static bool HmQueued(BYTE* a,const char* n) {
    // Only the primary body channel acknowledges primary skill start.
    if(HmIs(HmName(*(BYTE**)(a+0x378)),n))return true;
    for(int i=0;i<5;++i)
        if(HmIs(HmName(*(BYTE**)(a+0x2b8+i*8)),n))return true;
    return false;
}
static void HmWithdrawUnstarted(const HmFrame& f,const char* n) {
    // Native 307f0 stores a definition pointer plus request timestamp.
    // Remove only this policy's failed, never-started request. No active motion is touched.
    for(int i=0;i<3;++i) {
        BYTE** p=(BYTE**)(f.actor+0x378+i*8);
        if(HmIs(HmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
    for(int i=0;i<5;++i) {
        BYTE** p=(BYTE**)(f.actor+0x2b8+i*8);
        if(HmIs(HmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
}
static void HmSubmit(const HmFrame& f,const char* name,bool attack,int reserve=0) {
    if(!name)return;
    if(HmIs(f.name,name)) {
        // Original MREQ_SELF continuation window; never reset a running action.
        if((HmIs(name,"CHARGE") && f.frame>=35 && f.frame<=40) ||
           ((HmIs(name,"FORWARD_A")||HmIs(name,"BACK_A")) && f.frame>=50 && f.frame<=60))
            ((AnNameFn)(hmModule+0x62000))(f.ai,name);
        return;
    }
    if(HmQueued(f.actor,name))return;
    if(attack && f.mp<reserve)return;
    ((AnNameFn)(hmModule+0x62000))(f.ai,name);
    if(attack) {
        hmState.pending=name;hmState.pendingAt=GetTickCount();hmState.pendingFrame=f.frame;
        hmState.began=false;hmState.owned=name;++hmState.ticket;
    }
    Log("HM_REQUEST ticket=%u action=%s attack=%d cost=%d mp=%d xz=%.3f dy=%.3f native_failed=%d",
        hmState.ticket,name,attack,reserve,f.mp,f.distance,f.dy,(int)*(BYTE*)(f.ai+0x331));
}
static bool HmFaceGeneralPhantom(const HmFrame& f) {
    if(hmSource!=732)return false;
    // Native LOCKON's first parameter is total acquisition aperture, not turn speed.
    // FORWARD_A(180,30) cannot turn when abs(error)>=90deg. STOP(360,720)
    // can acquire behind and turn using its ORIGINAL control/frames.
    const float coneHalf=1.57079632679f;
    const float oneForwardTurnFrame=0.00872664626f; // original30deg/sec /60Hz
    if(!hmState.facing && fabsf(f.headingError)>=coneHalf) {
        hmState.facing=true;hmState.facingAt=GetTickCount();
        Log("HM_FACE_BEGIN source=%d yaw_deg=%.4f motion=%s frame=%d flags=%u lock_block=%d",
            hmSource,f.headingError*57.2957795131f,f.name,f.frame,
            *(DWORD*)(f.actor+0x720),*(int*)(f.actor+0xb98));
    }
    if(!hmState.facing)return false;
    if(fabsf(f.headingError)<=oneForwardTurnFrame) {
        Log("HM_FACE_ALIGNED source=%d yaw_deg=%.4f elapsed_ms=%lu motion=%s frame=%d",
            hmSource,f.headingError*57.2957795131f,GetTickCount()-hmState.facingAt,f.name,f.frame);
        hmState.facing=false;return false;
    }
    // Native direct requests persist after the prior motion ends. Publish STOP even
    // on its first natural frame so the old E_LANCHAR cannot restart from interrupt0.
    // Category0 has no MREQ0 transition, so holding STOP does NOT reset its frames.
    // A moving category1 accepts MREQ0 and enters the same original STOP normally.
    const char* direct=HmName(*(BYTE**)(f.actor+0x378));
    if(!HmIs(direct,"STOP")) {
        ((AnNameFn)(hmModule+0x62000))(f.ai,"STOP");
        Log("HM_FACE_HOLD prior_request=%s current=%s frame=%d new_request=%s",
            direct,f.name,f.frame,HmName(*(BYTE**)(f.actor+0x378)));
    }
    return true;
}
#include "hm_general_adapter.h"
static void HmDecide(const HmFrame& f) {
    if(hmState.actor!=f.actor) {ZeroMemory(&hmState,sizeof hmState);hmState.actor=f.actor;}
    HmGeneralPostKo(f);
    if(f.hp<=0 || f.enemyHp<=0 || f.eventFlag!=0)return;
    if(hmState.pending) {
        if(HmIs(f.name,hmState.pending)) {
            hmState.pending=NULL;hmState.began=true;
            Log("HM_STARTED ticket=%u motion=%s frame=%d mp=%d",hmState.ticket,f.name,f.frame,f.mp);
        } else if(GetTickCount()-hmState.pendingAt<1000 || !HmLocomotion(f.name)) return;
        else {
            Log("HM_NOT_STARTED ticket=%u expected=%s current=%s frame=%d",hmState.ticket,hmState.pending,f.name,f.frame);
            HmWithdrawUnstarted(f,hmState.pending);
            hmState.pending=NULL;hmState.owned=NULL;
        }
    }
    // Retain the original explicitly allowed COMBO1 -> 2 -> 3 -> 4 windows.
    if(hmState.began && hmState.owned && HmIs(f.name,hmState.owned)) {
        const char* next=NULL;
        if(HmIs(f.name,"COMBO1") && f.frame>=55 && f.frame<80)next="COMBO2";
        if(HmIs(f.name,"COMBO2") && f.frame>=40 && f.frame<65)next="COMBO3";
        if(HmIs(f.name,"COMBO3") && f.frame>=40 && f.frame<65)next="COMBO4";
        if(next) {HmSubmit(f,next,true);return;}
    }
    if(!HmLocomotion(f.name))return; // attack, hit, catch and mandatory events own execution
    if(hmState.owned && hmState.began) {
        Log("HM_FINISHED ticket=%u motion=%s next=%s mp=%d",hmState.ticket,hmState.owned,f.name,f.mp);
        hmState.owned=NULL;hmState.began=false;++hmState.cycle;
    }
    // Actual native CHARGE gives +3 at 15/25/35 and loops 40 -> 10.
    // The LOAD movement-B fallback has no MP addition; never use it for recovery.
    if(hmState.charging) {
        if(f.mp<hmState.reserve) {HmSubmit(f,"CHARGE",false);return;}
        hmState.charging=false;
        Log("HM_CHARGED mp=%d reserve=%d",f.mp,hmState.reserve);
    }
    if(hmSource==720 || hmSource==722){HmGeneralDecide(f);return;}
    unsigned step=hmState.cycle%5;
    // The huge boss hull obstructs root-distance80 approach. Open with guided attacks,
    // then permit each close attack only when its own projected volume meets a live HIT box.
    if(hmSource==732 || hmSource==733) {
        static const unsigned rmOrder[5]={2,4,0,1,3};
        static const unsigned smOrder[5]={4,2,1,0,3};
        step=(hmSource==732?rmOrder:smOrder)[step];
    }
    int cost=(step==2 || step==4)?40:(step==3?10:(step==1?5:0));
    if(f.mp<cost || f.mp<5) {
        hmState.charging=true;hmState.reserve=cost>10?cost:10;
        HmSubmit(f,"CHARGE",false);return;
    }
    if(HmFaceGeneralPhantom(f))return;
    // Huge bosses have multiple damageable body volumes. A moving head lock is
    // not a necessary prerequisite when the chosen near attack already overlaps one.
    if((hmSource==732 || hmSource==733) && (step==0 || step==1 || step==3) && HmMeleeContact(f,step)) {
        if(fabsf(f.dy)>12.5f)Log("HM_BODY_CONTACT_PRIORITY source=%d step=%u head_dy=%.3f",hmSource,step,f.dy);
        if(step==0)HmSubmit(f,"COMBO1",true);
        else if(step==1)HmSubmit(f,"RAI_2",true,5);
        else HmSubmit(f,"KAMA1",true,10);
        return;
    }
    // Use the same live lock-on coordinate system as the native attack correction.
    // Half of the original 25-unit attack box is the entry tolerance, not a stat edit.
    if(fabsf(f.dy)>12.5f) {HmSubmit(f,f.dy>0?"UP_A":"DOWN_A",false);return;}
    if(step==2) {
        // Native E_LANCHAR handles its own 1000-unit opening and all 16 guided emissions.
        if(f.distance>1000.0f) {HmSubmit(f,"FORWARD_A",false);return;}
        HmSubmit(f,"E_LANCHAR",true,40);return;
    }
    if(step==4) {
        // H_CROW flies 25 units/frame; 800 units reaches during its native guidance interval.
        if(f.distance>800.0f) {HmSubmit(f,"FORWARD_A",false);return;}
        if(f.distance<200.0f) {HmSubmit(f,"BACK_A",false);return;}
        HmSubmit(f,"H_CROW",true,40);return;
    }
    // RAI_2 is a stationary close-range storm, not a travelling rifle projectile.
    // Its native target distance is 80, speed 200 and preparation is only 19 frames.
    // Enter at its actual native target distance, independently of COMBO/KAMA.
    const float entry=step==1?80.0f:300.0f;
    if(hmSource==732 || hmSource==733) {
        if(!HmMeleeContact(f,step)){HmSubmit(f,"FORWARD_A",false);return;}
    } else if(f.distance>entry) {HmSubmit(f,"FORWARD_A",false);return;}
    if(step==0)HmSubmit(f,"COMBO1",true);
    else if(step==1)HmSubmit(f,"RAI_2",true,5);
    else HmSubmit(f,"KAMA1",true,10);
}
static void HmTrace(const HmFrame& f) {
    DWORD now=GetTickCount();if(now-hmState.lastLog<250)return;hmState.lastLog=now;
    Log("HM_STATE t=%lu ticket=%u cycle=%u motion=%s frame=%d cat=%d hp=%d enemyhp=%d mp=%d event=%d xz=%.3f dy=%.3f rootsY=%.3f,%.3f locksY=%.3f,%.3f charge=%d reserve=%d pending=%s",
        now,hmState.ticket,hmState.cycle,f.name,f.frame,f.category,f.hp,f.enemyHp,f.mp,f.eventFlag,
        f.distance,f.dy,f.root[13],f.targetRoot[13],f.lock[13],f.targetLock[13],
        hmState.charging,hmState.reserve,hmState.pending?hmState.pending:"-");
    if(hmSource==732)
      Log("HM_HEADING yaw_deg=%.4f facing=%d flags=%u lock_block=%d root=%.4f,%.4f,%.4f target_lock=%.4f,%.4f,%.4f forward=%.6f,%.6f",
          f.headingError*57.2957795131f,hmState.facing,*(DWORD*)(f.actor+0x720),*(int*)(f.actor+0xb98),
          f.root[12],f.root[13],f.root[14],f.targetLock[12],f.targetLock[13],f.targetLock[14],f.root[8],f.root[10]);
    const char* nodes[3]={"attack","collid_body","collid_body2"};
    for(int i=0;i<3;++i) {
        BYTE* n=((MmFind_t)(hmModule+0x15a00))(i==0?f.actor:f.target,nodes[i]);
        if(n) {float* w=((MmWorld_t)(hmModule+0x15930))(n);
          Log("HM_NODE node=%s xyz=%.4f,%.4f,%.4f",nodes[i],w[12],w[13],w[14]);}
    }
}
static void __fastcall HmTick(BYTE* secondary,void*,DWORD dt,DWORD arg) {
    BYTE* ai=secondary-0x714;HmFrame f={};
    if(!HmRead(ai,&f)) {hmPreviousTick(secondary,dt,arg);return;}
    typedef void (__thiscall* UpdateFn)(BYTE*,DWORD);
    ((UpdateFn)(hmModule+0x62580))(ai,dt); // native observation/timers
    // No LOAD tick / rifle-selection path. Native DEF owns the actual skill.
    // Suppress stale locomotion output, and replace only the borrowed EMPTY budget
    // with this robot's native CHARGE resource policy (actor MP is never written).
    *(int*)(ai+0x2c)=0;
    *(int*)(ai+0x298)=-1;*(BYTE*)(ai+0x29c)=0;
    HmDecide(f);
    // 64b20 also issues LOAD guard/step/mode requests. It is a tactical writer,
    // not actor physics or the DEF hit/KO interpreter. Use its START-only reset,
    // never a second strategic command writer during an HM-owned action.
    if(HmIs(f.name,"START"))((AnPhaseFn)(hmModule+0x64b20))(ai);
    HmTrace(f);
}
static void ApplyHmAi(BYTE* obj,HMODULE module) {
    BYTE* b=(BYTE*)module;
    if(hmModule==b && *(void**)(b+0xe80c8)==(void*)HmTick && hmPreviousTick)
      MmSwapSlot((void**)(b+0xe80c8),(void*)HmTick,(void*)hmPreviousTick);
    hmEnabled=false;
    if(!duelBalance || !duelAutoFight || DuelAiTier()!=1)return;
    const int loaded=*(int*)(obj+0x18);int destination;
    const int general=HgRoute(duelBalance!=0,duelAutoFight!=0,DuelAiTier(),balScene,loaded);
    if(balScene==732 && loaded==1732) {
        hmSide=0;hmSource=732;destination=9303;
        hmOwnDef="HM_RM_OWNED_R1.def";hmTargetDef="RMDVB.def";
    } else if(general==9781) {
        hmSide=0;hmSource=720;destination=9781;
        hmOwnDef="HM_GM_OWN_C1.def";hmTargetDef="PAIR720P1R1.def";
    } else if(general==9782) {
        hmSide=0;hmSource=722;destination=9782;
        hmOwnDef="HM_LM_OWN_C1.def";hmTargetDef="PAIR722P1R1.def";
    } else return;
    const BYTE tickHead[]={0x8b,0x44,0x24,0x04,0x81,0xec,0x98,0,0,0};
    if(memcmp(b+0x60290,tickHead,sizeof tickHead) || *(void**)(b+0xe80c8)!=b+0x60290) {
        Log("HM_NATIVE DISABLED native fingerprint/slot mismatch");return;
    }
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\\\dmpack513_hm_owned.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES){Log("HM_NATIVE missing private pack");return;}
    *(int*)(obj+0x18)=destination;
    hmModule=b;hmPreviousTick=(MmTick_t)(b+0x60290);ZeroMemory(&hmState,sizeof hmState);
    hmEnabled=MmSwapSlot((void**)(b+0xe80c8),b+0x60290,(void*)HmTick);
    Log("HM_NATIVE enabled=%d source=%d scene=%d side=%d policy=HM_R1 tactics=own",hmEnabled,hmSource,destination,hmSide);
}

