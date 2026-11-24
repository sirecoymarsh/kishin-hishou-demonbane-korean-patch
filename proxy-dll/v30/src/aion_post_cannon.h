#pragma once
// R17: only the previously observed P2 post-cannon problem.
// No first-shot, player-side, combat-data or aiming changes.
struct AionPostCycle { bool inCannon,complete; int maxFrame; unsigned completions; };
static bool AionPostScope(const AionRoute* r,int side)
{
    return r && r->tier==1 && r->control==1 && r->source==845 &&
        r->loaded==7354 && r->destination==9109 && side==1;
}
static void AionPostObserve(AionPostCycle& p,bool cannon,int frame)
{
    if(cannon) {
        if(!p.inCannon)p.maxFrame=0;
        p.inCannon=true;
        if(frame>p.maxFrame)p.maxFrame=frame;
    } else if(p.inCannon) {
        if(p.maxFrame>=620) {p.complete=true;++p.completions;}
        p.inCannon=false;p.maxFrame=0;
    }
}
static bool AionPostEligible(const AionRoute* r,int side,const AionPostCycle& p,const AionSense& s,int hp)
{
    return AionPostScope(r,side) && p.complete && s.tier==1 && hp>0 && hp<=400 &&
        s.alive && s.eventFlag==0 && s.weapon==0 && s.cannon==1 &&
        s.mp>=0 && s.distance>=0.0f && s.distance<100000.0f && fabsf(s.height)<100000.0f;
}
static AionDecision AionPostChoose(const AionRoute* r,int side,const AionPostCycle& p,const AionSense& s,int hp,AionDecision d)
{
    if(!AionPostEligible(r,side,p,s,hp))return d;
    const AionPolicy& policy=r->actor[side].policy;
    d.reserve=100;
    // 100 units of approach hysteresis inside the existing 950-unit firing gate.
    d.approachRange=policy.fireRange-100.0f;
    if(!s.idle)d.choice=AION_HOLD;
    else if(s.distance>policy.fireRange)d.choice=AION_MOVE;
    else d.choice=s.mp>=100 ? AION_CANNON : AION_RECOVER;
    return d;
}
