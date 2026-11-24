#pragma once
#include "aion_cannon_aim_math.h"
// R15: private balanced Normal automatic EM/QM routes, CANNON only.
// Whole weapon subtree: visible weapon, beam meshes, and all eight emitters.
// No actor root, hit shape, resource, attack-frame, or projectile-speed writes.
struct AcPose {BYTE* actor;BYTE* weapon;float original[3],written[3];int lastFrame;};
static AcPose acPose[2];
static bool AcOwner(BYTE* actor,BYTE** target,int* side){
 if(!anEnabled||!anRoute||!duelBalance||DuelAiTier()!=1||!duelAutoFight||
    anRoute->tier!=1||anRoute->control!=1||balScene!=anRoute->source||
    GetModuleHandleW(battleModule)!=(HMODULE)anModule||!actor)return false;
 int s=*(int*)(actor+0x724);if(s<0||s>1)return false;
 const AionActorRoute& route=anRoute->actor[s];
 if(!route.definition||!MmDefinition(actor,route.definition))return false;
 if(!AionInstallScope(duelBalance!=0,(int)DuelAiTier(),duelAutoFight?1:0,balScene,anRoute,s))return false;
 BYTE* manager=*(BYTE**)(anModule+0x11e3b0);
 BYTE* enemy=manager?((MmTarget_t)(anModule+0x39d60))(manager,actor):NULL;
 if(!enemy||!MmDefinition(enemy,route.targetDefinition)||*(int*)(enemy+0x724)!=1-s)return false;
 *target=enemy;*side=s;return true;
}
static void AcDirty(BYTE* node){
 node[0x94]&=0xfc;((void(__cdecl*)(BYTE*))(anModule+0x15540))(node);
}
static void AcRestore(BYTE* actor){
 for(int s=0;s<2;++s){
  AcPose& p=acPose[s];if(p.actor!=actor||!p.weapon)continue;
  // Verify current tree membership and our exact last write before restoration.
  BYTE* now=((MmFind_t)(anModule+0x15a00))(actor,"I_wepon");
  if(now==p.weapon&&!memcmp(now+0x14,p.written,sizeof p.written)){
   memcpy(now+0x14,p.original,sizeof p.original);AcDirty(now);
  }
  p.weapon=NULL;
 }
}
static void AcAimWeapon(BYTE* actor){
 BYTE* target=NULL;int side=-1;
 if(!AcOwner(actor,&target,&side)||*(int*)(actor+0x558)<=0||*(int*)(target+0x558)<=0)return;
 BYTE* motion=*(BYTE**)(actor+0x398);
 if(!motion||!*(const char**)motion||strcmp(*(const char**)motion,"CANNON"))return;
 int frame=*(int*)(motion+0x54);
 if(frame<240||frame>480||actor[0xbb9]||*(int*)(actor+0xb58))return;
 MmFind_t find=(MmFind_t)(anModule+0x15a00);MmWorld_t world=(MmWorld_t)(anModule+0x15930);
 BYTE* weapon=find(actor,"I_wepon");BYTE* muzzle=find(actor,"canonn_Null1");BYTE* hit=find(target,"collid_body");
 if(!weapon||!muzzle||!hit||!*(BYTE**)(weapon+0x98))return;
 // The source DMO has zero pivot/preorientation for this exact weapon node.
 for(int k=0;k<3;++k)if(fabsf(*(float*)(weapon+0x34+4*k))>.00001f||fabsf(*(float*)(weapon+0x44+4*k))>.00001f)return;
 float pm[16],wm[16],mm[16],hm[16];
 memcpy(pm,world(*(BYTE**)(weapon+0x98)),sizeof pm);memcpy(wm,world(weapon),sizeof wm);
 memcpy(mm,world(muzzle),sizeof mm);memcpy(hm,world(hit),sizeof hm);
 // QM.dmo collid_body native box center, not the actor root or box node origin.
 AcVec aim=AcPoint(hm,AcV(0,345.75f,22.125f));
 AcVec scale=AcV(*(float*)(weapon+0x24),*(float*)(weapon+0x28),*(float*)(weapon+0x2c));
 float weight=frame<270?(frame-240)/30.0f:frame>450?(480-frame)/30.0f:1.0f;
 AcAim solved={};bool ok=AcSolve(pm,wm,mm,aim,scale,weight,&solved);
 if(!ok){if(frame%30==0)Log("AION_CANNON_AIM_REJECT side=%d frame=%d muzzle=%.2f,%.2f,%.2f hit=%.2f,%.2f,%.2f forward=%.3f,%.3f,%.3f",side,frame,mm[12],mm[13],mm[14],aim.x,aim.y,aim.z,-mm[8],-mm[9],-mm[10]);return;}
 AcPose& p=acPose[side];p.actor=actor;p.weapon=weapon;
 memcpy(p.original,weapon+0x14,sizeof p.original);
 const float angles[3]={solved.angles.x,solved.angles.y,solved.angles.z};
 memcpy(weapon+0x14,angles,sizeof angles);memcpy(p.written,angles,sizeof angles);AcDirty(weapon);
 if(frame%10==0||p.lastFrame<270&&frame>=270){
  float after[16];memcpy(after,world(muzzle),sizeof after);
  Log("AION_CANNON_AIM side=%d frame=%d hp=%d/%d pitch=%.4f->%.4f delta=%.4f range=%.2f origin=%.3f,%.3f,%.3f hit=%.3f,%.3f,%.3f dir=%.5f,%.5f,%.5f",side,frame,*(int*)(actor+0x558),*(int*)(target+0x558),solved.pitchBefore,solved.pitchAfter,solved.delta,solved.forwardRange,after[12],after[13],after[14],aim.x,aim.y,aim.z,-after[8],-after[9],-after[10]);
  BYTE* beam=find(actor,"M_beam4");if(beam){
   const float* b=world(beam);Log("AION_CANNON_VISUAL side=%d frame=%d beam=%.4f,%.4f,%.4f forward=%.5f,%.5f,%.5f weapon=%p muzzle=%p targetHit=%p",side,frame,b[12],b[13],b[14],-b[8],-b[9],-b[10],weapon,muzzle,hit);
  }
 }
 p.lastFrame=frame;
}
static void AcObserveProjectile(BYTE* projectile){
 if(!anEnabled||!anRoute)return;
 const char* def=NgDefinition(projectile);
 if(!NgPrefix(def,"AN_T1_QM_CANON"))return;
 BYTE* owner=*(BYTE**)(projectile+0x6f8);BYTE* target=NULL;int side=-1;
 if(!AcOwner(owner,&target,&side))return;
 BYTE* motion=*(BYTE**)(projectile+0x398);const char* name=motion?*(const char**)motion:NULL;
 if(!name)return;int frame=*(int*)(motion+0x54);
 if(frame>2||strcmp(name,"SHOOT")&&strcmp(name,"BREAK")&&strcmp(name,"BREAK2"))return;
 float* w=((MmWorld_t)(anModule+0x15930))(projectile);
 Log("AION_CANNON_CONTACT side=%d ptr=%p motion=%s frame=%d hp=%d enemy_hp=%d pos=%.3f,%.3f,%.3f fwd=%.5f,%.5f,%.5f def=%s",side,projectile,name,frame,*(int*)(owner+0x558),*(int*)(target+0x558),w[12],w[13],w[14],-w[8],-w[9],-w[10],def);
}
static void __fastcall AcBeforePose(BYTE* actor,void*){
 __try{AcRestore(actor);}__except(EXCEPTION_EXECUTE_HANDLER){Log("AION_CANNON_RESTORE exception; original update retained");}
 ((AnPhaseFn)(anModule+0x35ba0))(actor);
}
static void __fastcall AcBeforeMaterials(BYTE* actor,void*){
 __try{AcAimWeapon(actor);AcObserveProjectile(actor);}__except(EXCEPTION_EXECUTE_HANDLER){Log("AION_CANNON_AIM exception; original materials retained");}
 ((AnPhaseFn)(anModule+0x38d00))(actor);
}
