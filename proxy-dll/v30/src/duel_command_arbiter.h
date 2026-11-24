// Mechanism only: no tactical choices or combat writes.
#pragma once
#include <stdint.h>
enum DuelCommandPhase { DC_IDLE, DC_REQUESTED, DC_RUNNING, DC_RECOVERY };
enum DuelCommandEvent { DC_NONE, DC_STARTED, DC_COMPLETED, DC_REJECTED,
    DC_QUEUE_LOST, DC_LOST_UNDER_DAMAGE, DC_NATIVE_EVENT, DC_EXPIRED,
    DC_RECOVERED, DC_TARGET_CHANGED, DC_ENDED };
struct DuelCommand {
    DuelCommandPhase phase; DuelCommandEvent failure;
    unsigned ticket,issuedAt,lostSamples,recoverySamples; uintptr_t target;
    int hpAtIssue; bool damageObserved;
};
struct DuelCommandObservation {
    unsigned now; uintptr_t target; int hp;
    bool alive,nativeEvent,actual,queued,ready;
};
static bool DuelCommandBegin(DuelCommand& c,unsigned now,uintptr_t target,int hp)
{
    if(c.phase!=DC_IDLE)return false;
    ++c.ticket;c.phase=DC_REQUESTED;c.failure=DC_NONE;c.issuedAt=now;c.target=target;
    c.hpAtIssue=hp;c.damageObserved=false;c.lostSamples=0;c.recoverySamples=0;return true;
}
static DuelCommandEvent DuelCommandFail(DuelCommand& c,DuelCommandEvent reason)
{
    c.phase=DC_RECOVERY;c.failure=reason;c.recoverySamples=0;return reason;
}
static DuelCommandEvent DuelCommandObserve(DuelCommand& c,const DuelCommandObservation& o)
{
    if(c.phase==DC_IDLE)return DC_NONE;
    if(!o.alive) {c.phase=DC_IDLE;return DC_ENDED;}
    c.damageObserved=c.damageObserved || o.hp<c.hpAtIssue;
    if(c.phase==DC_RECOVERY) {
        if(o.ready && !o.nativeEvent && !o.queued && ++c.recoverySamples>=2) {
            c.phase=DC_IDLE;return DC_RECOVERED;
        }
        return DC_NONE;
    }
    if(c.target!=o.target)return DuelCommandFail(c,DC_TARGET_CHANGED);
    if(o.nativeEvent)return DuelCommandFail(c,DC_NATIVE_EVENT);
    if(c.phase==DC_REQUESTED) {
        if(o.actual) {c.phase=DC_RUNNING;return DC_STARTED;}
        if(o.queued)c.lostSamples=0;
        else if(++c.lostSamples>=2 && o.ready)
            return DuelCommandFail(c,c.damageObserved?DC_LOST_UNDER_DAMAGE:DC_QUEUE_LOST);
        if(o.ready && o.now-c.issuedAt>=1500)return DuelCommandFail(c,DC_EXPIRED);
        return DC_NONE;
    }
    // Native continuations and animation frames keep running unchanged.
    if(!o.actual && o.ready && !o.queued) {c.phase=DC_IDLE;return DC_COMPLETED;}
    return DC_NONE;
}


// Admit the original action dispatcher immediately after actual start. It owns
// normal release/continuation semantics; ownership still blocks new proposals.
static bool DuelCommandGatesDispatch(const DuelCommand& c)
{
    return c.phase==DC_REQUESTED || c.phase==DC_RECOVERY;
}
