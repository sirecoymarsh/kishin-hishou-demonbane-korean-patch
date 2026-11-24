#pragma once
#include "nero_player_camera_math.h"
// Display only: wrap the two native camera-matrix CALLs. No actor/node writes.
// position and rotation receive exactly the same cached matrix in one camera update.
typedef void (__thiscall* NcMatrix_t)(BYTE*,float*);
static BYTE* ncamModule;
static NcMatrix_t ncOriginal;
static BYTE* ncActor;
static bool ncReady;
static NcPose ncPose;
static DWORD ncUpdated,ncLogged;
static const char* NcDefinition(BYTE* actor)
{
 BYTE* def=actor?*(BYTE**)(actor+0x16c):NULL;
 return def?*(const char**)(def+0x34):NULL;
}
static bool NcEligible(BYTE* actor)
{
 return actor && ncamModule && GetModuleHandleW(battleModule)==(HMODULE)ncamModule &&
  (*(DWORD*)(actor+0x720)&3) && NcScope(duelBalance!=0,balScene,*(int*)(actor+0x724),
   *(float*)(actor+0xb6c),NcDefinition(actor));
}
static bool NcCompute(BYTE* actor,NcPose* out,DWORD now)
{
 if(!NcEligible(actor))return false;
 BYTE* mgr=*(BYTE**)(ncamModule+0x11e3b0);if(!mgr)return false;
 BYTE* enemy=((MmTarget_t)(ncamModule+0x39d60))(mgr,actor);
 if(!enemy || *(int*)(enemy+0x724)!=1 || !(*(DWORD*)(enemy+0x720)&3))return false;
 BYTE* a=*(BYTE**)(actor+0xb84),*b=*(BYTE**)(enemy+0xb84);if(!a||!b)return false;
 MmWorld_t world=(MmWorld_t)(ncamModule+0x15930);
 float am[16],bm[16];memcpy(am,world(a),sizeof am);memcpy(bm,world(b),sizeof bm);
 NcVec self={am[12],am[13]-110.0f,am[14]},other={bm[12],bm[13],bm[14]},half={250,220,250};
 const char* enemyDef=NcDefinition(enemy);
 if(NcDefinitionFamily(enemyDef,"qm")){other.y-=110.0f;half=NcVec{400,280,400};}
 else if(NcDefinitionFamily(enemyDef,"rm")||NcDefinitionFamily(enemyDef,"sm"))half=NcVec{600,600,600};
 else if(NcDefinitionFamily(enemyDef,"mm"))half=NcVec{110,110,110};
 float root[16];memcpy(root,world(actor),sizeof root);NcVec selfRight={root[0],0,root[2]};
 float dt=ncReady && ncActor==actor ? float(now-ncUpdated)*0.001f : 0.0f;
 if(dt>0.25f)dt=0.25f;
 return NcCompose(self,other,half,*(float*)(actor+0xb7c),ncReady&&ncActor==actor,ncPose.axis,ncPose.range,dt,out,selfRight);
}
static void __fastcall NcPositionMatrix(BYTE* actor,void*,float* out)
{
 ncOriginal(actor,out);
 __try {
  DWORD now=GetTickCount();NcPose next={};
  if(!NcCompute(actor,&next,now)){ncReady=false;return;}
  ncActor=actor;ncPose=next;ncReady=true;ncUpdated=now;memcpy(out,ncPose.matrix,sizeof ncPose.matrix);
  if(now-ncLogged>=1000){ncLogged=now;Log("NERO_P1_CAMERA source=%ld actor=%p range=%.2f eye=%.2f,%.2f,%.2f center=%.2f,%.2f,%.2f focal=%.1f",
   balScene,actor,ncPose.range,ncPose.eye.x,ncPose.eye.y,ncPose.eye.z,ncPose.center.x,ncPose.center.y,ncPose.center.z,*(float*)(actor+0xb7c));}
 } __except(EXCEPTION_EXECUTE_HANDLER){ncReady=false;}
}
static void __fastcall NcRotationMatrix(BYTE* actor,void*,float* out)
{
 ncOriginal(actor,out);
 __try {
  if(ncReady && ncActor==actor && NcEligible(actor))memcpy(out,ncPose.matrix,sizeof ncPose.matrix);
 } __except(EXCEPTION_EXECUTE_HANDLER){}
}
static void InstallNeroPlayerCamera(HMODULE module)
{
 ncReady=false;ncActor=NULL;ncUpdated=ncLogged=0;
 if(!module)return;
 BYTE* m=(BYTE*)module;
 const DWORD rvas[]={0x34d89,0x34dc9};void* hooks[]={(void*)NcPositionMatrix,(void*)NcRotationMatrix};
 BYTE original[2][5];bool ours[2]={};
 for(int i=0;i<2;++i){
  BYTE* at=m+rvas[i];if(at[0]!=0xe8){Log("NERO_P1_CAMERA disabled: call opcode mismatch");return;}
  BYTE* to=at+5+*(LONG*)(at+1);ours[i]=to==(BYTE*)hooks[i];
  if(!ours[i] && to!=m+0x34780){Log("NERO_P1_CAMERA disabled: call target mismatch");return;}
  memcpy(original[i],at,5);
 }
 ncamModule=m;ncOriginal=(NcMatrix_t)(m+0x34780);
 for(int i=0;i<2;++i){
  if(ours[i])continue;
  BYTE call[5]={0xe8,0,0,0,0};*(LONG*)(call+1)=(LONG)((BYTE*)hooks[i]-(m+rvas[i]+5));
  if(!WriteCode(m+rvas[i],call,5)){
   for(int j=0;j<i;++j)if(!ours[j])WriteCode(m+rvas[j],original[j],5);
   ncReady=false;Log("NERO_P1_CAMERA disabled: write failed");return;
  }
 }
 Log("NERO_P1_CAMERA installed: balanced P1 QM ordinary modes0/1/2 only; camera output matrices only");
}

