// Korean native pause menu and explicit result requests, 2026-11-02.
#pragma once
static BYTE* pmModule;
static bool pmTrace;
static wchar_t pmCapture[MAX_PATH];
static int pmCaptureFrames;
typedef bool(__thiscall* PmOpenFn)(BYTE*,int);
typedef void(__thiscall* PmTickFn)(BYTE*,float);
typedef void(__thiscall* PmSliceFn)(void*,int,int,int,int,void**);
typedef void(__thiscall* PmAddFn)(BYTE*,const int*);
typedef void(__thiscall* PmMouseFn)(BYTE*,int,int);
static HWND pmWindow;
static bool HoPauseTick(BYTE* menu);   // duel_handover.h
__declspec(noinline) static int FightAutoState(int* shown); // duel_handover.h: -1 this fight cannot switch, else 1 the AI plays it now
__declspec(noinline) static void ToggleFightAuto();
// 11-03 the 자동 전투 row: its plates (켜기, 끄기 x normal/hover/pressed) are in dmpack525_auto_pause's atlas at x 512,
// y 600..839; the menu keeps thirty plate views (+0x158), all taken, so the row borrows three this fight never shows
static BYTE* pmAutoMenu;
static void* pmAutoViews[6];
static bool pmAutoPack;
static int pmOpenTicks;
static bool PmAutoViews(BYTE* menu)
{
 void* atlas=*(void**)(menu+0x1ec);if(!atlas || !pmAutoPack)return false;
 if(pmAutoMenu!=menu){ZeroMemory(pmAutoViews,sizeof pmAutoViews);pmAutoMenu=menu;}
 for(int j=0;j<6;++j)if(!pmAutoViews[j])((PmSliceFn)(pmModule+0x17930))(atlas,512,600+j*40,280,40,&pmAutoViews[j]);
 for(int j=0;j<6;++j)if(!pmAutoViews[j])return false;
 return true;
}
static BOOL CALLBACK PmFindWindow(HWND w,LPARAM)
{
 DWORD pid=0;GetWindowThreadProcessId(w,&pid);wchar_t cls[64];
 if(pid==GetCurrentProcessId() && GetClassNameW(w,cls,64) && !wcscmp(cls,L"OCGameApplicationWindow")){
  pmWindow=w;return FALSE;
 }
 return TRUE;
}
static void __fastcall PmMousePosition(BYTE* menu,void*,int x,int y)
{
 // The native battle WndProc bypasses khd4k's story-menu input mapper.
 // Reuse its published picture rectangle, including letterbox offsets and exact integer scale.
 if(!IsWindow(pmWindow)){pmWindow=NULL;EnumWindows(PmFindWindow,0);}
 DWORD size=(DWORD)(UINT_PTR)GetPropA(pmWindow,"khd4k.size");
 DWORD pos=(DWORD)(UINT_PTR)GetPropA(pmWindow,"khd4k.pos");
 const int width=LOWORD(size),height=HIWORD(size),ox=(short)LOWORD(pos),oy=(short)HIWORD(pos);
 const int rawX=x,rawY=y;
 if(width>0 && height>0){x=(x-ox)*800/width;y=(y-oy)*600/height;}
 ((PmMouseFn)(pmModule+0x2d850))(menu,x,y);
 if(pmTrace && *(int*)(menu+0x144)==1)
  Log("PAUSE_MOUSE raw=%d,%d mapped=%d,%d picture=%d,%d,%d,%d",rawX,rawY,x,y,ox,oy,width,height);
}
static bool PmResetCamera()
{
 HMODULE camera=GetModuleHandleW(L"d3dxof.dll");
 typedef BOOL(WINAPI* ResetFn)();
 ResetFn reset=camera?(ResetFn)GetProcAddress(camera,"KhdCamResetView"):NULL;
 return reset && reset()!=FALSE;
}
static bool PmViews(BYTE* menu)
{
 void* atlas=*(void**)(menu+0x1ec);if(!atlas)return false;
 void** views=(void**)(menu+0x158);
 for(int i=15;i<30;++i)if(!views[i])
  ((PmSliceFn)(pmModule+0x17930))(atlas,512,(i-15)*40,280,40,&views[i]);
 for(int i=15;i<30;++i)if(!views[i])return false;
 return true;
}
static bool __fastcall PmOpen(BYTE* menu,void*,int state)
{
 bool result=((PmOpenFn)(pmModule+0x2e4c0))(menu,state);
 if(!result || state!=1 || !PmViews(menu))return result;
 int* lo=*(int**)(menu+0x1e0);int* hi=*(int**)(menu+0x1e4);
 if(!lo || hi-lo!=30)return result;
 const LONG scene=*(LONG*)(pmModule+0x11e494);
 bool duel=IsDuelScene(scene);
 // Row layout: enabled, selected-state, normal/hover/pressed textures, action, top/bottom/left/right.
 for(int i=0;i<3;++i){lo[i*10+6]=156+i*48;lo[i*10+7]=195+i*48;}
 const int nativeFirst=lo[2];          // the native first row's plates, unseen in DUEL (replaced just below)
 if(duel){lo[2]=24;lo[3]=25;lo[4]=26;}
 if(duel && duelMenuExit){lo[22]=21;lo[23]=22;lo[24]=23;lo[25]=0;}
 HMODULE camera=GetModuleHandleW(L"d3dxof.dll");
 const int cameraReady=camera && GetProcAddress(camera,"KhdCamResetView") ? 1 : 0;
 const int actions[3]={6,4,5},textures[3]={27,15,18};
 for(int i=0;i<3;++i){
  const int v=textures[i],y=300+i*48;
  const int row[10]={i==0?cameraReady:1,0,v,v+1,v+2,actions[i],y,y+39,260,539};
  ((PmAddFn)(pmModule+0x2e430))(menu+0x1dc,row);
 }
 int shown=0;
 const int autoState=FightAutoState(&shown);
 const bool autoRow=nativeFirst>=0 && nativeFirst<=27 && PmAutoViews(menu);
 if(autoRow){                         // the seventh row, under 즉시 패배: 자동 전투 켜기 / 끄기 for this fight's mode
  void** views=(void**)(menu+0x158);
  const int s=duel?nativeFirst:24;    // borrowed: DUEL the native first row's, elsewhere 대전 선택 (DUEL only)
  for(int k=0;k<3;++k)views[s+k]=pmAutoViews[(shown?3:0)+k];
  const int y=444,row[10]={autoState>=0?1:0,0,s,s+1,s+2,7,y,y+39,260,539};
  ((PmAddFn)(pmModule+0x2e430))(menu+0x1dc,row);
 }
 pmCaptureFrames=0;pmOpenTicks=0;
 Log("PAUSE_MENU open scene=%ld duel=%d rows=%d locale=ko auto_row=%d(%s)",scene,duel,autoRow?7:6,autoState,shown?"끄기":"켜기");
 return result;
}
static void __fastcall PmTick(BYTE* menu,void*,float dt)
{
 if(HoPauseTick(menu))((PmOpenFn)(pmModule+0x2e4c0))(menu,0);
 int before=*(int*)(menu+0x144);
 int action=*(int*)(menu+0x214);
 if(pmCapture[0] && before==1 && ++pmCaptureFrames==60)DhnSaveTarget(pmCapture);
 if(before==3 && *(int*)(menu+0x218)>=29 && action==7){
  ToggleFightAuto();                  // 11-03 자동 전투 row: this fight's mode, at once
  ((PmOpenFn)(pmModule+0x2e4c0))(menu,0);
 }
 if(before==3 && *(int*)(menu+0x218)>=29 && action==6){
  bool reset=PmResetCamera();
  Log("PAUSE_CAMERA_RESET accepted=%d scene=%ld",reset,*(LONG*)(pmModule+0x11e494));
  ((PmOpenFn)(pmModule+0x2e4c0))(menu,0);
 }
 if(*(int*)(menu+0x144)==3 && *(int*)(menu+0x218)>=29 && (action==4||action==5)){
  BYTE* manager=*(BYTE**)(pmModule+0x11e3b0);
  typedef BYTE*(__thiscall* ActorFn)(BYTE*,int);
  BYTE* actors[2]={NULL,NULL};
  if(manager)for(int i=0;i<2;++i)actors[i]=((ActorFn)(pmModule+0x39dc0))(manager,i);
  const int side=action==4?1:0;
  if(actors[side] && *(int*)(actors[side]+0x558)>0){
   int old=*(int*)(actors[side]+0x558);
   ((HvSetFn)(pmModule+0x30e50))(actors[side],100,0);
   Log("PAUSE_RESULT action=%s side=%d hp=%d->%d scene=%ld native_ko=1",action==4?"win":"lose",side,old,*(int*)(actors[side]+0x558),*(LONG*)(pmModule+0x11e494));
  }
  ((PmOpenFn)(pmModule+0x2e4c0))(menu,0);
 }
 // 11-03: for the first 30 ticks (half a second) after opening, 취소 (bit 4) counts as already down - in v12 the menu took a
 // 취소 at its first ticks and closed at once (07:59-08:5x tests, every time): the ESC that opened it, still held, and once
 // 13 ticks in (an automatic DUEL fight) - the reader also takes a pad button for 취소 (0x2d396: 0x14). The edge is
 // raw & ~(+0x1f8) (0x2d4e0).
 const int ticked=pmOpenTicks;
 if(before==1 && pmOpenTicks<30){++pmOpenTicks;*(int*)(menu+0x1f8)|=4;}
 ((PmTickFn)(pmModule+0x2e8e0))(menu,dt);
 if(pmTrace && before==1 && (ticked<30 || *(int*)(menu+0x1f4)))   // passive: what the tick saw (no extra reads - they move the menu's edges)
  Log("PAUSE_TICK n=%d state=1->%d raw=%x prev=%x edge=%x row=%d f220=%d f154=%d",ticked+1,*(int*)(menu+0x144),*(int*)(menu+0x1f4),
      *(int*)(menu+0x1f8),*(int*)(menu+0x1fc),((int(__thiscall*)(BYTE*))(pmModule+0x2d230))(menu),*(int*)(menu+0x220),*(int*)(menu+0x154));
 if(pmTrace && (before!=*(int*)(menu+0x144) || action!=*(int*)(menu+0x214)))
  Log("PAUSE_TRACE t=%lu state=%d->%d action=%d->%d count=%d input=%x end=%d abort=%d",GetTickCount(),before,*(int*)(menu+0x144),action,*(int*)(menu+0x214),*(int*)(menu+0x218),*(int*)(menu+0x1fc),pmModule[0x26f30c],pmModule[0x270020]);
}
static void InstallPauseMenu(HMODULE module)
{
 if(!module)return;BYTE* m=(BYTE*)module;
 wchar_t ini[MAX_PATH];swprintf_s(ini,L"%s\\autobattle.ini",abDir);
 pmTrace=GetPrivateProfileIntW(L"autobattle",L"pause_trace",0,ini)!=0;
 GetPrivateProfileStringW(L"autobattle",L"pause_capture",L"",pmCapture,MAX_PATH,ini);
 if(!((pmCapture[0]==L'P'||pmCapture[0]==L'p') && pmCapture[1]==L':' && pmCapture[2]==L'\\'))pmCapture[0]=0;
 const DWORD at[]={0x297eb,0x2e94f,0x2e983,0x298e9,0x2ead8,0x1458,0x14d2,0x1508};
 const DWORD native[]={0x2e4c0,0x2e4c0,0x2e4c0,0x2e8e0,0x2e8e0,0x2d850,0x2d850,0x2d850};
 void* fn[]={(void*)PmOpen,(void*)PmOpen,(void*)PmOpen,(void*)PmTick,(void*)PmTick,(void*)PmMousePosition,(void*)PmMousePosition,(void*)PmMousePosition};
 bool ours[8]={};BYTE saved[8][5];
 const char oldPath[]="data/menu/esc_menu.dds",newPath[]="data/menu/esc_more.dds";
 if(memcmp(m+0xe5f40,oldPath,sizeof oldPath) && memcmp(m+0xe5f40,newPath,sizeof newPath)){
  Log("PAUSE_MENU disabled: atlas path differs");return;
 }
 wchar_t pack[MAX_PATH];swprintf_s(pack,L"%s\\dmpack524_pause.dat",gameDir);
 if(GetFileAttributesW(pack)==INVALID_FILE_ATTRIBUTES){Log("PAUSE_MENU disabled: private atlas pack missing");return;}
 swprintf_s(pack,L"%s\\dmpack525_auto_pause.dat",gameDir);
 pmAutoPack=GetFileAttributesW(pack)!=INVALID_FILE_ATTRIBUTES;   // without it the 자동 전투 row stays out
 for(int i=0;i<8;++i){BYTE* p=m+at[i];void* target=p+5+*(LONG*)(p+1);
  ours[i]=p[0]==0xe8 && target==fn[i];
  if(!ours[i] && (p[0]!=0xe8 || target!=m+native[i])){Log("PAUSE_MENU disabled: call mismatch +%lx",at[i]);return;}
  memcpy(saved[i],p,5);
 }
 pmModule=m;
 for(int i=0;i<8;++i)if(!ours[i]){
  BYTE call[5]={0xe8};*(LONG*)(call+1)=(LONG)((BYTE*)fn[i]-(m+at[i]+5));
  if(!WriteCode(m+at[i],call,5)){
   for(int j=0;j<i;++j)if(!ours[j])WriteCode(m+at[j],saved[j],5);
   Log("PAUSE_MENU disabled: call write failed");return;
  }
 }
 if(!WriteCode(m+0xe5f40,(const BYTE*)newPath,sizeof newPath)){
  for(int i=0;i<8;++i)if(!ours[i])WriteCode(m+at[i],saved[i],5);
  Log("PAUSE_MENU disabled: atlas path write failed");return;
 }
 Log("PAUSE_MENU installed: native navigation/render/KO retained, reset action and scaled native mouse input");
}
