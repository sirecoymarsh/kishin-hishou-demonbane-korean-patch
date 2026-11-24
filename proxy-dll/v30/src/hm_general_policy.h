// Codex-authored HM policy. Only Normal automatic P1 HM-GM / HM-LM.
#pragma once
#include <math.h>
#include <string.h>
enum HgAction { HG_NONE,HG_STOP,HG_FORWARD,HG_UP,HG_DOWN,HG_CHARGE,HG_COMBO,HG_RAI,HG_KAMA,HG_LAUNCH,HG_CROW };
struct HgInput {int source,mp,enemyFrame;float distance,dy,yaw;bool contact;const char* enemy;};
struct HgChoice {HgAction action;int reserve;const char* reason;};
static bool HgName(const char* a,const char* b){return a && !strcmp(a,b);}
static int HgRoute(bool balance,bool automatic,int tier,int source,int loaded) {
    if(!balance || !automatic || tier!=1)return 0;
    if(source==720 && loaded==720)return 9781;
    if(source==722 && loaded==722)return 9782;
    return 0;
}
static HgChoice HgPick(const HgInput& x) {
    HgChoice c={HG_NONE,0,"out-of-scope"};
    if(x.source!=720 && x.source!=722)return c;
    // STOP uses the original broad-aperture acquisition, never a yaw write.
    if(fabsf(x.yaw)>=1.0471975512f){c.action=HG_STOP;c.reason="align-native-stop";return c;}
    const bool gm=x.source==720;
    const bool longGM=gm && HgName(x.enemy,"KINKI1") && x.enemyFrame<150;
    const bool longLM=!gm && ((HgName(x.enemy,"ICE") && x.enemyFrame<250) ||
        (HgName(x.enemy,"9HeadDragon") && x.enemyFrame<420));
    const bool recoveringGM=gm && ((HgName(x.enemy,"COMBO1") && x.enemyFrame>=30 && x.enemyFrame<60) ||
        (HgName(x.enemy,"COMBO2") && x.enemyFrame>=20 && x.enemyFrame<60) ||
        (HgName(x.enemy,"KICK") && x.enemyFrame>=30 && x.enemyFrame<85));
    const bool recoveringLM=!gm && HgName(x.enemy,"COMBO1") && x.enemyFrame>=34 && x.enemyFrame<50;
    if(x.contact && x.mp>=5 && (recoveringGM||recoveringLM)) {
        c.action=HG_RAI;c.reserve=5;c.reason="contact-and-recovery-window";return c;
    }
    if(x.contact && x.mp>=10 && (longGM||longLM)) {
        c.action=HG_KAMA;c.reserve=10;c.reason="contact-and-long-enemy-commit";return c;
    }
    if(x.contact) {c.action=HG_COMBO;c.reason="contact-zero-cost-chain";return c;}
    // Only near attacks require lock-height alignment. Native guided projectiles
    // aim at the target themselves; chasing a jumping target's height first is wasteful.
    if(x.distance<=300.0f && fabsf(x.dy)<=12.5f) {
        c.action=HG_COMBO;c.reason=gm?"gm-intercept-native-lunge":"lm-close-native-lunge";return c;
    }
    if(x.distance<=1000.0f && x.distance>300.0f && x.mp>=40 &&
       (longGM || (!gm && HgName(x.enemy,"9HeadDragon") && x.enemyFrame<300))) {
        c.action=HG_LAUNCH;c.reserve=40;c.reason=gm?"gm-dragon-startup-punish":"lm-dragon-startup-punish";return c;
    }
    if(x.distance>=200.0f && x.distance<=800.0f && x.mp>=40) {
        c.action=HG_CROW;c.reserve=40;c.reason=gm?"gm-mobile-guided-intercept":"lm-ranged-guided-pressure";return c;
    }
    if(x.mp<5 || (x.mp<40 && x.distance>300.0f)) {
        c.action=HG_CHARGE;c.reserve=x.distance>300.0f?40:10;
        c.reason="native-charge-reserve";return c;
    }
    if(fabsf(x.dy)>12.5f) {c.action=x.dy>0?HG_UP:HG_DOWN;c.reason="live-lock-height";return c;}
    c.action=HG_FORWARD;c.reason=gm?"gm-close-intercept":"lm-close-after-cast";return c;
}
