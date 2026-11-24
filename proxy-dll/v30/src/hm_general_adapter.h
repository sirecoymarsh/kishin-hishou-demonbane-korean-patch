#pragma once
#include "hm_general_policy.h"
#include "hm_general_geometry.h"
static void HmGeneralDecide(const HmFrame& f) {
    BYTE* enemyMo=*(BYTE**)(f.target+0x398);
    HgInput x={hmSource,f.mp,enemyMo?*(int*)(enemyMo+0x54):0,f.distance,f.dy,0,HmGeneralContact(f),HmName(enemyMo)};
    const float tx=f.targetLock[12]-f.root[12],tz=f.targetLock[14]-f.root[14];
    x.yaw=atan2f(tx*f.root[0]+tz*f.root[2],tx*f.root[8]+tz*f.root[10]);
    const HgChoice c=HgPick(x);
    static const char* names[]={"","STOP","FORWARD_A","UP_A","DOWN_A","CHARGE","COMBO1","RAI_2","KAMA1","E_LANCHAR","H_CROW"};
    if(c.action==HG_NONE)return;
    const char* action=names[c.action];
    if(c.action==HG_CHARGE){hmState.charging=true;hmState.reserve=c.reserve;}
    // An ordinary idle frame must replace a stale attack request before yielding.
    // This is limited to these new routes; original MREQ executes all transitions.
    if(c.action==HG_STOP) {
        if(!HmIs(HmName(*(BYTE**)(f.actor+0x378)),"STOP"))
            ((AnNameFn)(hmModule+0x62000))(f.ai,"STOP");
    } else if(c.action>=HG_COMBO && HmQueued(f.actor,action)) {
        // The native direct slot persists after a completed action. When the new
        // policy deliberately selects that same skill, adopt the still-pending
        // request with a NEW ticket. Do not reset the native motion or queue.
        hmState.pending=action;hmState.pendingAt=GetTickCount();hmState.pendingFrame=f.frame;
        hmState.began=false;hmState.owned=action;++hmState.ticket;
        Log("HM_REQUEST ticket=%u action=%s attack=1 cost=%d mp=%d xz=%.3f dy=%.3f native_failed=0 retained=1",
            hmState.ticket,action,c.reserve,f.mp,f.distance,f.dy);
    } else HmSubmit(f,action,c.action>=HG_COMBO,c.reserve);
    static DWORD last;DWORD now=GetTickCount();
    if(now-last>=250 || c.action>=HG_COMBO) {
        last=now;
        Log("HM_GENERAL source=%d enemy=%s ef=%d choice=%s why=%s contact=%d yaw=%.3f xz=%.3f dy=%.3f mp=%d",
            hmSource,x.enemy,x.enemyFrame,action,c.reason,x.contact,x.yaw*57.2957795131f,x.distance,x.dy,x.mp);
    }
}

static void HmGeneralPostKo(const HmFrame& f) {
    // Only the new Kraken route: wait for the original skill to return to STOP.
    // Never interrupt an active skill, hit/catch/KO, or native event processing.
    if(hmSource!=722 || f.hp<=0 || f.enemyHp>0 || f.eventFlag!=0 ||
       !HmIs(f.name,"STOP") || !hmState.owned || !hmState.began)return;
    const char* direct=HmName(*(BYTE**)(f.actor+0x378));
    if(!HmIs(direct,hmState.owned))return;
    // Replace this policy's completed, persistent request using native MREQ.
    // No motion/frame/HP/MP writes and no synthetic winner/end request.
    ((AnNameFn)(hmModule+0x62000))(f.ai,"STOP");
    Log("HM_POSTKO_IDLE source=%d ticket=%u prior=%s current=%s frame=%d new_request=%s native_failed=%d",
        hmSource,hmState.ticket,direct,f.name,f.frame,
        HmName(*(BYTE**)(f.actor+0x378)),(int)*(BYTE*)(f.ai+0x331));
    Log("HM_FINISHED ticket=%u motion=%s next=%s mp=%d postko=1",
        hmState.ticket,hmState.owned,f.name,f.mp);
    hmState.pending=NULL;hmState.owned=NULL;hmState.began=false;
    hmState.charging=false;++hmState.cycle;
}
