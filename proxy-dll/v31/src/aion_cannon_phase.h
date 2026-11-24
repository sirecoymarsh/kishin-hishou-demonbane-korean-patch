// User-approved exception: balanced Normal automatic Aion vs Nero only.
// Exact installed routes supply HP1000 actors. No attack/MP/frame data is set here.
#pragma once
static AionDecision AionCannonPhaseChoose(const AionPolicy& p,const AionSense& s,int hp)
{
    AionDecision d=AionChoose(p,s);
    if(hp>600 || hp<=0 || !s.alive || !s.idle || s.eventFlag!=0 ||
       s.mp<0 || s.weapon<0 || s.weapon>2 ||
       !(s.distance>=0.0f && s.distance<100000.0f) || !(fabsf(s.height)<100000.0f))return d;
    // Finish a blade already prepared; never start another preparation in this phase.
    if(s.weapon==1) {d.choice=AION_THROW;d.reserve=0;return d;}
    if(s.weapon==2) {d.choice=s.mp>=20?AION_THROW_MULTI:AION_RECOVER;d.reserve=20;return d;}
    // Begin saving the unchanged MP100 cost before the native HP40 unlock.
    // Bank only after any already prepared blade has been thrown.
    if(hp>400) {
        if(s.distance>400.0f) {d.choice=AION_MOVE;d.approachRange=350.0f;return d;}
        d.choice=AION_RECOVER;d.reserve=100;return d;
    }
    // Native STOP -> STOP_V -> STOP_X unlocks #44 at HP <= 400. Preserve that event.
    if(s.cannon!=1) {d.choice=AION_HOLD;d.reserve=0;return d;}
    // Existing pair-specific movement chooses the firing position. Save the full cost.
    if(s.distance>400.0f) {d.choice=AION_MOVE;d.approachRange=350.0f;return d;}
    d.reserve=100;d.choice=s.mp>=100?AION_CANNON:AION_RECOVER;
    return d;
}
