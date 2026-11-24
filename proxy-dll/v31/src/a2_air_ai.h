// A2/AM014, Normal automatic P1 vs MM only. Native DEF/physics/stats unchanged.
// LOAD supplies the native actor interface; its strategy tick is never called for A2.
#pragma once
struct A2Frame {
    BYTE *ai,*actor,*target,*motion; const char* name;
    int frame,category,hp,enemyHp,mp,equipment,eventFlag;
    float root[16],targetRoot[16],lock[16],targetLock[16],range,dy,yaw,targetSpeed;
    unsigned effects; int missiles,bullets;
};
struct A2State {
    BYTE* actor; const char* pending; const char* owned; unsigned ticket,completed;
    DWORD requestedAt,lastLog,lastTargetAt; float targetX,targetZ;
    bool running,charging; int reserve;
};
static BYTE* a2Module;
static bool a2Enabled;
static MmTick_t a2PreviousTick;
static A2State a2State;
static bool A2Is(const char* a,const char* b) {return a && b && !_stricmp(a,b);}
static const char* A2Name(BYTE* p) {return p ? *(const char**)p : "";}
static bool A2Move(const char* n) {
    return n && (!strncmp(n,"STOP",4) || A2Is(n,"CHARGE") || A2Is(n,"FORWARD_A") ||
        A2Is(n,"BACK_A") || A2Is(n,"UP_A") || A2Is(n,"DOWN_A"));
}
static unsigned A2Effects(BYTE* actor,int* missiles,int* bullets) {
    // The actor's own CMN pool, not scene siblings: 0x9c material entries hold
    // live instance vectors at +0x90/+0x94. Same reader as the retained R6 probe.
    unsigned mask=0; *missiles=0;*bullets=0;
    BYTE* manager=*(BYTE**)(actor+0x6f4);if(!manager)return 0;
    BYTE* begin=*(BYTE**)(manager+8);BYTE* end=*(BYTE**)(manager+12);
    if(!begin || end<begin || end-begin>0x9c*256 || (end-begin)%0x9c)return 0;
    for(BYTE* mat=begin;mat<end;mat+=0x9c) {
        BYTE** lo=*(BYTE***)(mat+0x90);BYTE** hi=*(BYTE***)(mat+0x94);
        if(!lo || hi<lo || hi-lo>256)continue;
        for(BYTE** p=lo;p<hi;++p) {
            BYTE* n=*p;if(!n)continue;
            BYTE* d=*(BYTE**)(n+0x16c);BYTE* mo=*(BYTE**)(n+0x398);
            if(!d || !mo || *(int*)(mo+0x74)==0)continue;
            const char* name=*(const char**)(d+0x34);
            if(!name)continue;
            const char* leaf=strrchr(name,'/');const char* back=strrchr(name,'\\');
            if(back && (!leaf || back>leaf))leaf=back;leaf=leaf?leaf+1:name;
            if(!_strnicmp(leaf,"gun_i",5) && strlen(leaf)>=12 && !_stricmp(leaf+6,"_AM.def")) {
                mask|=1;++*missiles;
            } else if(A2Is(leaf,"gun_C_AM.def")) {mask|=2;++*bullets;}
            else if(A2Is(leaf,"gun.def") || A2Is(leaf,"gun2.def"))mask|=4;
        }
    }
    return mask;
}
static bool A2Read(BYTE* ai,A2Frame* f) {
    if(!a2Enabled || !duelBalance || !duelAutoFight || DuelAiTier()!=1 || balScene!=787 ||
        GetModuleHandleW(battleModule)!=(HMODULE)a2Module)return false;
    __try {
        if(*(void**)ai!=a2Module+0xe80d4)return false;
        BYTE* a=*(BYTE**)(ai+4);if(!a || *(int*)(a+0x724)!=0 || !MmDefinition(a,"A2_MM_AIR_R1.def"))return false;
        BYTE* mgr=*(BYTE**)(a2Module+0x11e3b0);
        BYTE* b=mgr?((MmTarget_t)(a2Module+0x39d60))(mgr,a):NULL;
        if(!b || *(int*)(b+0x724)!=1 || !MmDefinition(b,"MMDV.def"))return false;
        BYTE* an=*(BYTE**)(a+0xb84);BYTE* bn=*(BYTE**)(b+0xb84);if(!an||!bn)return false;
        MmWorld_t w=(MmWorld_t)(a2Module+0x15930);AnParamFn get=(AnParamFn)(a2Module+0x30e90);
        f->ai=ai;f->actor=a;f->target=b;f->motion=*(BYTE**)(a+0x398);f->name=A2Name(f->motion);
        f->frame=f->motion?*(int*)(f->motion+0x54):0;f->category=f->motion?*(int*)(f->motion+0x74):-1;
        f->hp=get(a,100);f->enemyHp=get(b,100);f->mp=get(a,101);f->equipment=get(a,10);f->eventFlag=get(a,1);
        memcpy(f->root,w(a),64);memcpy(f->targetRoot,w(b),64);memcpy(f->lock,w(an),64);memcpy(f->targetLock,w(bn),64);
        const float dx=f->targetLock[12]-f->root[12],dz=f->targetLock[14]-f->root[14];
        f->range=sqrtf(dx*dx+dz*dz);f->dy=f->targetLock[13]-f->lock[13];
        f->yaw=atan2f(dx*f->root[0]+dz*f->root[2],dx*f->root[8]+dz*f->root[10]);
        f->effects=A2Effects(a,&f->missiles,&f->bullets);f->targetSpeed=0;
        DWORD now=GetTickCount();
        if(a2State.actor==a && a2State.lastTargetAt && now>a2State.lastTargetAt) {
            float x=f->targetLock[12]-a2State.targetX,z=f->targetLock[14]-a2State.targetZ;
            f->targetSpeed=sqrtf(x*x+z*z)*1000.0f/(now-a2State.lastTargetAt);
        }
        return _finite(f->range)&&_finite(f->dy)&&_finite(f->yaw);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void A2ClearOwnRequest(BYTE* actor,const char* name) {
    // 316f0 MREQ_SELF compares the direct pointer with current. Clear only the
    // acknowledged request, never the active motion, its frames or foreign requests.
    for(int i=0;i<3;++i) {
        BYTE** p=(BYTE**)(actor+0x378+i*8);
        if(A2Is(A2Name(*p),name)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
    for(int i=0;i<5;++i) {
        BYTE** p=(BYTE**)(actor+0x2b8+i*8);
        if(A2Is(A2Name(*p),name)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
}
static void A2Request(const A2Frame& f,const char* name,int cost,bool attack,const char* reason) {
    if(f.mp<cost)return;
    const char* direct=A2Name(*(BYTE**)(f.actor+0x378));
    if(A2Is(direct,name))return;
    if(attack && A2Is(f.name,name))return;
    ((AnNameFn)(a2Module+0x62000))(f.ai,name);
    if(attack){a2State.pending=name;a2State.owned=name;a2State.running=false;a2State.requestedAt=GetTickCount();++a2State.ticket;}
    Log("A2_REQUEST ticket=%u action=%s attack=%d cost=%d mp=%d equip=%d xz=%.3f dy=%.3f yaw=%.3f effects=%u missiles=%d reason=%s native_failed=%d",
        a2State.ticket,name,attack,cost,f.mp,f.equipment,f.range,f.dy,f.yaw*57.2957795f,f.effects,f.missiles,reason,(int)*(BYTE*)(f.ai+0x331));
}
static void A2Decide(const A2Frame& f) {
    if(a2State.actor!=f.actor){ZeroMemory(&a2State,sizeof a2State);a2State.actor=f.actor;}
    if(f.hp<=0 || f.enemyHp<=0 || f.eventFlag)return;
    if(a2State.pending) {
        if(A2Is(f.name,a2State.pending)) {
            Log("A2_STARTED ticket=%u action=%s frame=%d mp=%d",a2State.ticket,f.name,f.frame,f.mp);
            A2ClearOwnRequest(f.actor,a2State.pending);a2State.pending=NULL;a2State.running=true;
        } else if(!A2Move(f.name) || GetTickCount()-a2State.requestedAt<1500)return;
        else {
            Log("A2_NOT_STARTED ticket=%u action=%s current=%s",a2State.ticket,a2State.pending,f.name);
            A2ClearOwnRequest(f.actor,a2State.pending);a2State.pending=NULL;a2State.owned=NULL;
        }
    }
    // Retain valid attack/hit/grab/KO execution. No attack is cancelled by a timer.
    if(!A2Move(f.name))return;
    if(a2State.running) {
        Log("A2_FINISHED ticket=%u action=%s next=%s mp=%d equip=%d effects=%u",a2State.ticket,a2State.owned,f.name,f.mp,f.equipment,f.effects);
        a2State.running=false;a2State.owned=NULL;++a2State.completed;
    }
    int reserve=f.equipment==2?30:50; // native summon20 + one missile30, no edited costs
    if(a2State.charging) {
        if(f.mp<a2State.reserve){A2Request(f,"CHARGE",0,false,"native_recovery");return;}
        a2State.charging=false;Log("A2_CHARGED mp=%d reserve=%d",f.mp,a2State.reserve);
    }
    if(f.mp<reserve) {
        a2State.charging=true;a2State.reserve=reserve;
        A2Request(f,"CHARGE",0,false,"reserve_next_attack");return;
    }
    // Native STOP acquisition360/720 is used before narrow120 firing cones.
    if(fabsf(f.yaw)>0.174532925f) {A2Request(f,"STOP",0,false,"face_target");return;}
    if(f.equipment!=2) {A2Request(f,"GUN_SUMMON",20,true,"equip_own_air_guns");return;}
    // MM is a mobile aerial target. Prefer homing six-shot ITACUA; align the live
    // lock nodes and use only existing AM014 air movement. No ground aliases.
    if(fabsf(f.dy)>150.0f) {A2Request(f,f.dy>0?"UP_A":"DOWN_A",0,false,"lock_height");return;}
    if(f.range>1800.0f) {A2Request(f,"FORWARD_A",0,false,"missile_entry");return;}
    if(f.range<600.0f) {A2Request(f,"BACK_A",0,false,"ranged_clearance");return;}
    if(!(f.effects&1)) {A2Request(f,"ITACUA",30,true,"homing_vs_air_motion");return;}
    // The CTHUGUA gun stops turning65 frames before its native shot. Use it only
    // in a currently slow-target opportunity; otherwise low-cost twin GUN fire.
    if(!(f.effects&2) && f.targetSpeed<180.0f && fabsf(f.dy)<80.0f) {
        A2Request(f,"CTHUGUA",30,true,"slow_target_opening");return;
    }
    if(!(f.effects&4)) {A2Request(f,"GUN",4,true,"missiles_active_cover_fire");return;}
    A2Request(f,"STOP",0,false,"wait_existing_payload");
}
static void __fastcall A2Tick(BYTE* secondary,void*,DWORD dt,DWORD arg) {
    BYTE* ai=secondary-0x714;A2Frame f={};
    if(!A2Read(ai,&f)){a2PreviousTick(secondary,dt,arg);return;}
    typedef void (__thiscall* UpdateFn)(BYTE*,DWORD);
    ((UpdateFn)(a2Module+0x62580))(ai,dt);
    *(int*)(ai+0x2c)=0;*(int*)(ai+0x298)=-1;*(BYTE*)(ai+0x29c)=0;
    A2Decide(f);
    if(A2Is(f.name,"START"))((AnPhaseFn)(a2Module+0x64b20))(ai);
    DWORD now=GetTickCount();
    if(now-a2State.lastLog>=250) {
        a2State.lastLog=now;
        Log("A2_STATE ticket=%u motion=%s frame=%d hp=%d enemyhp=%d mp=%d equip=%d event=%d xz=%.3f dy=%.3f yaw=%.3f enemySpeed=%.3f effects=%u missiles=%d bullets=%d charge=%d pending=%s",
          a2State.ticket,f.name,f.frame,f.hp,f.enemyHp,f.mp,f.equipment,f.eventFlag,f.range,f.dy,f.yaw*57.2957795f,f.targetSpeed,f.effects,f.missiles,f.bullets,a2State.charging,a2State.pending?a2State.pending:"-");
    }
    a2State.targetX=f.targetLock[12];a2State.targetZ=f.targetLock[14];a2State.lastTargetAt=now;
}
static void ApplyA2Ai(BYTE* obj,HMODULE module) {
    BYTE* b=(BYTE*)module;
    if(a2Module==b && *(void**)(b+0xe80c8)==(void*)A2Tick && a2PreviousTick)
        MmSwapSlot((void**)(b+0xe80c8),(void*)A2Tick,(void*)a2PreviousTick);
    a2Enabled=false;
    if(!duelBalance || !duelAutoFight || DuelAiTier()!=1 || balScene!=787 || *(int*)(obj+0x18)!=1787)return;
    const BYTE head[]={0x8b,0x44,0x24,0x04,0x81,0xec,0x98,0,0,0};
    if(memcmp(b+0x60290,head,sizeof head) || *(void**)(b+0xe80c8)!=b+0x60290){Log("A2_NATIVE fingerprint mismatch");return;}
    wchar_t p[MAX_PATH];swprintf_s(p,L"%s\\dmpack514_a2_air.dat",gameDir);
    if(GetFileAttributesW(p)==INVALID_FILE_ATTRIBUTES){Log("A2_NATIVE private pack missing");return;}
    a2Module=b;a2PreviousTick=(MmTick_t)(b+0x60290);ZeroMemory(&a2State,sizeof a2State);
    a2Enabled=MmSwapSlot((void**)(b+0xe80c8),b+0x60290,(void*)A2Tick);
    if(a2Enabled)*(int*)(obj+0x18)=9421;
    Log("A2_NATIVE enabled=%d source=787 loaded=1787 destination=9421 side=0 policy=A2_AIR_R1",a2Enabled);
}
