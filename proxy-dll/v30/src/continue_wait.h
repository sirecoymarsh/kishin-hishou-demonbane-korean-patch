#pragma once
// NPS waits added to pre-Continue dialogue are marked KEYSKIP=FALSE, SYNC=TRUE.
// Keep the native event loop and extend only these waits when a click ends them early.
typedef int(__stdcall* CwNativeFn)(BYTE*,int,int,int,int);
static CwNativeFn cwNative;
static bool cwTrace;
static int __stdcall CwWait(BYTE* app,int ms,int keyskip,int mode,int sync)
{
 if(keyskip!=0 || sync!=1 || ms<=0 || ms>120000)return cwNative(app,ms,keyskip,mode,sync);
 const BYTE skip=app[0x1894d];const DWORD start=GetTickCount(),duration=(DWORD)ms;
 int result=1,calls=0;
 if(cwTrace)Log("CONTINUE_WAIT start tick=%lu duration=%lu",start,duration);
 do {
  app[0x1894d]=0;
  LONG remaining=(LONG)(duration-(GetTickCount()-start));if(remaining<=0)break;
  result=cwNative(app,remaining,0,mode,sync);++calls;
  if(cwTrace)Log("CONTINUE_WAIT native_return elapsed=%lu result=%d",GetTickCount()-start,result);
  if(!result)break; // Keep native quit/load/script changes authoritative.
  if(GetTickCount()-start<duration)Sleep(1);
 } while(GetTickCount()-start<duration);
 app[0x1894d]=skip;
 if(cwTrace)Log("CONTINUE_WAIT end tick=%lu elapsed=%lu native_calls=%d result=%d",GetTickCount(),GetTickCount()-start,calls,result);
 return result;
}
static void InstallContinueWait(HMODULE module,const wchar_t* ini)
{
 BYTE* m=(BYTE*)module;BYTE* at=m+0x32f93;
 if(at[0]!=0xe8 || at+5+*(LONG*)(at+1)!=m+0x20760){Log("CONTINUE_WAIT disabled: native call differs");return;}
 cwTrace=GetPrivateProfileIntW(L"autobattle",L"continue_trace",0,ini)!=0;
 cwNative=(CwNativeFn)(m+0x20760);BYTE call[5]={0xe8};*(LONG*)(call+1)=(LONG)((BYTE*)CwWait-(at+5));
 if(WriteCode(at,call,5))Log("CONTINUE_WAIT installed: marked defeat waits only; native event loop retained");
}
