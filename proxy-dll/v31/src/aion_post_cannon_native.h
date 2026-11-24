#pragma once
static const char* AnPostMotion(const AnFrame& f,int* frame=NULL)
{
    BYTE* motion=*(BYTE**)(f.actor+0x398);
    if(frame)*frame=motion?*(int*)(motion+0x54):-1;
    return motion?*(const char**)motion:NULL;
}
static void AnPostObserve(const AnFrame& f,AnState& s)
{
    if(!AionPostScope(anRoute,f.side))return;
    int frame=-1;const char* name=AnPostMotion(f,&frame);
    if(!name)return;
    unsigned before=s.post.completions;
    AionPostObserve(s.post,!_stricmp(name,"CANNON"),frame);
    if(s.post.completions!=before)
        Log("AION_POST_CANNON_COMPLETE side=%d cycles=%u motion=%s frame=%d hp=%d mp=%d xz=%.2f",
            f.side,s.post.completions,name,frame,*(int*)(f.actor+0x558),f.sense.mp,f.sense.distance);
}
static bool AnPostEligible(const AnFrame& f,const AnState& s)
{
    return AionPostEligible(anRoute,f.side,s.post,f.sense,*(int*)(f.actor+0x558));
}
static bool AnPostGroundSafe(const AnFrame& f,const char* name)
{
    // Do not cancel EMPTY/falling/landing, attacks, hits, throws or scripted events.
    if(!name || fabsf(f.self[13])>5.0f)return false;
    return !_strnicmp(name,"STOP",4) || !_stricmp(name,"FORWARD_G") ||
        !_stricmp(name,"BACK_G") || !_stricmp(name,"LMOVE_G") || !_stricmp(name,"RMOVE_G");
}
static void AnPostClearDirections(BYTE* ai)
{
    // AI intention slots only. Native actor input/physics/frame state is untouched.
    *(int*)(ai+0x20)=0;*(int*)(ai+0x24)=0;*(int*)(ai+0x28)=0;*(int*)(ai+0x2c)=0;
}
static void AnPostCommand(BYTE* ai,const AnFrame& f,AnState& s,const char* want,const char* current)
{
    if(!_stricmp(current,want)) {s.groundChoice=want;return;}
    if(s.groundChoice && !_stricmp(s.groundChoice,want))return;
    AnName(ai,want);
    if(!*(BYTE*)(ai+0x331))s.groundChoice=want;
    Log("AION_POST_COMMAND side=%d mode=%d motion=%s previous=%s failed=%d mp=%d xz=%.2f",
        f.side,(int)s.mode,want,current,(int)*(BYTE*)(ai+0x331),f.sense.mp,f.sense.distance);
}
static bool AnPostMovement(BYTE* ai,const AnFrame& f,AnState& s)
{
    if(!AnPostEligible(f,s) || (s.mode!=AN_APPROACH && s.mode!=AN_RECOVERY && s.mode!=AN_SETTLING))return false;
    const char* name=AnPostMotion(f);
    if(!AnPostGroundSafe(f,name)) {s.groundChoice=NULL;return true;}
    AnPostClearDirections(ai);
    if(s.mode==AN_SETTLING) {
        if(f.sense.idle) {s.mode=AN_NONE;s.groundChoice=NULL;AnStateSet(ai,0,0);}
        return true;
    }
    if(s.mode==AN_RECOVERY && f.sense.distance>f.route->policy.fireRange) {
        s.mode=AN_APPROACH;s.stopRange=f.route->policy.fireRange-100.0f;s.groundChoice=NULL;
    }
    if(s.mode==AN_APPROACH && f.sense.distance<=s.stopRange) {
        s.mode=AN_RECOVERY;s.reserve=100;s.groundChoice=NULL;
        Log("AION_POST_RANGE_REACHED side=%d xz=%.2f mp=%d",f.side,f.sense.distance,f.sense.mp);
    }
    if(s.mode==AN_APPROACH) {
        // Ground forward includes the original per-frame LOCKON. Never request a jump
        // or a nonexistent generic LTURN/RTURN, and never cancel on a two-second timer.
        AnPostCommand(ai,f,s,"FORWARD_G",name);
    } else if(f.sense.mp>=s.reserve) {
        if(f.sense.idle && !_strnicmp(name,"STOP",4)) {
            s.mode=AN_NONE;s.groundChoice=NULL;AnStateSet(ai,0,0);
            Log("AION_POST_READY side=%d mp=%d xz=%.2f",f.side,f.sense.mp,f.sense.distance);
        } else {
            // Release the one ground request once. Let native STOP become idle
            // before the cannon can own the actor; never overlap walk and fire.
            AnPostCommand(ai,f,s,"STOP_G",name);
        }
    } else {
        // R18: remain mobile inside cannon range using unchanged native moves.
        // STOP_X grants only 5 MP in its 200-frame loop; do not edit that data.
        // Keep a stable sidestep, not repeated starts that reset the MP keyframes.
        const bool wasBacking=s.groundChoice && !_stricmp(s.groundChoice,"BACK_G");
        const bool back=f.sense.distance<650.0f || (wasBacking && f.sense.distance<750.0f);
        const char* want=back ? "BACK_G" : ((s.sequence&1) ? "LMOVE_G" : "RMOVE_G");
        AnPostCommand(ai,f,s,want,name);
    }
    DWORD now=GetTickCount();
    if(now-s.lastMoveTrace>=500) {
        s.lastMoveTrace=now;
        Log("AION_POST_MOVE t=%lu side=%d mode=%d xz=%.2f mp=%d motion=%s idle=%d rootY=%.2f req=%d/%d/%d enabled=%d",
            now,f.side,(int)s.mode,f.sense.distance,f.sense.mp,name,(int)f.sense.idle,f.self[13],
            *(int*)(ai+0x24),*(int*)(ai+0x20),*(int*)(ai+0x28),*(int*)(ai+0x2c));
    }
    return true;
}
