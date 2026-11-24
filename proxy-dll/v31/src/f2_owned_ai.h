#pragma once
// F2 own policy, first pilot: Normal automatic P1 F2 vs MM only.
// LOAD supplies an allocated actor interface; its strategy/attack/defence ticks
// are NEVER called for the scoped F2. All combat execution remains in exact DEF.
struct F2Frame {
    BYTE *ai,*actor,*target,*motion;const char *name,*enemyMotion;
    int frame,category,hp,enemyHp,mp,weapon,unlock,bind,eventFlag;
    float root[16],targetRoot[16],lock[16],targetLock[16];
    float distance,dy,yaw;
};
struct F2State {
    BYTE* actor;const char *pending,*owned;unsigned ticket,chains,shots;
    DWORD pendingAt,lastLog;bool began,charging,ranged;int reserve;
    float time,gunReady,iceReady,fireReady,bigReady,bladeReady;
    float targetX,targetZ,speed;bool haveTarget;int chainEnemyHp;unsigned dryChains;
};
static BYTE* f2Module;static bool f2Enabled;static MmTick_t f2Previous;
static F2State f2State;
static const char* F2Name(BYTE* p){return p?*(const char**)p:"";}
static bool F2Is(const char* a,const char* b){return a&&b&&!_stricmp(a,b);}
static bool F2Idle(const char* n){return n&&(!strncmp(n,"STOP",4)||F2Is(n,"CHARGE")||F2Is(n,"FORWARD_A")||F2Is(n,"BACK_A")||F2Is(n,"UP")||F2Is(n,"DOWN"));}
static bool F2Read(BYTE* ai,F2Frame* f){
    if(!f2Enabled||!duelBalance||!duelAutoFight||DuelAiTier()!=1||balScene!=771||GetModuleHandleW(battleModule)!=(HMODULE)f2Module)return false;
    __try{
        if(*(void**)ai!=f2Module+0xe80d4)return false;
        BYTE* a=*(BYTE**)(ai+4);if(!a||*(int*)(a+0x724)!=0||!MmDefinition(a,"F2_MM_OWNED_R1.def"))return false;
        BYTE* mgr=*(BYTE**)(f2Module+0x11e3b0);BYTE* b=mgr?((MmTarget_t)(f2Module+0x39d60))(mgr,a):NULL;
        if(!b||*(int*)(b+0x724)!=1||!MmDefinition(b,"MMDV.def"))return false;
        BYTE *an=*(BYTE**)(a+0xb84),*bn=*(BYTE**)(b+0xb84);if(!an||!bn)return false;
        f->ai=ai;f->actor=a;f->target=b;f->motion=*(BYTE**)(a+0x398);f->name=F2Name(f->motion);
        f->enemyMotion=F2Name(*(BYTE**)(b+0x398));f->frame=f->motion?*(int*)(f->motion+0x54):0;f->category=f->motion?*(int*)(f->motion+0x74):-1;
        AnParamFn get=(AnParamFn)(f2Module+0x30e90);
        f->hp=get(a,100);f->enemyHp=get(b,100);f->mp=get(a,101);f->weapon=get(a,50);f->unlock=get(a,54);f->bind=get(a,254);f->eventFlag=get(a,1);
        MmWorld_t w=(MmWorld_t)(f2Module+0x15930);memcpy(f->root,w(a),64);memcpy(f->targetRoot,w(b),64);memcpy(f->lock,w(an),64);memcpy(f->targetLock,w(bn),64);
        float dx=f->targetRoot[12]-f->root[12],dz=f->targetRoot[14]-f->root[14];f->distance=sqrtf(dx*dx+dz*dz);f->dy=f->targetLock[13]-f->lock[13];
        dx=f->targetLock[12]-f->root[12];dz=f->targetLock[14]-f->root[14];f->yaw=atan2f(dx*f->root[0]+dz*f->root[2],dx*f->root[8]+dz*f->root[10]);
        return _finite(f->distance)&&_finite(f->dy)&&_finite(f->yaw);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void F2ClearRequest(const F2Frame& f,const char* name){
    // Clear only this acknowledged ticket's request, never the current motion.
    // A stale GUN self-request otherwise loops86->61 and spends MP indefinitely.
    for(int i=0;i<3;++i){BYTE** p=(BYTE**)(f.actor+0x378+i*8);if(F2Is(F2Name(*p),name)){*p=NULL;*(DWORD*)(p+1)=0;}}
    for(int i=0;i<5;++i){BYTE** p=(BYTE**)(f.actor+0x2b8+i*8);if(F2Is(F2Name(*p),name)){*p=NULL;*(DWORD*)(p+1)=0;}}
}
static void F2Request(const F2Frame& f,const char* name,bool ticket,int reserve=0){
    if(ticket&&f.mp<reserve)return;
    const char* direct=F2Name(*(BYTE**)(f.actor+0x378));
    if(F2Is(direct,name))return;
    ((AnNameFn)(f2Module+0x62000))(f.ai,name);
    if(ticket){f2State.pending=name;f2State.owned=name;f2State.pendingAt=GetTickCount();f2State.began=false;++f2State.ticket;}
    Log("F2_REQUEST ticket=%u action=%s owned=%d reserve=%d mp=%d weapon=%d xz=%.3f dy=%.3f yaw=%.3f failed=%d",f2State.ticket,name,ticket,reserve,f.mp,f.weapon,f.distance,f.dy,f.yaw,(int)*(BYTE*)(f.ai+0x331));
}
static bool F2Ice(const char* n){return n&&!strncmp(n,"ITACUA",6);}
static void F2StartLease(const char* n){
    // Conservative full native paths, in GAME time. No motion is cancelled.
    // GUN: last emission160 + SHOOT120 + BREAK20. Ice: six emissions through
    // ITACUA6 then SHOOT240/BREAK30/SHOOT2 240/BREAK2 30, rounded upward.
    if(F2Is(n,"GUN_SOUSHA"))f2State.gunReady=f2State.time+5.1f;
    if(F2Is(n,"GUN"))f2State.gunReady=f2State.time+3.6f;
    if(F2Is(n,"ITACUA"))f2State.iceReady=f2State.time+12.0f;
    if(F2Is(n,"CTHGUA"))f2State.fireReady=f2State.time+7.0f;
    if(F2Is(n,"CTHGUA_ITACUA"))f2State.bigReady=f2State.time+10.0f;
    // A full native chain already spaces Z1 reuse by193f and Z5 by193f.
    // Only an early-ended S_COMBO1 can return before its own74f effect path ends.
    if(F2Is(n,"S_COMBO1"))f2State.bladeReady=f2State.time+1.9f;
}
static bool F2Charge(const F2Frame& f,int needed){
    if(f2State.charging){
        if(f.mp<f2State.reserve){F2Request(f,"CHARGE",false);return true;}
        Log("F2_CHARGED mp=%d reserve=%d",f.mp,f2State.reserve);f2State.charging=false;
    }
    if(f.mp>=needed)return false;
    f2State.charging=true;f2State.reserve=needed;
    Log("F2_CHARGE_BEGIN mp=%d reserve=%d",f.mp,needed);F2Request(f,"CHARGE",false);return true;
}
static bool F2Align(const F2Frame& f,float range,float height,float yaw){
    // STOP acquires rear targets at360 degrees; forward flight acquires only120.
    if(fabsf(f.yaw)>yaw){F2Request(f,"STOP",false);return false;}
    if(fabsf(f.dy)>height){F2Request(f,f.dy>0?"UP":"DOWN",false);return false;}
    if(f.distance>range){F2Request(f,"FORWARD_A",false);return false;}
    return true;
}
static void F2Decide(const F2Frame& f){
    if(f.hp<=0||f.enemyHp<=0||f.eventFlag!=0)return;
    if(f2State.pending){
        if(F2Is(f.name,f2State.pending)){
            Log("F2_STARTED ticket=%u motion=%s frame=%d mp=%d weapon=%d",f2State.ticket,f.name,f.frame,f.mp,f.weapon);
            if(F2Is(f.name,"S_COMBO1"))f2State.chainEnemyHp=f.enemyHp;
            F2ClearRequest(f,f2State.pending);F2StartLease(f.name);f2State.pending=NULL;f2State.began=true;
        }else if(!F2Idle(f.name)||GetTickCount()-f2State.pendingAt<1600)return;
        else{Log("F2_NOT_STARTED ticket=%u expected=%s current=%s frame=%d",f2State.ticket,f2State.pending,f.name,f.frame);F2ClearRequest(f,f2State.pending);f2State.pending=NULL;f2State.owned=NULL;}
    }
    // Actual F2 native category chain: 5->6->7->8->9, at40/30/28/30.
    // Queue just before the one-frame native window, not after it.
    if(f2State.began&&f2State.owned&&F2Is(f.name,f2State.owned)){
        const char* next=NULL;
        if(F2Is(f.name,"S_COMBO1")&&f.frame>=35&&f.frame<=40)next="S_COMBO2";
        if(F2Is(f.name,"S_COMBO2")&&f.frame>=27&&f.frame<=30)next="S_COMBO3";
        if(F2Is(f.name,"S_COMBO3")&&f.frame>=25&&f.frame<=28)next="S_COMBO4";
        if(F2Is(f.name,"S_COMBO4")&&f.frame>=28&&f.frame<=30)next="S_COMBO5";
        if(next&&fabsf(f.dy)<=100.0f&&fabsf(f.yaw)<=1.05f){F2Request(f,next,true);return;}
    }
    if(!F2Idle(f.name))return; // Includes ITACUA2..6, damage, grabs and mandatory events.
    if(f2State.owned&&f2State.began){
        const char* n=f2State.owned;
        Log("F2_FINISHED ticket=%u action=%s next=%s mp=%d",f2State.ticket,n,f.name,f.mp);
        if(!strncmp(n,"S_COMBO",7)){
            ++f2State.chains;f2State.shots=0;
            const int contact=f2State.chainEnemyHp-f.enemyHp;
            // R1 observed successful 105-damage chains were followed by unnecessary
            // 40MP round-trip weapon switching. Keep the native blades while contact
            // is productive; repeated misses or separation can justify changing weapons.
            const bool guard=F2Is(f.enemyMotion,"GUARD");
            if(contact>0||guard)f2State.dryChains=0;else ++f2State.dryChains;
            // MM guard is a valid pressure state; abandon blades only after two
            // actual misses outside guard, or a separation beyond native600 reach.
            f2State.ranged=f2State.dryChains>=2||f.distance>600.0f;
            Log("F2_CHAIN_RESULT damage_window=%d range=%.3f target=%s ranged=%d",contact,f.distance,f.enemyMotion,f2State.ranged);
        }
        if(F2Ice(n)||F2Is(n,"GUN")||F2Is(n,"GUN_SOUSHA")||F2Is(n,"CTHGUA")||F2Is(n,"CTHGUA_ITACUA")){
            ++f2State.shots;
            if(f2State.shots>=2){f2State.ranged=false;f2State.shots=0;f2State.dryChains=0;}
        }
        f2State.owned=NULL;f2State.began=false;
    }
    // Against mobile airborne MM, start with one native sword chain, then answer
    // separation with guided ice/tracking burst, retaining a whole summon+shot budget.
    // This tactical contract is NOT enabled for any other target or placement.
    if(!f2State.ranged){
        if(F2Charge(f,f.weapon==0?0:20))return;
        if(f.weapon!=0){F2Request(f,"KEN_SUMMON",true,20);return;}
        if(!F2Align(f,450.0f,100.0f,0.70f))return;
        // MM's observed knockdown/invulnerable recovery is not a whiff defect.
        if(F2Is(f.enemyMotion,"DAMAGE4")||F2Is(f.enemyMotion,"STOP_MUTEKI")){F2Request(f,"STOP",false);return;}
        if(f2State.time<f2State.bladeReady){F2Request(f,"STOP",false);return;}
        F2Request(f,"S_COMBO1",true);return;
    }
    const int budget=f.weapon==2?25:45;
    if(F2Charge(f,budget))return;
    if(f.weapon!=2){F2Request(f,"GUN_SUMMON",true,45);return;}
    if(!F2Align(f,900.0f,30.0f,0.35f))return;
    if(f.distance<180.0f){F2Request(f,"BACK_A",false);return;}
    // Long compound preparation is valid only with original unlock54 and full80MP.
    // MM's moving attacks do not offer the stationary recovery required here.
    const bool targetQuiet=!strncmp(f.enemyMotion,"STOP",4)||F2Is(f.enemyMotion,"EMPTY");
    if(f.unlock==1&&f.mp>=80&&targetQuiet&&f2State.speed<10.0f&&f.distance>=600.0f&&f2State.time>=f2State.bigReady){F2Request(f,"CTHGUA_ITACUA",true,80);return;}
    if(f2State.shots==0&&f2State.time>=f2State.iceReady){F2Request(f,"ITACUA",true,25);return;}
    if(f2State.time>=f2State.gunReady){F2Request(f,"GUN_SOUSHA",true,25);return;}
    // Recover with native +3 at10/20/30 and MREQ_SELF40->10 while effects finish.
    if(f.mp<45){F2Charge(f,45);return;}
    F2Request(f,"STOP",false);
}
static void __fastcall F2Tick(BYTE* secondary,void*,DWORD dt,DWORD arg){
    BYTE* ai=secondary-0x714;F2Frame f={};
    if(!F2Read(ai,&f)){f2Previous(secondary,dt,arg);return;}
    if(f2State.actor!=f.actor){ZeroMemory(&f2State,sizeof f2State);f2State.actor=f.actor;}
    float seconds;memcpy(&seconds,&dt,4);
    if(_finite(seconds)&&seconds>0&&seconds<1){
        f2State.time+=seconds;
        if(f2State.haveTarget){float x=f.targetRoot[12]-f2State.targetX,z=f.targetRoot[14]-f2State.targetZ;f2State.speed=0.8f*f2State.speed+0.2f*sqrtf(x*x+z*z)/seconds;}
    }
    f2State.targetX=f.targetRoot[12];f2State.targetZ=f.targetRoot[14];f2State.haveTarget=true;
    typedef void (__thiscall* ObserveFn)(BYTE*,DWORD);((ObserveFn)(f2Module+0x62580))(ai,dt);
    *(int*)(ai+0x2c)=0;*(int*)(ai+0x298)=-1;*(BYTE*)(ai+0x29c)=0;
    if(F2Is(f.name,"START"))((AnPhaseFn)(f2Module+0x64b20))(ai);else F2Decide(f);
    DWORD now=GetTickCount();if(now-f2State.lastLog>=200){f2State.lastLog=now;
        Log("F2_STATE time=%.3f ticket=%u motion=%s frame=%d cat=%d hp=%d enemyhp=%d mp=%d weapon=%d unlock=%d bind=%d xz=%.3f dy=%.3f yaw=%.3f targetspeed=%.3f ranged=%d charging=%d reserve=%d pending=%s",f2State.time,f2State.ticket,f.name,f.frame,f.category,f.hp,f.enemyHp,f.mp,f.weapon,f.unlock,f.bind,f.distance,f.dy,f.yaw,f2State.speed,f2State.ranged,f2State.charging,f2State.reserve,f2State.pending?f2State.pending:"-");
    }
}
static void ApplyF2Ai(BYTE* obj,HMODULE module){
    BYTE* b=(BYTE*)module;
    if(f2Module==b&&*(void**)(b+0xe80c8)==(void*)F2Tick&&f2Previous)MmSwapSlot((void**)(b+0xe80c8),(void*)F2Tick,(void*)f2Previous);
    f2Enabled=false;
    if(!duelBalance||!duelAutoFight||DuelAiTier()!=1||balScene!=771||*(int*)(obj+0x18)!=1771)return;
    const BYTE head[]={0x8b,0x44,0x24,0x04,0x81,0xec,0x98,0,0,0};
    if(memcmp(b+0x60290,head,sizeof head)||*(void**)(b+0xe80c8)!=b+0x60290){Log("F2_NATIVE disabled fingerprint/slot mismatch");return;}
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\dmpack514_f2_owned.dat",gameDir);
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES){Log("F2_NATIVE missing private pack");return;}
    f2Module=b;f2Previous=(MmTick_t)(b+0x60290);ZeroMemory(&f2State,sizeof f2State);
    f2Enabled=MmSwapSlot((void**)(b+0xe80c8),b+0x60290,(void*)F2Tick);
    if(f2Enabled)*(int*)(obj+0x18)=9501;
    Log("F2_NATIVE enabled=%d source=771 scene=9501 side=0 tier=1 policy=F2_MM_R3 native_tactics=0",f2Enabled);
}
