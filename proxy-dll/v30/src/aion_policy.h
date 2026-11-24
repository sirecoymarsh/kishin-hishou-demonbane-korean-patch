// Pure Aion decisions. Thresholds are candidate AI choices, never combat stats.
#pragma once
#include <math.h>
enum AionChoice { AION_HOLD,AION_MOVE,AION_RECOVER,AION_SINGLE,AION_FULL_VOLLEY,AION_VOLLEY,AION_THROW,AION_THROW_MULTI,AION_SHOT,AION_CANNON,AION_COMBO,AION_KICK,AION_GRAB };
struct AionPolicy { float meleeRange,meleeHeight,fireRange,approachRange; int preferRanged; };
struct AionSense { bool idle,alive; int eventFlag,weapon,cannon,mp,tier; unsigned sequence; float distance,height; };
struct AionDecision { AionChoice choice; int reserve; float approachRange; };
static AionDecision AionChoose(const AionPolicy& p,const AionSense& s)
{
    AionDecision d={AION_HOLD,0,p.approachRange};
    if(!s.alive || !s.idle || s.eventFlag!=0 || s.mp<0 || s.weapon<0 || s.weapon>2 ||
       !(s.distance>=0.0f && s.distance<100000.0f) || !(fabsf(s.height)<100000.0f))return d;
    // #1 is the scripted end-event gate, NOT the actor's altitude/ground flag.
    if(s.weapon==2) {d.choice=s.mp>=20 ? AION_THROW_MULTI : AION_RECOVER;d.reserve=20;return d;}
    if(s.weapon==1) {
        d.choice=s.mp>=40 && (s.tier==2 || s.sequence%3!=0) ? AION_VOLLEY : AION_THROW;
        return d;
    }
    const bool melee=s.distance<=p.meleeRange && fabsf(s.height)<=p.meleeHeight;
    // Do not spend the remaining MP on repeated incomplete preparations.
    if(s.mp<30 && !melee) {d.choice=AION_RECOVER;d.reserve=30;return d;}
    if(s.distance>p.fireRange) {d.choice=AION_MOVE;return d;}
    const unsigned slot=s.sequence%6;
    if(s.cannon==1 && s.mp>=100 && (s.tier==2 || slot==0)) {d.choice=AION_CANNON;return d;}
    if(melee && (!p.preferRanged || s.mp<30) && (slot%2==0 || s.mp<30)) {
        d.choice=slot==4 && s.mp>=40 ? AION_GRAB : (s.tier==0 ? AION_KICK : AION_COMBO);
        return d;
    }
    if(s.mp>=70 && (s.tier==2 || slot==1 || slot==4)) {d.choice=AION_FULL_VOLLEY;return d;}
    if(s.mp>=30 && (slot!=3 || s.tier==0)) {d.choice=AION_SINGLE;return d;}
    if(s.mp>=10) {d.choice=AION_SHOT;return d;}
    d.choice=AION_RECOVER;d.reserve=30;return d;
}
static const char* AionMotion(AionChoice c)
{
    switch(c) {
    case AION_SINGLE:return "AN_KEN_SINGLE";
    case AION_FULL_VOLLEY:return "AN_KEN_FULL";
    case AION_VOLLEY:return "AN_KEN_VOLLEY";
    case AION_THROW:return "KEN_OUT";
    case AION_THROW_MULTI:return "KENBU2";
    case AION_SHOT:return "SHOT";
    case AION_CANNON:return "CANNON";
    case AION_COMBO:return "AN_COMBO1";
    case AION_KICK:return "KICK";
    case AION_GRAB:return "syakunetu1";
    default:return 0;
    }
}

// Recheck when native idle dispatch actually consumes the request. Resource or
// target changes between selection and dispatch must not start half a chain.
static bool AionCanStart(AionChoice c,const AionPolicy& p,const AionSense& s)
{
    if(!s.idle || !s.alive || s.eventFlag!=0 || s.mp<0)return false;
    switch(c) {
    case AION_SINGLE:return s.weapon==0 && s.mp>=30;
    case AION_FULL_VOLLEY:return s.weapon==0 && s.mp>=70;
    case AION_VOLLEY:return s.weapon==1 && s.mp>=40;
    case AION_THROW:return s.weapon==1;
    case AION_THROW_MULTI:return s.weapon==2 && s.mp>=20;
    case AION_SHOT:return s.weapon==0 && s.mp>=10;
    case AION_CANNON:return s.weapon==0 && s.cannon==1 && s.mp>=100;
    case AION_COMBO:case AION_KICK:case AION_GRAB:
        return s.weapon==0 && s.distance<=p.meleeRange && fabsf(s.height)<=p.meleeHeight &&
            (c!=AION_GRAB || s.mp>=40);
    default:return false;
    }
}
