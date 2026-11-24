#pragma once
// Optional read-only render-target capture, disabled unless the isolated test INI supplies an absolute P: path.
static wchar_t dhnCapturePath[MAX_PATH];
static int dhnCaptureFrames;
static bool dhnCameraCaptureSeries;
struct DhnSurfaceDesc {DWORD format,type,usage,pool,size,multisample,width,height;};
struct DhnLocked {int pitch;void* bits;};
typedef HRESULT(__stdcall* DhnGetTarget_t)(void*,void**);
typedef HRESULT(__stdcall* DhnCreateImage_t)(void*,UINT,UINT,int,void**);
typedef HRESULT(__stdcall* DhnCopy_t)(void*,void*,const RECT*,UINT,void*,const POINT*);
typedef HRESULT(__stdcall* DhnGetDesc_t)(void*,DhnSurfaceDesc*);
typedef HRESULT(__stdcall* DhnLock_t)(void*,DhnLocked*,const RECT*,DWORD);
typedef HRESULT(__stdcall* DhnUnlock_t)(void*);
typedef ULONG(__stdcall* DhnRelease_t)(void*);
static void DhnSaveTarget(const wchar_t* capturePath)
{
 if(!dhnModule || !capturePath[0])return;
 void* device=*(void**)(dhnModule+0x11e5a8);if(!device)return;
 void** vt=*(void***)device;void *surface=NULL,*image=NULL;DhnSurfaceDesc d={};
 HRESULT hr=((DhnGetTarget_t)vt[32])(device,&surface);
 if(SUCCEEDED(hr) && surface){
  hr=((DhnGetDesc_t)(*(void***)surface)[8])(surface,&d);
  if(SUCCEEDED(hr) && (d.format==21 || d.format==22) && d.width<=8192 && d.height<=8192){
   hr=((DhnCreateImage_t)vt[27])(device,d.width,d.height,(int)d.format,&image);
   if(SUCCEEDED(hr) && image)hr=((DhnCopy_t)vt[28])(device,surface,NULL,0,image,NULL);
   if(SUCCEEDED(hr) && image){
    DhnLocked lock={};hr=((DhnLock_t)(*(void***)image)[9])(image,&lock,NULL,0x10);
    if(SUCCEEDED(hr)){
     BITMAPFILEHEADER f={};BITMAPINFOHEADER b={};f.bfType=0x4d42;
     f.bfOffBits=sizeof(f)+sizeof(b);f.bfSize=f.bfOffBits+d.width*d.height*4;
     b.biSize=sizeof b;b.biWidth=d.width;b.biHeight=-(LONG)d.height;b.biPlanes=1;b.biBitCount=32;b.biSizeImage=d.width*d.height*4;
     FILE* out=NULL;
     if(!_wfopen_s(&out,capturePath,L"wb") && out){
      fwrite(&f,sizeof f,1,out);fwrite(&b,sizeof b,1,out);
      for(DWORD y=0;y<d.height;++y)fwrite((BYTE*)lock.bits+y*lock.pitch,d.width*4,1,out);
      fclose(out);Log("DUEL_HUD_CAPTURE saved %lux%lu render target",d.width,d.height);
     }
     ((DhnUnlock_t)(*(void***)image)[10])(image);
    }
   }
  }
 }
 if(image)((DhnRelease_t)(*(void***)image)[2])(image);
 if(surface)((DhnRelease_t)(*(void***)surface)[2])(surface);
 if(FAILED(hr))Log("DUEL_HUD_CAPTURE unavailable hr=%08lx format=%lu",hr,d.format);
}

static void DhnCapture()
{
 if(!dhnCapturePath[0] || !dhnModule)return;
 if(dhnCameraCaptureSeries && (!ncReady || !NcEligible(ncActor)))return;
 ++dhnCaptureFrames;
 const bool wanted=dhnCameraCaptureSeries ? (dhnCaptureFrames==60 || dhnCaptureFrames==600 || dhnCaptureFrames==1200 || dhnCaptureFrames==1800 || dhnCaptureFrames==2400 || dhnCaptureFrames==3600) : dhnCaptureFrames==60;
 if(!wanted)return;
 wchar_t capturePath[MAX_PATH];
 if(dhnCameraCaptureSeries)swprintf_s(capturePath,L"%s.%04d.bmp",dhnCapturePath,dhnCaptureFrames);
 else wcscpy_s(capturePath,dhnCapturePath);
 DhnSaveTarget(capturePath);
}
