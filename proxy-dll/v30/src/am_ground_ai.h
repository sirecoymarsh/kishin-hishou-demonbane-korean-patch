// AM-owned ground tactics, R1. Scope: balanced Normal auto P1 AM -> P2 CM only.
// Original DEF motion execution, damage, charge rate, physics and inputs are untouched.
#pragma once
struct AmFrame {
    BYTE *ai,*actor,*target,*motion;
    const char *name,*enemyName;
    int frame,category,hp,enemyHp,mp,eventFlag,weapon,web;
    float root[16],targetRoot[16],lock[16],targetLock[16],distance,dy,yaw;
};
struct AmState {
    BYTE* actor; const char* pending; const char* owned;
    DWORD pendingAt,lastLog; unsigned ticket,swordChains,gunVolleys;
    bool began,charging; int reserve; int startHp;
};
static BYTE* amModule; static bool amEnabled; static MmTick_t amPreviousTick;
static AmState amState;
static const char* AmName(BYTE* mo) {return mo?*(const char**)mo:"";}
static bool AmIs(const char* a,const char* b) {return a&&b&&!_stricmp(a,b);}
static bool AmNeutral(const char* n) {
    return n&&(!strncmp(n,"STOP",4)||AmIs(n,"FORWARD_G")||AmIs(n,"BACK_G")||AmIs(n,"CHARGE"));
}
static bool AmRead(BYTE* ai,AmFrame* f) {
    if(!amEnabled||!duelBalance||!duelAutoFight||DuelAiTier()!=1||balScene!=670||
        GetModuleHandleW(battleModule)!=(HMODULE)amModule)return false;
    __try {
        if(*(void**)ai!=amModule+0xe7ad4)return false;
        BYTE* a=*(BYTE**)(ai+4);
        if(!a||*(int*)(a+0x724)!=0||!MmDefinition(a,"AM_CM_OWNED_R1.def"))return false;
        BYTE* mgr=*(BYTE**)(amModule+0x11e3b0);
        BYTE* b=mgr?((MmTarget_t)(amModule+0x39d60))(mgr,a):NULL;
        if(!b||*(int*)(b+0x724)!=1||!MmDefinition(b,"PAIR670P1R1.def"))return false;
        BYTE *an=*(BYTE**)(a+0xb84),*bn=*(BYTE**)(b+0xb84);
        if(!an||!bn)return false;
        f->ai=ai;f->actor=a;f->target=b;f->motion=*(BYTE**)(a+0x398);
        f->name=AmName(f->motion);f->enemyName=AmName(*(BYTE**)(b+0x398));
        f->frame=f->motion?*(int*)(f->motion+0x54):0;
        f->category=f->motion?*(int*)(f->motion+0x74):-1;
        AnParamFn get=(AnParamFn)(amModule+0x30e90);
        f->hp=get(a,100);f->enemyHp=get(b,100);f->mp=get(a,101);
        f->eventFlag=get(a,1);f->weapon=get(a,10);f->web=get(a,214);
        MmWorld_t w=(MmWorld_t)(amModule+0x15930);
        memcpy(f->root,w(a),sizeof f->root);memcpy(f->targetRoot,w(b),sizeof f->targetRoot);
        memcpy(f->lock,w(an),sizeof f->lock);memcpy(f->targetLock,w(bn),sizeof f->targetLock);
        const float dx=f->targetRoot[12]-f->root[12],dz=f->targetRoot[14]-f->root[14];
        f->distance=sqrtf(dx*dx+dz*dz);f->dy=f->targetLock[13]-f->lock[13];
        const float tx=f->targetLock[12]-f->root[12],tz=f->targetLock[14]-f->root[14];
        f->yaw=atan2f(tx*f->root[0]+tz*f->root[2],tx*f->root[8]+tz*f->root[10]);
        return _finite(f->distance)&&_finite(f->dy)&&_finite(f->yaw);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool AmQueued(BYTE* a,const char* n) {
    for(int i=0;i<3;++i)if(AmIs(AmName(*(BYTE**)(a+0x378+i*8)),n))return true;
    for(int i=0;i<5;++i)if(AmIs(AmName(*(BYTE**)(a+0x2b8+i*8)),n))return true;
    return false;
}
static void AmReleaseOwnRequest(BYTE* a,const char* n) {
    // Only the policy's acknowledged/rejected request. Never touch current motion channels.
    for(int i=0;i<3;++i) {
        BYTE** p=(BYTE**)(a+0x378+i*8);
        if(AmIs(AmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
    for(int i=0;i<5;++i) {
        BYTE** p=(BYTE**)(a+0x2b8+i*8);
        if(AmIs(AmName(*p),n)){*p=NULL;*(DWORD*)(p+1)=0;}
    }
}
static void AmSubmit(const AmFrame& f,const char* name,bool attack,int mpMinimum=0) {
    if(attack&&f.mp<mpMinimum)return;
    if(AmQueued(f.actor,name))return;
    if(AmIs(f.name,name)&&attack)return;
    ((AnNameFn)(amModule+0x62000))(f.ai,name);
    if(attack) {
        amState.pending=name;amState.pendingAt=GetTickCount();amState.owned=name;
        amState.began=false;amState.startHp=f.enemyHp;++amState.ticket;
    }
    Log("AM_REQUEST ticket=%u action=%s attack=%d mp_min=%d mp=%d weapon=%d xz=%.3f dy=%.3f native_failed=%d",
        amState.ticket,name,attack,mpMinimum,f.mp,f.weapon,f.distance,f.dy,(int)*(BYTE*)(f.ai+0x331));
}
static void AmCharge(const AmFrame& f,int reserve) {
    amState.charging=true;amState.reserve=reserve;AmSubmit(f,"CHARGE",false);
}
static void AmDecide(const AmFrame& f) {
    if(amState.actor!=f.actor){ZeroMemory(&amState,sizeof amState);amState.actor=f.actor;}
    if(f.hp<=0||f.enemyHp<=0||f.eventFlag!=0)return;
    if(amState.pending) {
        if(AmIs(f.name,amState.pending)) {
            Log("AM_STARTED ticket=%u motion=%s frame=%d mp=%d weapon=%d",amState.ticket,f.name,f.frame,f.mp,f.weapon);
            AmReleaseOwnRequest(f.actor,amState.pending);
            amState.pending=NULL;amState.began=true;
        } else if(GetTickCount()-amState.pendingAt<1200||!AmNeutral(f.name))return;
        else {
            Log("AM_NOT_STARTED ticket=%u expected=%s current=%s frame=%d",amState.ticket,amState.pending,f.name,f.frame);
            AmReleaseOwnRequest(f.actor,amState.pending);amState.pending=NULL;amState.owned=NULL;amState.began=false;
        }
    }
    // Queue slightly before the exact original acceptance frame. No extra stages.
    if(amState.began&&amState.owned&&AmIs(f.name,amState.owned)) {
        const char* next=NULL;
        if(AmIs(f.name,"KEN_COMBO1")&&f.frame>=30&&f.frame<=35&&f.weapon==1)next="KEN_COMBO2";
        if(AmIs(f.name,"KEN_COMBO2")&&f.frame>=30&&f.frame<=35&&f.weapon==1)next="KEN_COMBO3";
        if(AmIs(f.name,"COMBO1")&&f.frame>=45&&f.frame<=50&&f.weapon==0)next="COMBO2";
        if(AmIs(f.name,"COMBO2")&&f.frame>=35&&f.frame<=40&&f.weapon==0)next="COMBO3";
        if(next){AmSubmit(f,next,true);return;}
    }
    if(!AmNeutral(f.name))return; // DEF owns attack preparation, release, hit, grab and KO.
    if(amState.owned&&amState.began) {
        Log("AM_FINISHED ticket=%u action=%s next=%s mp=%d weapon=%d target_hp_change=%d",
            amState.ticket,amState.owned,f.name,f.mp,f.weapon,amState.startHp-f.enemyHp);
        if(AmIs(amState.owned,"KEN_COMBO3"))++amState.swordChains;
        if(AmIs(amState.owned,"GUN"))++amState.gunVolleys;
        amState.owned=NULL;amState.began=false;
    }
    if(amState.charging) {
        if(f.mp<amState.reserve){AmSubmit(f,"CHARGE",false);return;}
        Log("AM_CHARGED mp=%d reserve=%d",f.mp,amState.reserve);
        amState.charging=false;
    }
    // CM is a ground native-AI opponent. A visible attack opening is governed by
    // AM's original correction distances and armament, not the borrowed FM action table.
    if(fabsf(f.yaw)>0.174532925f){AmSubmit(f,"STOP",false);return;}
    if(f.weapon==1) {
        // Preserve the reusable sword at close range. Throw only on separation;
        // THROW itself clears #10 at65 and owns the projectile/guard/hit behavior.
        if(f.distance>450.0f&&f.distance<=900.0f){AmSubmit(f,"THROW",true);return;}
        if(f.distance>300.0f){AmSubmit(f,"FORWARD_G",false);return;}
        AmSubmit(f,"KEN_COMBO1",true);return;
    }
    if(f.weapon==2) {
        if(f.mp<20){AmCharge(f,30);return;}
        if(f.distance>900.0f){AmSubmit(f,"FORWARD_G",false);return;}
        if(f.distance<160.0f) {
            // Re-evaluate distance after the100-frame gun summon. Do not fire a stale plan.
            if(f.mp>=30)AmSubmit(f,"KEN_SUMMON",true,30);
            else AmSubmit(f,"GUN_OUT",true);
            return;
        }
        // Cthugha's long preparation is reserved for CM's stationary recovery.
        if(f.mp>=30&&f.distance>300.0f&&(AmIs(f.enemyName,"CHARGE")||AmIs(f.enemyName,"EMPTY")))
            AmSubmit(f,"CTHUGUA",true,30);
        else AmSubmit(f,"GUN",true,20);
        return;
    }
    if(f.distance<=140.0f){AmSubmit(f,"COMBO1",true);return;}
    if(f.distance<=450.0f) {
        if(f.mp>=30)AmSubmit(f,"KEN_SUMMON",true,30);
        else if(f.distance<=300.0f)AmSubmit(f,"COMBO1",true);
        else AmCharge(f,30);
        return;
    }
    // Summon20 plus legal initial GUN threshold20; charge retains the intended reserve.
    if(f.mp<40){AmCharge(f,40);return;}
    AmSubmit(f,"GUN_SUMMON",true,40);
}
static void AmTrace(const AmFrame& f) {
    DWORD now=GetTickCount();if(now-amState.lastLog<100)return;amState.lastLog=now;
    Log("AM_STATE t=%lu ticket=%u motion=%s frame=%d cat=%d enemy_motion=%s hp=%d enemyhp=%d mp=%d weapon=%d web=%d event=%d xz=%.3f dy=%.3f yaw=%.3f charge=%d reserve=%d pending=%s",
        now,amState.ticket,f.name,f.frame,f.category,f.enemyName,f.hp,f.enemyHp,f.mp,f.weapon,f.web,f.eventFlag,
        f.distance,f.dy,f.yaw*57.2957795131f,amState.charging,amState.reserve,amState.pending?amState.pending:"-");
    const char* nodes[]={"attack","GAN_R","GAN_L","baruzi_Null"};
    for(int i=0;i<4;++i) {
        BYTE* n=((MmFind_t)(amModule+0x15a00))(f.actor,nodes[i]);
        if(n){float* w=((MmWorld_t)(amModule+0x15930))(n);
            Log("AM_NODE ticket=%u node=%s xyz=%.4f,%.4f,%.4f target_lock=%.4f,%.4f,%.4f",
                amState.ticket,nodes[i],w[12],w[13],w[14],f.targetLock[12],f.targetLock[13],f.targetLock[14]);}
    }
}
static void __fastcall AmTick(BYTE* secondary,void*,DWORD dt,DWORD arg) {
    BYTE* ai=secondary-0x714;AmFrame f={};
    if(!AmRead(ai,&f)){amPreviousTick(secondary,dt,arg);return;}
    typedef void (__thiscall* UpdateFn)(BYTE*,DWORD);
    ((UpdateFn)(amModule+0x62580))(ai,dt);
    // Shared CAiBase observation/init only. No DEMONTWOSWORD tactic/dispatcher writes.
    if(AmIs(f.name,"START"))((AnPhaseFn)(amModule+0x64b20))(ai);
    else AmDecide(f);
    AmTrace(f);
}
static void ApplyAmAi(BYTE* obj,HMODULE module) {
    BYTE* b=(BYTE*)module;
    if(amModule==b&&*(void**)(b+0xe7ac8)==(void*)AmTick&&amPreviousTick)
        MmSwapSlot((void**)(b+0xe7ac8),(void*)AmTick,(void*)amPreviousTick);
    amEnabled=false;
    if(!duelBalance||!duelAutoFight||DuelAiTier()!=1||balScene!=670||*(int*)(obj+0x18)!=670)return;
    const BYTE head[]={0x8b,0x44,0x24,0x04,0x55,0x56,0x57,0x8b,0xf9,0x8d,0xb7,0xec,0xf8,0xff,0xff};
    if(memcmp(b+0x59750,head,sizeof head)||*(void**)(b+0xe7ac8)!=b+0x59750) {
        Log("AM_NATIVE DISABLED native fingerprint/slot mismatch");return;
    }
    wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack514_am_owned.dat",gameDir);
    if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES){Log("AM_NATIVE missing private pack");return;}
    *(int*)(obj+0x18)=9401;amModule=b;amPreviousTick=(MmTick_t)(b+0x59750);
    ZeroMemory(&amState,sizeof amState);
    amEnabled=MmSwapSlot((void**)(b+0xe7ac8),b+0x59750,(void*)AmTick);
    Log("AM_NATIVE enabled=%d source=670 scene=9401 side=0 policy=AM_CM_R1",amEnabled);
}
