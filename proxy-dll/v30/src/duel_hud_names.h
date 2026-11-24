#pragma once
#include "duel_hud_name_data.h"
// The two narrow HUD consumers are wrapped. Combat identity, AI, stats, and assets remain immutable.
struct DhnView {int w,h,x,y;float u0,v0,u1,v1;void* texture;void* next;};
struct DhnArgs {DhnView* view;int x,y;float sx,sy;DWORD color;int a,b,c;};
static_assert(sizeof(DhnView)==40 && sizeof(DhnArgs)==36,"Native x86 HUD layout");
typedef void(__thiscall* DhnDraw_t)(void*,DhnView*,int,int,float,float,DWORD,int,int,int);
static bool dhnDuel;
static BYTE* dhnModule;
static DWORD dhnLogged;
#include "duel_hud_capture.h"
static DhnView dhnViews[17][12];
static void DhnRect(DhnView& v,void* texture,int x,int y,int w,int h)
{
 v.w=w;v.h=h;v.x=x;v.y=y;v.u0=float(x)/512.0f;v.v0=float(y)/512.0f;
 v.u1=float(x+w)/512.0f;v.v1=float(y+h)/512.0f;v.texture=texture;v.next=NULL;
}
static int __fastcall DhnRead(BYTE* actor,void*)
{
 int original=*(int*)(actor+0xb70);
 __try {return DhnIndex(dhnDuel,*(int*)(actor+0x724),NcDefinition(actor),original);}
 __except(EXCEPTION_EXECUTE_HANDLER){return original;}
}
static void __stdcall DhnDraw(void* renderer,BYTE* hud,const DhnArgs* a)
{
 DhnDraw_t draw=(DhnDraw_t)(*(void***)renderer)[0x94/4];
 int index=-1;BYTE* actor=NULL;
 __try {
  actor=*(BYTE**)(hud+0x1dc);
  if(actor && dhnDuel)index=DhnIndex(true,*(int*)(actor+0x724),NcDefinition(actor),-1);
 } __except(EXCEPTION_EXECUTE_HANDLER){index=-1;}
 if(index<0 || index>16 || !a->view || !a->view->texture){
  draw(renderer,a->view,a->x,a->y,a->sx,a->sy,a->color,a->a,a->b,a->c);return;
 }
 DhnView* v=dhnViews[index];
 // Native atlas stays untouched: use existing complete labels or a blank plate plus native glyph quads.
 const char* text=DhnText(index);
 DhnRect(v[0],a->view->texture,310,1+14*(text?14:index),192,13);
 draw(renderer,&v[0],a->x,a->y,a->sx,a->sy,a->color,a->a,a->b,a->c);
 if(text){
  int offset=(192-DhnTextWidth(text))/2;
  for(int k=0;text[k] && k<11;++k){
   const DhnGlyph* g=DhnGlyphFor(text[k]);if(!g)break;
   DhnRect(v[k+1],a->view->texture,g->x,g->y,g->w,13);
   draw(renderer,&v[k+1],a->x+int(float(offset)*a->sx),a->y,a->sx,a->sy,a->color,a->a,a->b,a->c);
   offset+=g->w+1;
  }
 }
 __try {DhnCapture();} __except(EXCEPTION_EXECUTE_HANDLER){dhnCapturePath[0]=0;}
 if(!(dhnLogged&(1u<<index))){
  dhnLogged|=1u<<index;
  Log("DUEL_HUD_NAME group=1 original=%d display=%d label=%s def=%s view=%dx%d texture=%p native_ai_unchanged=1",
   *(int*)(actor+0xb70),index,text?text:"original-atlas",NcDefinition(actor),a->view->w,a->view->h,a->view->texture);
 }
}
static void __declspec(naked) DhnReadBridge()
{
 __asm {
  push ecx
  push edx
  mov ecx,eax
  call DhnRead
  pop edx
  pop ecx
  ret
 }
}
static void __declspec(naked) DhnDrawBridge()
{
 __asm {
  pushad
  lea eax,[esp+36]
  push eax
  push esi
  push ecx
  call DhnDraw
  popad
  ret 36
 }
}
static void InstallDuelHudNames(HMODULE module,bool duel)
{
 dhnDuel=false;dhnLogged=0;dhnCaptureFrames=0;
 wchar_t ini[MAX_PATH];swprintf_s(ini,L"%s\\autobattle.ini",abDir);
 GetPrivateProfileStringW(L"autobattle",L"hud_capture",L"",dhnCapturePath,MAX_PATH,ini);
 dhnCameraCaptureSeries=GetPrivateProfileIntW(L"autobattle",L"camera_capture_series",0,ini)==1;
 if(!((dhnCapturePath[0]==L'P' || dhnCapturePath[0]==L'p') && dhnCapturePath[1]==L':' && dhnCapturePath[2]==L'\\'))dhnCapturePath[0]=0;
 if(!module)return;
 BYTE* m=(BYTE*)module;
 const DWORD rva[]={0x2bbb5,0x2bc0e};
 const BYTE expected[2][6]={{0x8b,0x80,0x70,0x0b,0,0},{0xff,0x95,0x94,0,0,0}};
 void* fn[]={(void*)DhnReadBridge,(void*)DhnDrawBridge};
 bool ours[2]={};BYTE saved[2][6];
 for(int k=0;k<2;++k){
  BYTE* at=m+rva[k];ours[k]=at[0]==0xe8 && at+5+*(LONG*)(at+1)==fn[k] && at[5]==0x90;
  if(!ours[k] && memcmp(at,expected[k],6)){Log("DUEL_HUD_NAME disabled: native consumer mismatch +%lx",rva[k]);return;}
  memcpy(saved[k],at,6);
 }
 for(int k=0;k<2;++k){
  if(ours[k])continue;
  BYTE call[6]={0xe8,0,0,0,0,0x90};*(LONG*)(call+1)=(LONG)((BYTE*)fn[k]-(m+rva[k]+5));
  if(!WriteCode(m+rva[k],call,6)){
   for(int j=0;j<k;++j)if(!ours[j])WriteCode(m+rva[j],saved[j],6);
   Log("DUEL_HUD_NAME disabled: consumer write failed");return;
  }
 }
 dhnModule=m;dhnDuel=duel;
 Log("DUEL_HUD_NAME installed: duel=%d; 3 new native-glyph labels, identity independent from AI; actor/atlas writes=0",duel);
}
