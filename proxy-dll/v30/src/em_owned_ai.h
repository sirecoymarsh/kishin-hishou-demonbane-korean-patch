// EM-owned two-stage sword planning; native DEF performs all actual actions.
// Narrow pilot: balanced Normal automatic P1 EM vs GM/IM only.
#pragma once
struct EmOwnRoute {int source,loaded,destination;const char* own;const char* target;};
static const EmOwnRoute emOwnRoutes[]={
    {704,704,9531,"EM_GM_OWNED_R1.def","PAIR704P1R1.def"},
    {711,711,9532,"EM_IM_OWNED_R1.def","PAIR711P1R1.def"}
};
static const EmOwnRoute* EmFindRoute(bool balance,bool automatic,int tier,int source,int loaded) {
    if(!balance || !automatic || tier!=1)return NULL;
    for(unsigned i=0;i<_countof(emOwnRoutes);++i)
        if(emOwnRoutes[i].source==source && emOwnRoutes[i].loaded==loaded)return &emOwnRoutes[i];
    return NULL;
}
struct EmOwnFrame {
    BYTE *ai,*actor,*target,*motion;const char *name,*enemyName;
    int frame,category,hp,enemyHp,mp,weapon,cannon,eventFlag;
    float root[16],targetRoot[16],lock[16],targetLock[16],distance,dy,yaw;
};
enum EmPlan { EM_FREE,EM_SINGLE,EM_VOLLEY,EM_COMBO,EM_GUN,EM_CANNON,EM_SWORD,EM_LAMP };
struct EmOwnState {
    BYTE* actor;EmPlan plan;const char* pending;const char* active;
    unsigned ticket,cycle;DWORD pendingAt,lastLog;bool started,recovering;
    int reserve;int lastStage;bool facing,unlockWait;
};
static BYTE* emOwnModule;
static bool emOwnEnabled;
static const EmOwnRoute* emOwnRoute;
static MmTick_t emOwnPrevious;
static EmOwnState emOwn;
static const char* EmName(BYTE* m){return m?*(const char**)m:"";}
static bool EmIs(const char* a,const char* b){return a && b && !_stricmp(a,b);}
static bool EmIdle(const char* n){return EmIs(n,"STOP")||EmIs(n,"STOP_X")||EmIs(n,"STOP_G");}
static bool EmMove(const char* n){return EmIs(n,"FORWARD_G")||EmIs(n,"BACK_G")||EmIs(n,"LMOVE_G")||EmIs(n,"RMOVE_G")||EmIs(n,"FORWARD_B");}
static bool EmFree(const char* n){return EmIdle(n)||EmMove(n)||EmIs(n,"CHARGE");}
static bool EmRead(BYTE* ai,EmOwnFrame* f) {
    if(!emOwnEnabled || !emOwnRoute || !duelBalance || !duelAutoFight || DuelAiTier()!=1 ||
       balScene!=emOwnRoute->source || GetModuleHandleW(battleModule)!=(HMODULE)emOwnModule)return false;
    __try {
        if(*(void**)ai!=emOwnModule+0xe81d4)return false;
        BYTE* a=*(BYTE**)(ai+4);
        if(!a || *(int*)(a+0x724)!=0 || !MmDefinition(a,emOwnRoute->own))return false;
        BYTE* mgr=*(BYTE**)(emOwnModule+0x11e3b0);
        BYTE* b=mgr?((MmTarget_t)(emOwnModule+0x39d60))(mgr,a):NULL;
        if(!b || *(int*)(b+0x724)!=1 || !MmDefinition(b,emOwnRoute->target))return false;
        BYTE *an=*(BYTE**)(a+0xb84),*bn=*(BYTE**)(b+0xb84);if(!an||!bn)return false;
        MmWorld_t w=(MmWorld_t)(emOwnModule+0x15930);AnParamFn get=(AnParamFn)(emOwnModule+0x30e90);
        f->ai=ai;f->actor=a;f->target=b;f->motion=*(BYTE**)(a+0x398);f->name=EmName(f->motion);
        f->enemyName=EmName(*(BYTE**)(b+0x398));f->frame=f->motion?*(int*)(f->motion+0x54):0;
        f->category=f->motion?*(int*)(f->motion+0x74):-1;
        f->hp=get(a,100);f->enemyHp=get(b,100);f->mp=get(a,101);f->weapon=get(a,41);
        f->cannon=get(a,44);f->eventFlag=get(a,1);
        memcpy(f->root,w(a),sizeof f->root);memcpy(f->targetRoot,w(b),sizeof f->targetRoot);
        memcpy(f->lock,w(an),sizeof f->lock);memcpy(f->targetLock,w(bn),sizeof f->targetLock);
        float dx=f->targetRoot[12]-f->root[12],dz=f->targetRoot[14]-f->root[14];
        f->distance=sqrtf(dx*dx+dz*dz);f->dy=f->targetLock[13]-f->lock[13];
        dx=f->targetLock[12]-f->root[12];dz=f->targetLock[14]-f->root[14];
        f->yaw=atan2f(dx*f->root[0]+dz*f->root[2],dx*f->root[8]+dz*f->root[10]);
        return _finite(f->distance)&&_finite(f->dy)&&_finite(f->yaw);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool EmQueued(BYTE* a,const char* n) {
    if(EmIs(EmName(*(BYTE**)(a+0x378)),n))return true;
    for(int i=0;i<5;++i)if(EmIs(EmName(*(BYTE**)(a+0x2b8+i*8)),n))return true;
    return false;
}
static void EmWithdraw(const EmOwnFrame& f,const char* n) {
    // Withdraw only our unstarted proposal; never clear an executing motion.
    for(int i=0;i<3;++i){BYTE** p=(BYTE**)(f.actor+0x378+i*8);if(EmIs(EmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}}
    for(int i=0;i<5;++i){BYTE** p=(BYTE**)(f.actor+0x2b8+i*8);if(EmIs(EmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}}
}
static unsigned EmReleaseConsumedGun(const EmOwnFrame& f) {
    // Releasing our held request leaves all current animations and buffered input intact.
    // Native MREQ_SELF compares these direct slots to the active motion pointer.
    unsigned cleared=0;
    for(int i=0;i<3;++i) {
        BYTE** slot=(BYTE**)(f.actor+0x378+i*8);
        if(*slot==f.motion){*slot=NULL;*(DWORD*)(slot+1)=0;++cleared;}
    }
    return cleared;
}
static void EmSubmit(const EmOwnFrame& f,const char* name,bool attack,int cost=0) {
    if(!name || f.mp<cost)return;
    if(EmIs(f.name,name))return; // original frame progression; no repeated self request
    const bool queued=EmQueued(f.actor,name);
    if(queued && !attack)return;
    // Adopt a still-queued same-name proposal into a fresh ownership ticket.
    // Otherwise a native restart after damage would run without its follow-up plan.
    if(!queued)((AnNameFn)(emOwnModule+0x62000))(f.ai,name);
    if(attack){emOwn.pending=name;emOwn.pendingAt=GetTickCount();emOwn.active=name;emOwn.started=false;++emOwn.ticket;}
    Log("EM_REQUEST t=%lu source=%d ticket=%u plan=%d action=%s attack=%d cost=%d mp=%d weapon=%d xz=%.3f dy=%.3f failed=%d reused_queue=%d",
        GetTickCount(),emOwnRoute->source,emOwn.ticket,(int)emOwn.plan,name,(int)attack,cost,f.mp,f.weapon,f.distance,f.dy,queued?-1:(int)*(BYTE*)(f.ai+0x331),(int)queued);
}
static void EmHoldStop(const EmOwnFrame& f) {
    // Old direct requests survive completion. Claim STOP even on its natural first frame.
    // Native interrupt0 has no MREQ0, hence this does not restart STOP each tick.
    if(!EmIs(EmName(*(BYTE**)(f.actor+0x378)),"STOP")) {
        ((AnNameFn)(emOwnModule+0x62000))(f.ai,"STOP");
        Log("EM_HOLD t=%lu ticket=%u actual=%s frame=%d mp=%d",GetTickCount(),emOwn.ticket,f.name,f.frame,f.mp);
    }
}
static void EmCharge(const EmOwnFrame& f) {
    // Standing still is no recovery below 40% HP: STOP's #100<=400 check moves Aion to STOP_X, +5 MP per
    // 200 frames (user 11-01 「피 40% 이하가 되니까 아무것도 안하고 가만히 있네」: 18/7/19 s waits vs IM, 25 s vs GM).
    // Native CHARGE adds at 10/20/30 and loops 20->10 while it stays requested; category1 takes the next request.
    if(!EmIs(EmName(*(BYTE**)(f.actor+0x378)),"CHARGE")) {
        ((AnNameFn)(emOwnModule+0x62000))(f.ai,"CHARGE");
        Log("EM_CHARGE t=%lu ticket=%u actual=%s frame=%d mp=%d reserve=%d hp=%d",GetTickCount(),emOwn.ticket,f.name,f.frame,f.mp,emOwn.reserve,f.hp);
    }
}
static bool EmFace(const EmOwnFrame& f) {
    // Native ground FORWARD first aperture90 cannot acquire a target behind.
    // STOP has original360/720 at frames0..10; no yaw or physics is written here.
    if(!emOwn.facing && fabsf(f.yaw)>=0.7853981634f)emOwn.facing=true;
    if(!emOwn.facing)return false;
    if(fabsf(f.yaw)<=0.0872664626f){emOwn.facing=false;return false;}
    EmHoldStop(f);return true;
}
static void EmFinish(const EmOwnFrame& f) {
    Log("EM_FINISHED t=%lu ticket=%u plan=%d action=%s next=%s mp=%d weapon=%d",GetTickCount(),emOwn.ticket,(int)emOwn.plan,emOwn.active?emOwn.active:"-",f.name,f.mp,f.weapon);
    emOwn.active=NULL;emOwn.started=false;
}
// User 11-01 「벨제부트 전에 한정해 AI를 바꿔야 할지도 모르겠네. 아이온이 바르자이 쓰는건 로드비야키같은 원거리 공중 로봇
// 상대로만 냅두고, 근접전에선 알 하자드의 램프로 접근해 바르자이로 공격하는걸로」. IM only: no throw, volley, GUN or cannon
// (its EPIDEMIC — 5 MP, 150 invisible frames — slipped every cannon). The lamp is 초열 비상 SHONETSU1: 40 MP, catch at
// 40..80, a catch becomes 소멸 주법 SHONETSU2 natively (「알하자드의 램프, 연소!」); its homing CORRECTDIST is
// ISPLAYER-only, so the AI's reach is the animation's own. The scimitar chain is S_COMBO1 (free, corrects up to 500 itself)
// -> S_COMBO2 -> S_COMBO3 (5 MP); the sword (#41) stays out while it is not thrown.
static void EmImMelee(const EmOwnFrame& f) {
    if(emOwn.recovering) {
        if(f.mp>=emOwn.reserve) {
            emOwn.recovering=false;Log("EM_RECOVERED t=%lu mp=%d reserve=%d close=0",GetTickCount(),f.mp,emOwn.reserve);
        } else {EmCharge(f);return;}
    }
    if(EmFace(f))return;
    if(f.weapon==0) {
        if(f.mp<30){emOwn.recovering=true;emOwn.reserve=30;EmCharge(f);return;}
        emOwn.plan=EM_SWORD;EmSubmit(f,"KEN_SUMMON",true,30);return;
    }
    // 11-01 v5: SHONETSU1 from 750..1250 never moved Aion (six 40-MP whiffs); it stays the S_COMBO3 cancel only.
    // User 「대시로 해줘」: close in with the free boost dash FORWARD_B (LOCKON 180/60, held by MREQ_SELF 41->31).
    // A released dash still runs 41..61 and takes no attack request, so hand off to S_COMBO1 earlier while dashing.
    const bool dashing=EmIs(f.name,"FORWARD_B");
    if(f.distance<=(dashing?700.0f:450.0f) && fabsf(f.dy)<=65.0f) {
        if(dashing)Log("EM_DASH_HANDOFF t=%lu xz=%.3f frame=%d mp=%d",GetTickCount(),f.distance,f.frame,f.mp);
        emOwn.plan=EM_SWORD;EmSubmit(f,"S_COMBO1",true);return;
    }
    if(!dashing)Log("EM_DASH t=%lu xz=%.3f dy=%.3f mp=%d",GetTickCount(),f.distance,f.dy,f.mp);
    EmSubmit(f,"FORWARD_B",false);
}
// User 11-01 「60%부터 포 모으기」「블러드 상대로 근접하다가 포 모으기」, GM only. Above HP 600 the IM close-range
// scimitar plan. From HP 600 no MP is spent (the Nero route's cannon phase banks from HP 600 the same way): free
// scimitar or punch chains up close, native CHARGE to CANNON's 100 otherwise, a dash back in once full. Native
// STOP -> STOP_V -> STOP_X writes #44 on STOP_X's frame 1 at HP <= 400 (through STOP_W only after its 200-frame loop),
// so away from GM the idle is held until it does; then CANNON (100 MP; it clears #41 and the sword material itself,
// its LOCKON turns from frame 270). GM has no EPIDEMIC-like relocation, so the IM-born 400 minimum is not used.
static void EmGmPlan(const EmOwnFrame& f) {
    if(f.hp>600){EmImMelee(f);return;}
    const bool dashing=EmIs(f.name,"FORWARD_B");
    const bool close=f.distance<=(dashing?700.0f:450.0f) && fabsf(f.dy)<=65.0f;
    if(f.cannon==1 && f.mp>=100) {
        emOwn.recovering=false;
        if(f.distance>950.0f){EmSubmit(f,"FORWARD_G",false);return;}
        if(EmFace(f))return;
        emOwn.plan=EM_CANNON;EmSubmit(f,"CANNON",true,100);return;
    }
    if(close) {
        if(f.weapon==1){emOwn.plan=EM_SWORD;EmSubmit(f,"S_COMBO1",true);}
        else {emOwn.plan=EM_COMBO;EmSubmit(f,"COMBO1",true);}
        return;
    }
    if(f.hp<=400 && f.cannon!=1) {
        if(!emOwn.unlockWait)
            Log("EM_CANNON_UNLOCK_WAIT t=%lu hp=%d mp=%d motion=%s frame=%d",GetTickCount(),f.hp,f.mp,f.name,f.frame);
        emOwn.unlockWait=true;EmHoldStop(f);return;
    }
    if(f.mp<100) {
        if(!emOwn.recovering || emOwn.reserve!=100)
            Log("EM_CANNON_BANK t=%lu hp=%d mp=%d weapon=%d xz=%.3f",GetTickCount(),f.hp,f.mp,f.weapon,f.distance);
        emOwn.recovering=true;emOwn.reserve=100;EmCharge(f);return;
    }
    emOwn.recovering=false;
    EmSubmit(f,"FORWARD_B",false);
}
// User 11-01 「아이온이 계속 공격하면 게임 종료 대사가 안뜨는 버그가 있나보네」. Our last direct request outlives
// its motion: after the KO S_COMBO3/S_COMBO1 restarted straight from STOP (interrupt0 MREQ5/7 at frame 20) for
// 12 s (v5), 7 s (v6) and 86 s (v7, never PWIN) while the loser waited in WAIT/WAIT2. As HM-LM's HmGeneralPostKo:
// once the opponent is KO, replace this policy's persisting request with native STOP — STOP is no cancel for a running
// chain (no MREQ0 in interrupts 5..7 or the chain windows), so the motion finishes and the idle stays idle.
static void EmPostKo(const EmOwnFrame& f) {
    if(f.hp<=0 || f.enemyHp>0 || f.eventFlag!=0)return;
    const char* direct=EmName(*(BYTE**)(f.actor+0x378));
    if(!direct[0] || EmIs(direct,"STOP"))return;
    ((AnNameFn)(emOwnModule+0x62000))(f.ai,"STOP");
    Log("EM_POSTKO_IDLE t=%lu ticket=%u prior=%s current=%s frame=%d new_request=%s",GetTickCount(),emOwn.ticket,
        direct,f.name,f.frame,EmName(*(BYTE**)(f.actor+0x378)));
    emOwn.pending=NULL;emOwn.active=NULL;emOwn.started=false;
}
static void EmDecide(const EmOwnFrame& f) {
    if(emOwn.actor!=f.actor){ZeroMemory(&emOwn,sizeof emOwn);emOwn.actor=f.actor;}
    EmPostKo(f);
    if(f.hp<=0 || f.enemyHp<=0 || f.eventFlag!=0)return;
    if(emOwn.pending) {
        if(EmIs(f.name,emOwn.pending)) {
            Log("EM_STARTED t=%lu ticket=%u action=%s frame=%d mp=%d weapon=%d",GetTickCount(),emOwn.ticket,f.name,f.frame,f.mp,f.weapon);
            emOwn.pending=NULL;emOwn.started=true;
            if(emOwnRoute->source==711 && emOwn.plan==EM_GUN && EmIs(f.name,"GUN")) {
                const unsigned cleared=EmReleaseConsumedGun(f);
                Log("EM_GUN_RELEASE t=%lu ticket=%u frame=%d mp=%d direct_cleared=%u",GetTickCount(),emOwn.ticket,f.frame,f.mp,cleared);
            }
        } else if(GetTickCount()-emOwn.pendingAt<1500 || !EmFree(f.name))return;
        else {
            Log("EM_NOT_STARTED t=%lu ticket=%u action=%s actual=%s frame=%d queued=%d",GetTickCount(),emOwn.ticket,emOwn.pending,f.name,f.frame,(int)EmQueued(f.actor,emOwn.pending));
            EmWithdraw(f,emOwn.pending);emOwn.pending=NULL;emOwn.active=NULL;emOwn.started=false;
        }
    }
    // Native valid windows only. No runtime frame writes or attack-body clones.
    if(emOwn.started && emOwn.active && EmIs(f.name,emOwn.active)) {
        if(emOwn.plan==EM_COMBO) {
            if(EmIs(f.name,"COMBO1") && f.frame>=50 && f.frame<75){EmSubmit(f,"COMBO2",true);return;}
            if(EmIs(f.name,"COMBO2") && f.frame>=45 && f.frame<75){EmSubmit(f,"COMBO3",true);return;}
        }
        if(emOwn.plan==EM_VOLLEY && EmIs(f.name,"KENBU1") && f.frame>=100 && f.frame<180 && f.weapon==2 && f.mp>=20) {
            EmSubmit(f,"KENBU2",true,20);return;
        }
        // IM scimitar chain: interrupts 5/6/7 take no request, S_COMBO1/2 take the next one at their frame 50 and
        // S_COMBO3 cancels into SHONETSU1 (MREQ 14) at 80..120.
        if(emOwn.plan==EM_SWORD) {
            if(EmIs(f.name,"S_COMBO1") && f.frame>=40 && f.frame<50){EmSubmit(f,"S_COMBO2",true);return;}
            if(EmIs(f.name,"S_COMBO2") && f.frame>=40 && f.frame<50){EmSubmit(f,"S_COMBO3",true);return;}
            const bool banking=emOwnRoute->source==704 && f.hp<=600;   // GM saves CANNON's 100 from HP 600
            if(EmIs(f.name,"S_COMBO3") && f.frame>=80 && f.frame<110 && f.mp>=40 && !banking){emOwn.plan=EM_LAMP;EmSubmit(f,"SHONETSU1",true,40);return;}
        }
    }
    // Damage, grabs, jump/landing, preparation and live attacks keep native ownership.
    if(!EmFree(f.name))return;
    if(emOwn.active && emOwn.started)EmFinish(f);
    if(emOwnRoute->source==704)EmGmPlan(f);
    else EmImMelee(f);
}
static void __fastcall EmOwnTick(BYTE* secondary,void*,DWORD dt,DWORD arg) {
    BYTE* ai=secondary-0x714;EmOwnFrame f={};
    if(!EmRead(ai,&f)){emOwnPrevious(secondary,dt,arg);return;}
    typedef void (__thiscall* SenseFn)(BYTE*,DWORD);
    ((SenseFn)(emOwnModule+0x62580))(ai,dt);
    *(int*)(ai+0x2c)=0; // AI movement proposal only, never an actor resource or frame
    if(EmIs(f.name,"START"))((AnPhaseFn)(emOwnModule+0x64b20))(ai);
    else EmDecide(f);
    DWORD now=GetTickCount();if(now-emOwn.lastLog>=100){emOwn.lastLog=now;
        Log("EM_STATE t=%lu ticket=%u cycle=%u plan=%d motion=%s frame=%d hp=%d enemyhp=%d mp=%d weapon=%d cannon=%d xz=%.3f dy=%.3f yaw=%.3f rootsY=%.3f,%.3f pending=%s recover=%d",
            now,emOwn.ticket,emOwn.cycle,(int)emOwn.plan,f.name,f.frame,f.hp,f.enemyHp,f.mp,f.weapon,f.cannon,f.distance,f.dy,f.yaw*57.2957795f,f.root[13],f.targetRoot[13],emOwn.pending?emOwn.pending:"-",(int)emOwn.recovering);
    }
}
static void ApplyEmOwnedAi(BYTE* obj,HMODULE module) {
    BYTE* b=(BYTE*)module;
    if(emOwnModule==b && *(void**)(b+0xe81c8)==(void*)EmOwnTick && emOwnPrevious)
        MmSwapSlot((void**)(b+0xe81c8),(void*)EmOwnTick,(void*)emOwnPrevious);
    emOwnEnabled=false;emOwnRoute=NULL;
    const EmOwnRoute* r=EmFindRoute(duelBalance!=0,duelAutoFight!=0,(int)DuelAiTier(),balScene,*(int*)(obj+0x18));
    if(!r)return;
    const BYTE head[]={0x8b,0x44,0x24,0x04,0x56,0x57,0x8b,0xf9,0x8d,0xb7,0xec,0xf8,0xff,0xff};
    if(memcmp(b+0x61aa0,head,sizeof head) || *(void**)(b+0xe81c8)!=b+0x61aa0 || !AionNativeBytesMatch(b)) {
        Log("EM_OWNED_DISABLED exact native fingerprint or slot mismatch");return;
    }
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack514_em_owned.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES)return;
    emOwnModule=b;emOwnRoute=r;emOwnPrevious=(MmTick_t)(b+0x61aa0);ZeroMemory(&emOwn,sizeof emOwn);
    emOwnEnabled=MmSwapSlot((void**)(b+0xe81c8),b+0x61aa0,(void*)EmOwnTick);
    if(emOwnEnabled)*(int*)(obj+0x18)=r->destination;
    Log("EM_OWNED_ENABLED enabled=%d source=%d scene=%d side=0 tier=1 own=%s target=%s",(int)emOwnEnabled,r->source,r->destination,r->own,r->target);
}