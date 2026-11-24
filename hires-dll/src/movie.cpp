// khd4k.dll movies (uhd=1): the engine plays its movies with DirectShow - a filter graph whose video window is a child
// of the main window (system.dll+0x68990: put_Owner, then IVideoWindow::SetWindowPosition with a rectangle it centres
// in the client from GetClientRect). We answer GetClientRect with 800x600, so that rectangle is in 800x600 pixels and
// the movie would sit in the top-left corner of our big window; here it is mapped into the 4:3 image rectangle (the
// video renderer scales the video).
#include "common.h"
#include <unknwn.h>

extern RECT uhdImageRect;                  // display.cpp: where the 800x600 picture goes in the client

static const GUID kFilterGraph = { 0xe436ebb3, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID kVideoWindow = { 0x56a868b4, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };

typedef HRESULT(WINAPI* CoCreateInstance_t)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef HRESULT(STDMETHODCALLTYPE* SetWindowPosition_t)(void*, long, long, long, long);
static CoCreateInstance_t realCoCreateInstance;
static SetWindowPosition_t realSetWindowPosition;

static HRESULT STDMETHODCALLTYPE HookSetWindowPosition(void* self, long l, long t, long w, long h)
{
    long iw = uhdImageRect.right - uhdImageRect.left, ih = uhdImageRect.bottom - uhdImageRect.top;
    if (iw > 0 && ih > 0)
    {
        long nl = uhdImageRect.left + l * iw / 800, nt = uhdImageRect.top + t * ih / 600;
        long nr = uhdImageRect.left + (l + w) * iw / 800, nb = uhdImageRect.top + (t + h) * ih / 600;
        Log("movie window (%ld,%ld) %ldx%ld -> (%ld,%ld) %ldx%ld", l, t, w, h, nl, nt, nr - nl, nb - nt);
        l = nl;
        t = nt;
        w = nr - nl;
        h = nb - nt;
    }
    return realSetWindowPosition(self, l, t, w, h);
}

static HRESULT WINAPI HookCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID* out)
{
    HRESULT hr = realCoCreateInstance(clsid, outer, ctx, iid, out);
    if (SUCCEEDED(hr) && out && *out && IsEqualGUID(clsid, kFilterGraph))
    {
        IUnknown* graph = (IUnknown*)*out;
        void* vw = NULL;
        if (SUCCEEDED(graph->QueryInterface(kVideoWindow, &vw)) && vw)
        {
            void** vt = *(void***)vw;                            // IVideoWindow: SetWindowPosition is method 39
            if (Patch(&vt[39], (void*)HookSetWindowPosition, (void**)&realSetWindowPosition))
                Log("movie: IVideoWindow::SetWindowPosition hooked");
            ((IUnknown*)vw)->Release();
        }
        else Log("movie: filter graph without IVideoWindow");
    }
    return hr;
}

void InstallMovie()
{
    HookImport("ole32.dll", "CoCreateInstance", (void*)HookCoCreateInstance, (void**)&realCoCreateInstance);
    Log("movies: the video window goes into the 4:3 image rectangle");
}
