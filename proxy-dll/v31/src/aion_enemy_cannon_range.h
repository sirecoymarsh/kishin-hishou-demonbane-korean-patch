#pragma once
// R16: P2 automatic Aion vs Nero only. Reuse the existing pair fireRange950.
// The R12 distance400 restriction was based on one segment's travel, not the 8-node beam.
static AionDecision AionEnemyCannonRange(const AionRoute* r,int side,const AionSense& s,int hp,AionDecision d)
{
    if(!r||r->tier!=1||r->control!=1||r->source!=845||r->loaded!=7354||r->destination!=9109||side!=1||s.tier!=1)return d;
    if(hp<=0||hp>400||d.choice!=AION_MOVE)return d;
    const AionPolicy& p=r->actor[1].policy;
    if(!(s.distance>=0.0f&&s.distance<=p.fireRange)||!(fabsf(s.height)<100000.0f))return d;
    if(!AionCanStart(AION_CANNON,p,s))return d;
    d.choice=AION_CANNON;d.reserve=100;return d;
}
