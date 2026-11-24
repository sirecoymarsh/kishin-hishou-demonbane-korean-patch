// v11: private hard PLAYER2 data + native, one-shot special requests.
// All original damage/motion executors remain in charge. No forced motion/frame/position writes.
#pragma once
struct HvSkill { const char* code;const char* name;int mp;float nearDist,farDist,height;int flag; };
static const HvSkill hvSkills[]={
 {"am","AS",50,0,280,160,0},{"am","SINJUU",80,120,1700,700,0},
 {"bm","ABRAHADABRA",40,0,220,130,0},{"cm","GC_FIRE",50,0,1800,700,0},
 {"dm","ABRA",40,0,200,130,0},{"dm","H_B",50,0,650,180,0},
 {"em","SHONETSU1",40,0,180,130,1},{"em","CANNON",100,180,1800,700,2},
 {"fm","AS",50,0,600,400,0},{"fm","CTHGUA_ITACUA",80,150,1800,700,0},
 {"gm","KINKI1",60,0,1700,400,0},{"gm","syakunetu1",50,0,650,200,0},
 {"hm","mayoku",80,0,200,130,0},{"im","TAIZAI",70,0,200,400,6},
 {"im","ONRYOU",80,0,1800,700,5}, // 11-03 v13 flag 5 only against Lord Byakhee, 6 never (사용자 「원령 주탄을 쓰게」)
 {"jm","D_C",60,0,2200,900,0},
 {"km","LASER_MAX1",0,0,1800,700,0},{"km","LASER_ALL",0,0,1500,700,0},
 {"lm","A_ARM2",30,0,850,350,0},{"lm","9HeadDragon",40,0,1700,800,0},
 {"mm","B_ATTACK",30,0,220,150,0},{"mm","S_BARD",50,0,1800,500,0},
 {"nm","ASHURA_1",30,0,230,130,0},{"nm","HIRYUU",30,180,1700,550,0},
 {"nm","RASETSU_KAMAE",30,0,280,130,3},{"pm","YOG",80,0,6000,3000,0},
 {"qm","JUUSIN1",0,0,2200,1000,0},{"qm","KOKUIN666",100,0,6000,3000,4},
 {"rm","SENSOU",0,0,2200,1000,0},{"sm","Dark",0,0,2200,1000,0}
};
static BYTE* hvModule;
static bool hvEnabled,hvObserve;
static LONG hvInput=-1,hvResolved=-1,hvDestination=-1;
static int hvMpLogs[2],hvHitLogs;
struct HvState{BYTE* actor;const char* pending;const char* active;DWORD requested,nextAt;unsigned cycle;bool started;};
static HvState hvState;
static BYTE* hvOwnerActor;static bool hvOwnerActive;static bool hvOldLogged;
static const char* hvSupport;
typedef void(__thiscall* HvUpdateFn)(BYTE*);
typedef void(__thiscall* HvSetFn)(BYTE*,int,int);
typedef bool(__thiscall* HvRequestFn)(BYTE*,const char*,int);
static const char* HvName(BYTE* a){BYTE* m=*(BYTE**)(a+0x398);return m?*(const char**)m:"";}
static bool HvFree(const char* n){
 return n && (!_stricmp(n,"STOP")||!_stricmp(n,"STOP_G")||!_stricmp(n,"STOP_A")||!_stricmp(n,"STOP_X")||
 !_stricmp(n,"CHARGE")||!_strnicmp(n,"GUARD",5)||!_strnicmp(n,"FORWARD",7)||!_strnicmp(n,"BACK_",5)||!_strnicmp(n,"LMOVE",5)||!_strnicmp(n,"RMOVE",5));
}
// 11-05 (user, Ambrosius mirror, hard, below 40%: 「상대는 가만히 있어」): the vertical support moves HvThink asks for itself
// (UP_A / DOWN_A when the special's height window is missed) were not free, so after asking DOWN_A at 349 above PLAYER1 it
// never thought again - DOWN_A loops on itself (HM: jump VOID 45 15) and stood on the floor for 80 s until KO. Free for
// HvThink only (HvFree also gates Tiberius's mix and the Rasetsu target test).
static bool HvOwnMove(const char* n){return HvFree(n) || (n && (!_stricmp(n,"UP_A")||!_stricmp(n,"DOWN_A")));}
static const char* HvDefinition(BYTE* a){BYTE* d=*(BYTE**)(a+0x16c);return d?*(const char**)(d+0x34):NULL;}
static void HvWithdraw(BYTE* a,const char* name){
 if(!name)return;
 // Only slots containing our own proposal. Never touches the executing motion or another request.
 for(int j=0;j<2;++j)for(int i=0;i<(j?5:3);++i){
  BYTE** p=(BYTE**)(a+(j?0x2b8:0x378)+i*8);BYTE* m=*p;
  if(m && !_stricmp(*(const char**)m,name)){*p=NULL;*(DWORD*)(p+1)=0;}
 }
}
static void HvThink(BYTE* a){
 if(!hvEnabled || !duelBalance || duelDifficulty!=2 || balScene<0 || *(int*)(a+0x724)!=1)return;
 const char* def=HvDefinition(a);if(!def)return;
 char path[512];if(strcpy_s(path,def))return;_strlwr_s(path);for(char* p=path;*p;++p)if(*p=='\\')*p='/';
 if(!strstr(path,"data/hv11/r"))return; // root actor, never a projectile
 const char* code=strstr(path,"/character/");if(!code)return;code+=11;
 BYTE* mgr=*(BYTE**)(hvModule+0x11e3b0);if(!mgr)return;
 BYTE* target=((MmTarget_t)(hvModule+0x39d60))(mgr,a);
 const char* name=HvName(a);DWORD now=GetTickCount();
 if(hvState.actor!=a){ZeroMemory(&hvState,sizeof hvState);hvState.actor=a;hvState.nextAt=now;}
 if(!target || *(int*)(a+0x558)<=0 || *(int*)(target+0x558)<=0 || *(int*)(a+0x3cc)!=0){
  HvWithdraw(a,hvState.pending);hvState.pending=NULL;hvState.active=NULL;return;
 }
 if(hvState.pending){
  if(!_stricmp(name,hvState.pending)){
   hvState.started=true;hvState.active=hvState.pending;
   Log("HV11_START t=%lu source=%ld action=%s mp=%d hp=%d",now,balScene,name,*(int*)(a+0x55c),*(int*)(a+0x558));
   HvWithdraw(a,hvState.pending);hvState.pending=NULL;
  }else if(!HvOwnMove(name) || now-hvState.requested>1200){
   Log("HV11_WITHDRAW t=%lu requested=%s actual=%s",now,hvState.pending,name);
   HvWithdraw(a,hvState.pending);hvState.pending=NULL;hvState.nextAt=now+2000;
  }
  return;
 }
 if(hvState.active){
  if(!HvOwnMove(name))return;
  Log("HV11_FINISH t=%lu action=%s next=%s hp=%d enemy_hp=%d",now,hvState.active,name,*(int*)(a+0x558),*(int*)(target+0x558));
  hvState.active=NULL;hvState.nextAt=now+1000;
 }
 if(!HvOwnMove(name) || (LONG)(now-hvState.nextAt)<0)return;
 MmWorld_t world=(MmWorld_t)(hvModule+0x15930);
 const float* aw=world(a);const float* bw=world(target);
 float dx=bw[12]-aw[12],dz=bw[14]-aw[14],dy=bw[13]-aw[13],dist=sqrtf(dx*dx+dz*dz);
 if(!_finite(dist)||!_finite(dy))return;
 int mp=*(int*)(a+0x55c);const unsigned count=(unsigned)_countof(hvSkills);
 const bool vsMm=imOnryouModule && ImDefIn(target,"/character/mm/"); // 11-03 v13 rows flag 5/6 (DUEL only, as the swap)
 for(unsigned j=0;j<count;++j){
  unsigned i=(hvState.cycle+j)%count;const HvSkill& s=hvSkills[i];
  if(_strnicmp(code,s.code,2)||code[2]!='/'||mp<s.mp||dist<s.nearDist||dist>s.farDist||fabsf(dy)>s.height)continue;
  if(s.flag==1 && *(int*)(a+0x3c8+42*4)!=0)continue;
  if(s.flag==2 && *(int*)(a+0x3c8+44*4)!=0)continue;
  if(s.flag==3 && HvFree(HvName(target)))continue;
  if(s.flag==4 && *(int*)(a+0x558)>*(int*)(a+0x560)*35/100)continue;
  if((s.flag==5 || s.flag==6) && (s.flag==5)!=vsMm)continue;
  // Neutral recovery only: discard stale, unstarted AI proposals before choosing the new action.
  for(int q=0;q<5;++q){*(BYTE**)(a+0x2b8+q*8)=NULL;*(DWORD*)(a+0x2bc+q*8)=0;}
  bool accepted=false;
  for(int slot=0;slot<3;++slot)accepted=((HvRequestFn)(hvModule+0x307f0))(a,s.name,slot)||accepted;
  if(!accepted)continue;
  hvState.pending=s.name;hvState.requested=now;hvState.cycle=(i+1)%count;hvState.nextAt=now+2000;
  Log("HV11_REQUEST t=%lu source=%ld actor=%p action=%s mp=%d dist=%.2f dy=%.2f previous=%s",now,balScene,a,s.name,mp,dist,dy,name);return;
 }
 // No legacy AI below the threshold. Recover or move into an eligible special's native range.
 int reserve=101;float bestHeight=0;float bestRange=0;
 for(unsigned k=0;k<count;++k){const HvSkill& s=hvSkills[k];if(_strnicmp(code,s.code,2)||code[2]!='/')continue;
  if(s.flag==4)continue;if((s.flag==5 || s.flag==6) && (s.flag==5)!=vsMm)continue;if(s.mp<reserve)reserve=s.mp;
  if(s.farDist>bestRange)bestRange=s.farDist;if(s.height>bestHeight)bestHeight=s.height;
 }
 const char* options[3]={NULL,NULL,NULL};
 if(mp<reserve){options[0]="CHARGE";options[1]="STOP_G";options[2]="STOP_A";}
 else if(fabsf(dy)>bestHeight){options[0]=dy>0?"UP_A":"DOWN_A";options[1]="FORWARD_G";options[2]="FORWARD_A";}
 else {options[0]="FORWARD_G";options[1]="FORWARD_A";options[2]="FORWARD";}
 for(unsigned k=0;k<3;++k){if(!options[k])continue;bool accepted=false;
  for(int slot=0;slot<3;++slot)accepted=((HvRequestFn)(hvModule+0x307f0))(a,options[k],slot)||accepted;
  if(accepted){if(hvSupport!=options[k])Log("HV11_SUPPORT t=%lu action=%s mp=%d reserve=%d dist=%.2f dy=%.2f range=%.1f",now,options[k],mp,reserve,dist,dy,bestRange);hvSupport=options[k];break;}
 }
 hvState.nextAt=now+250;
}
// 11-04 v15/v16 (user: Kraken / Aion against Lord Byakhee keep shooting below 40% HP — at the finisher threshold they
// should use their finishers; Aion throws Barzai's scimitar instead of the gun; then 「로드 비야키 상대로 아마 다들 원거리
// 공격을 하게 만들었을텐데 비슷하게 고쳐줄래?」). The hard-mode automatic PLAYER2 definitions of the Lord Byakhee fights
// listed below read #151 in their idle/move ranged lines (dmpack531_mm_finishers.dat; was #101 = MP). Mirror: MP while HP
// is above 40% of max (unchanged behaviour), -1 at or below (the handoff point; -1 also stops Nero's `>=0` line) — the ranged
// lines stop and the hard-mode special requests (HvThink) can gather MP.
static const int hvMirrorScenes[]={510,511,512,514,515,516,517,519,520,521,522,523,808,809,810,812};
static void HvMirrorRanged(BYTE* a){
 if(!hvEnabled || *(int*)(a+0x724)!=1)return;
 const char* def=HvDefinition(a);if(!def)return;
 char path[512];if(strcpy_s(path,def))return;_strlwr_s(path);for(char* p=path;*p;++p)if(*p=='\\')*p='/';
 if(!strstr(path,"data/hv11/r"))return;
 bool listed=false;
 for(unsigned i=0;i<_countof(hvMirrorScenes) && !listed;++i){char want[32];sprintf_s(want,"/pair%dp1r1",hvMirrorScenes[i]);listed=strstr(path,want)!=NULL;}
 if(!listed)return;
 const int hp=*(int*)(a+0x558),maximum=*(int*)(a+0x560),mp=*(int*)(a+0x55c);
 const bool pinch=hp>0 && maximum>0 && hp*100<=maximum*40;
 static BYTE* lastActor;static int lastPinch=-1;
 if(a!=lastActor || (int)pinch!=lastPinch){
  Log("HV16_RANGED_MIRROR t=%lu actor=%p hp=%d max=%d mp=%d pinch=%d (#151 = %s) def=%.80s",GetTickCount(),a,hp,maximum,mp,(int)pinch,pinch?"-1: ranged lines off":"MP",path);
  lastActor=a;lastPinch=(int)pinch;
 }
 *(int*)(a+0x3c8+151*4)=pinch?-1:mp;
}
static void __fastcall HvUpdate(BYTE* a,void*){
 __try{HvMirrorRanged(a);}__except(EXCEPTION_EXECUTE_HANDLER){}
 GrabTrace(a,"before");
 KokuinNoGrab(a);  // 11-08 v22: the hard-mode enemy Nero's KOKUIN666 is not taken by a DUEL grab (before the update reads #380)
 int hp=*(int*)(a+0x558);float raw=*(float*)(a+0x718),guard=*(float*)(a+0x71c);
 ((HvUpdateFn)(hvModule+0x333a0))(a);
 GrabTrace(a,"after");
 IceFollow(a);     // 11-03 v13 Kraken PLAYER1 vs Lord Byakhee: the forming ice keeps over the caster
 ArmAim(a);        // 11-03 v13 Kraken PLAYER1 vs D Blood (balance mode): the bind arm points at D Blood's lockOn
 BladeHit(a);      // 11-08 v21 (balance mode): Nero's sword spell / Lightbringer hit wherever the drawn blade crosses the enemy
 KuzuUpdate(a);    // 11-03 v13 Kraken PLAYER1's 9-head dragon (balance mode): turn to the target, arm out 2x, damage 3x
 OnryouFast(a);    // 11-08 v23 hard-mode enemy Beelzebub's 원령 주탄: the charge (frames 1~560) at about twice its speed
 NeroApproachStep(a); // 11-08 v23 a Nero AI steps in to a robot held by her bind, then asks the sword spell
 AltLift(a);       // 11-03 v13 (balance mode): a robot that cannot fly rises to Nero's / the Clockwork Phantom's lock node height
 IceTraceActor(a); // 11-03 v13 test only (ice_trace=1)
 AltTraceActor(a); // 11-03 v13 test only (alt_trace=1): the robots' root and lock node heights
 SwordTraceActor(a); // 11-08 v21 test only (sword_trace=1): where Nero's sword spell / Lightbringer swords go
 ZyudanSteer(a);   // 11-08 v24 (balance mode): Beelzebub's curse bullet kept on the line to the target's lock node
 ZyudanHold(a);    // 11-09 v25: a DUEL curse bullet holding a robot is carried into its lock node (the robot inside the curse)
 ZyudanTraceActor(a); // 11-08 v23 test only (zyudan_trace=1): where Beelzebub's curse bullet goes
 TestGapActor(a);  // 11-08 v24 test only (test_gap=N): PLAYER2 put N away at its first stand
 __try{
  if(hvObserve && hp>*(int*)(a+0x558) && hvHitLogs++<200)
   Log("HV11_HIT t=%lu hard=%d side=%d raw=%.3f guard=%.3f hp=%d->%d motion=%s",GetTickCount(),(int)hvEnabled,*(int*)(a+0x724),raw,guard,hp,*(int*)(a+0x558),HvName(a));
 }__except(EXCEPTION_EXECUTE_HANDLER){hvEnabled=false;Log("HV11 disabled after unreadable actor state");}
}
static void __fastcall HvParams(BYTE* a,void*){
 int before=*(int*)(a+0x55c);
 ((HvUpdateFn)(hvModule+0x35a90))(a); // original timed set_param/add_param interpreter
 int value=*(int*)(a+0x55c),side=*(int*)(a+0x724);
 if(hvObserve && value>before && (side==0||side==1) && *(BYTE**)(a+0x398) && hvMpLogs[side]++<100)
  Log("HV11_MP t=%lu hard=%d side=%d before=%d raw_after=%d delta=%d motion=%s frame=%d",GetTickCount(),(int)hvEnabled,side,before,value,value-before,HvName(a),*(int*)(*(BYTE**)(a+0x398)+0x54));
}
static bool HvCall(BYTE* at,BYTE* original,void* fn,bool enable){
 if(at[0]!=0xe8)return false;
 BYTE* target=at+5+*(LONG*)(at+1);
 if(target!=original && target!=(BYTE*)fn)return false;
 BYTE* desired=enable?(BYTE*)fn:original;if(target==desired)return true;
 BYTE patch[5]={0xe8,0,0,0,0};*(LONG*)(patch+1)=(LONG)(desired-(at+5));return WriteCode(at,patch,sizeof patch);
}
// Secondary CAiBase interface at +0x714; slots and native functions recovered from this PE's RTTI.
struct HvAiSlot {DWORD slot,native;MmTick_t previous;};
static HvAiSlot hvAiSlots[]={
 {0xe7818,0x571f0,NULL},{0xe78cc,0x57900,NULL},{0xe79b8,0x58280,NULL},
 {0xe7ac8,0x59750,NULL},{0xe7b98,0x5a5b0,NULL},{0xe7c50,0x5ae30,NULL},
 {0xe7d4c,0x5bc40,NULL},{0xe7df0,0x5d2b0,NULL},{0xe7ef0,0x5dde0,NULL},
 {0xe7f8c,0x5ef00,NULL},{0xe807c,0x5f5c0,NULL},{0xe80c8,0x60290,NULL},
 {0xe8158,0x60b90,NULL},{0xe81c8,0x61aa0,NULL},{0xe867c,0x656c0,NULL}
};
static void __fastcall HvAiTick(BYTE* secondary,void*,DWORD dt,DWORD arg){
 MmTick_t previous=NULL;
 for(unsigned i=0;i<_countof(hvAiSlots);++i)if(*(void**)secondary==hvModule+hvAiSlots[i].slot){previous=hvAiSlots[i].previous;break;}
 if(!previous)return;
 BYTE* ai=secondary-0x714;BYTE* a=*(BYTE**)(ai+4);
 bool scoped=hvEnabled && duelBalance && duelDifficulty==2 && balScene>=0 && a && *(int*)(a+0x724)==1;
 if(scoped){
  const char* def=HvDefinition(a);char path[512];scoped=def && strcpy_s(path,def)==0;
  if(scoped){_strlwr_s(path);for(char* p=path;*p;++p)if(*p=='\\')*p='/';scoped=strstr(path,"data/hv11/r")!=NULL;}
 }
 if(!scoped){previous(secondary,dt,arg);return;}
 if(hvOwnerActor!=a){hvOwnerActor=a;hvOwnerActive=false;hvOldLogged=false;}
 const int hp=*(int*)(a+0x558),maximum=*(int*)(a+0x560);
 if(!hvOwnerActive && hp>0 && maximum>0 && hp*100<=maximum*40){
  hvOwnerActive=true;ZeroMemory(&hvState,sizeof hvState);hvSupport=NULL;
  Log("HV11_HANDOFF t=%lu actor=%p hp=%d max=%d percent=%.2f old_ai_disabled=1 motion=%s",GetTickCount(),a,hp,maximum,100.0*hp/maximum,HvName(a));
 }
 if(!hvOwnerActive){
  if(!hvOldLogged){Log("HV11_OLD_AI t=%lu hp=%d max=%d side=1",GetTickCount(),hp,maximum);hvOldLogged=true;}
  previous(secondary,dt,arg);return;
 }
 // Shared native sensing/timers only. Legacy tactical tick is NOT called after handoff.
 // DEF handles all animation, attack execution, damage/grab/KO and natural completion.
 typedef void(__thiscall* SenseFn)(BYTE*,DWORD);
 ((SenseFn)(hvModule+0x62580))(ai,dt);
 *(int*)(ai+0x2c)=0;
 HvThink(a);
}
static void RestoreHardV11(HMODULE module){
 if(hvModule!=(BYTE*)module)return;
 hvEnabled=false;
 for(unsigned i=0;i<_countof(hvAiSlots);++i){HvAiSlot& s=hvAiSlots[i];void** slot=(void**)(hvModule+s.slot);
  if(*slot==(void*)HvAiTick && s.previous)MmSwapSlot(slot,(void*)HvAiTick,(void*)s.previous);
 }
}
static bool InstallHardAi(){
 for(unsigned i=0;i<_countof(hvAiSlots);++i){HvAiSlot& s=hvAiSlots[i];void* old=*(void**)(hvModule+s.slot);
  bool known=old==hvModule+s.native || old==(void*)AmTick || old==(void*)A2Tick || old==(void*)HmTick || old==(void*)EmOwnTick || old==(void*)F2Tick || old==(void*)MmTickHook || old==(void*)KrTickObserve || old==(void*)NcTick ||
             old==(void*)QmTickHook;   // 11-08: MM-QM C12's Nero tick (calls the native tick) - hard's Nero chains over it
  if(!known){Log("HV11 AI slot mismatch rva=%lx ptr=%p",s.slot,old);RestoreHardV11((HMODULE)hvModule);return false;}
  s.previous=(MmTick_t)old;
  if(!MmSwapSlot((void**)(hvModule+s.slot),old,(void*)HvAiTick)){RestoreHardV11((HMODULE)hvModule);return false;}
 }
 return true;
}
static void ApplyHardV11(BYTE* obj,HMODULE bm,LONG input){
 hvModule=(BYTE*)bm;hvEnabled=false;hvObserve=false;ZeroMemory(&hvState,sizeof hvState);ZeroMemory(hvMpLogs,sizeof hvMpLogs);hvHitLogs=0;hvOwnerActor=NULL;hvOwnerActive=false;hvOldLogged=false;hvSupport=NULL;
 wchar_t ini[MAX_PATH],pack[MAX_PATH];swprintf_s(ini,L"%s\\autobattle.ini",abDir);swprintf_s(pack,L"%s\\dmpack522_hard_v11.dat",gameDir);
 bool trace=GetPrivateProfileIntW(L"autobattle",L"hard_v11_trace",0,ini)!=0;
 LONG loaded=*(LONG*)(obj+0x18);int dest=-1;
 if(duelBalance && duelDifficulty==2 && balScene>=0 && balScene<1000 && GetFileAttributesW(pack)!=INVALID_FILE_ATTRIBUTES)
  for(unsigned i=0;i<_countof(hvRoutes);++i)if(hvRoutes[i].source==loaded){dest=hvRoutes[i].dest;break;}
 bool hooks=HvCall(hvModule+0x393a8,hvModule+0x333a0,(void*)HvUpdate,dest>=0||trace);
 bool inputHook=dest<0 || InstallHardAi();
 bool observer=HvCall(hvModule+0x393f4,hvModule+0x35a90,(void*)HvParams,trace);
 if(!hooks || !inputHook || !observer){
  HvCall(hvModule+0x393a8,hvModule+0x333a0,(void*)HvUpdate,false);HvCall(hvModule+0x393f4,hvModule+0x35a90,(void*)HvParams,false);
  RestoreHardV11(bm);
  Log("HV11 hook identity mismatch; baseline kept");return;
 }
 hvObserve=trace;hvEnabled=dest>=0;hvInput=input;hvResolved=loaded;hvDestination=dest;
 if(hvEnabled)*(LONG*)(obj+0x18)=dest;
 Log("HV11_SCOPE enabled=%d balance=%ld tier=%ld auto=%ld input=%ld resolved=%ld destination=%d mp=10 special=3 handoff_hp=40 trace=%d",(int)hvEnabled,duelBalance,duelDifficulty,duelAutoFight,input,loaded,dest,(int)trace);
}
