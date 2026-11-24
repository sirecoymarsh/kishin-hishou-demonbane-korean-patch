// khd4k.dll trace: how system.dll draws (uhd=2 or uhd_trace=1). Hooks system.dll's imports (DirectDraw creation, GDI
// text/DIB/blit calls, registry reads, command messages) and the DirectDraw surface methods; writes the caller
// (system.dll RVA) of each call, the first few calls of every (function, caller) in full and then only counts.
#include "common.h"
#include <ddraw.h>

// ---------------------------------------------------------------- surfaces we have seen
struct Surf { void* p; DWORD w, h, caps; int id; };
static Surf surfs[256];
static LONG nSurfs;

static int SurfId(void* p)
{
    for (LONG i = 0; i < nSurfs && i < 256; i++) if (surfs[i].p == p) return surfs[i].id;
    return -1;
}

static void RectStr(const RECT* r, char* out, size_t n)
{
    if (r) _snprintf_s(out, n, _TRUNCATE, "(%ld,%ld)-(%ld,%ld)", r->left, r->top, r->right, r->bottom);
    else _snprintf_s(out, n, _TRUNCATE, "(null)");
}

// IDirectDrawSurface7 methods (vtable index): Blt 5, BltFast 7, Flip 11, GetDC 17, Lock 25, ReleaseDC 26, Unlock 32
typedef HRESULT(STDMETHODCALLTYPE* Blt_t)(void*, LPRECT, void*, LPRECT, DWORD, LPDDBLTFX);
typedef HRESULT(STDMETHODCALLTYPE* BltFast_t)(void*, DWORD, DWORD, void*, LPRECT, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* Flip_t)(void*, void*, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* Lock_t)(void*, LPRECT, LPDDSURFACEDESC2, DWORD, HANDLE);
static Blt_t realBlt;
static BltFast_t realBltFast;
static Flip_t realFlip;
static Lock_t realLock;

static HRESULT STDMETHODCALLTYPE HookBlt(void* self, LPRECT dst, void* src, LPRECT sr, DWORD flags, LPDDBLTFX fx)
{
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("Surface::Blt", rva))
    {
        char a[64], b[64];
        RectStr(dst, a, sizeof a);
        RectStr(sr, b, sizeof b);
        Log("Blt caller %08lx dst S%d %s <- src S%d %s flags %08lx", rva, SurfId(self), a, SurfId(src), b, flags);
    }
    return realBlt(self, dst, src, sr, flags, fx);
}

static HRESULT STDMETHODCALLTYPE HookBltFast(void* self, DWORD x, DWORD y, void* src, LPRECT sr, DWORD flags)
{
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("Surface::BltFast", rva)) Log("BltFast caller %08lx dst S%d at (%lu,%lu) <- src S%d", rva, SurfId(self), x, y, SurfId(src));
    return realBltFast(self, x, y, src, sr, flags);
}

static HRESULT STDMETHODCALLTYPE HookFlip(void* self, void* over, DWORD flags)
{
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("Surface::Flip", rva)) Log("Flip caller %08lx S%d flags %08lx", rva, SurfId(self), flags);
    return realFlip(self, over, flags);
}

static HRESULT STDMETHODCALLTYPE HookLock(void* self, LPRECT r, LPDDSURFACEDESC2 d, DWORD flags, HANDLE ev)
{
    DWORD rva = Rva(_ReturnAddress());
    HRESULT hr = realLock(self, r, d, flags, ev);
    if (Budget("Surface::Lock", rva)) Log("Lock caller %08lx S%d -> hr %08lx", rva, SurfId(self), hr);
    return hr;
}

static void HookSurfaceVtable(void* surf)
{
    void** vt = *(void***)surf;
    static volatile LONG done;
    if (InterlockedExchange(&done, 1)) return;
    Patch(&vt[5], (void*)HookBlt, (void**)&realBlt);
    Patch(&vt[7], (void*)HookBltFast, (void**)&realBltFast);
    Patch(&vt[11], (void*)HookFlip, (void**)&realFlip);
    Patch(&vt[25], (void*)HookLock, (void**)&realLock);
}

// IDirectDraw7: CreateSurface 6, SetCooperativeLevel 20, SetDisplayMode 21
typedef HRESULT(STDMETHODCALLTYPE* CreateSurface_t)(void*, LPDDSURFACEDESC2, void**, IUnknown*);
typedef HRESULT(STDMETHODCALLTYPE* SetCoop_t)(void*, HWND, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* SetDisplayMode_t)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
static CreateSurface_t realCreateSurface;
static SetCoop_t realSetCoop;
static SetDisplayMode_t realSetDisplayMode;

static HRESULT STDMETHODCALLTYPE HookCreateSurface(void* self, LPDDSURFACEDESC2 d, void** out, IUnknown* outer)
{
    DWORD rva = Rva(_ReturnAddress());
    HRESULT hr = realCreateSurface(self, d, out, outer);
    void* s = (SUCCEEDED(hr) && out) ? *out : NULL;
    int id = -1;
    if (s)
    {
        LONG i = InterlockedIncrement(&nSurfs) - 1;
        if (i < 256) { surfs[i].p = s; surfs[i].w = d->dwWidth; surfs[i].h = d->dwHeight; surfs[i].caps = d->ddsCaps.dwCaps; surfs[i].id = (int)i; id = (int)i; }
        HookSurfaceVtable(s);
    }
    Log("CreateSurface caller %08lx flags %08lx %lux%lu caps %08lx -> hr %08lx S%d", rva, d->dwFlags, d->dwWidth, d->dwHeight, d->ddsCaps.dwCaps, hr, id);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetCoop(void* self, HWND w, DWORD flags)
{
    Log("SetCooperativeLevel caller %08lx hwnd %p flags %08lx", Rva(_ReturnAddress()), w, flags);
    return realSetCoop(self, w, flags);
}

static HRESULT STDMETHODCALLTYPE HookSetDisplayMode(void* self, DWORD w, DWORD h, DWORD bpp, DWORD rate, DWORD flags)
{
    Log("SetDisplayMode caller %08lx %lux%lu %lubpp", Rva(_ReturnAddress()), w, h, bpp);
    return realSetDisplayMode(self, w, h, bpp, rate, flags);
}

typedef HRESULT(WINAPI* DDCreateEx_t)(GUID*, LPVOID*, REFIID, IUnknown*);
static DDCreateEx_t realDDCreateEx;

static HRESULT WINAPI HookDDCreateEx(GUID* g, LPVOID* out, REFIID iid, IUnknown* outer)
{
    HRESULT hr = realDDCreateEx(g, out, iid, outer);
    Log("DirectDrawCreateEx caller %08lx -> hr %08lx", Rva(_ReturnAddress()), hr);
    if (SUCCEEDED(hr) && out && *out)
    {
        void** vt = *(void***)*out;
        Patch(&vt[6], (void*)HookCreateSurface, (void**)&realCreateSurface);
        Patch(&vt[20], (void*)HookSetCoop, (void**)&realSetCoop);
        Patch(&vt[21], (void*)HookSetDisplayMode, (void**)&realSetDisplayMode);
    }
    return hr;
}

// ---------------------------------------------------------------- GDI
typedef BOOL(WINAPI* TextOutA_t)(HDC, int, int, LPCSTR, int);
typedef BOOL(WINAPI* BitBlt_t)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef HBITMAP(WINAPI* CreateDIBSection_t)(HDC, const BITMAPINFO*, UINT, VOID**, HANDLE, DWORD);
typedef HFONT(WINAPI* CreateFontIndirectA_t)(const LOGFONTA*);
static TextOutA_t realTextOutA;
static BitBlt_t chainBitBlt;
static CreateDIBSection_t realCreateDIBSection;
static CreateFontIndirectA_t realCreateFontIndirectA;

static void DcInfo(HDC dc, char* out, size_t n)
{
    HGDIOBJ bmp = GetCurrentObject(dc, OBJ_BITMAP);
    BITMAP bm = {};
    HGDIOBJ font = GetCurrentObject(dc, OBJ_FONT);
    LOGFONTA lf = {};
    if (bmp) GetObjectA(bmp, sizeof bm, &bm);
    if (font) GetObjectA(font, sizeof lf, &lf);
    _snprintf_s(out, n, _TRUNCATE, "bmp %ldx%ld/%u font h%ld wt%ld '%s' win %p", bm.bmWidth, bm.bmHeight, bm.bmBitsPixel,
                lf.lfHeight, lf.lfWeight, lf.lfFaceName, WindowFromDC(dc));
}

static BOOL WINAPI HookTextOutA(HDC dc, int x, int y, LPCSTR s, int len)
{
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("TextOutA", rva, 30))
    {
        wchar_t w[256] = {};
        char u[768] = {};
        int k = MultiByteToWideChar(932, 0, s, len < 255 ? len : 255, w, 255);
        if (k > 0) WideCharToMultiByte(CP_UTF8, 0, w, k, u, sizeof(u) - 1, NULL, NULL);
        char info[256];
        DcInfo(dc, info, sizeof info);
        Log("TextOutA caller %08lx at (%d,%d) len %d '%s' color %06lx | %s", rva, x, y, len, u, GetTextColor(dc), info);
    }
    return realTextOutA(dc, x, y, s, len);
}

static BOOL WINAPI HookBitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop)
{
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("BitBlt", rva))
    {
        char di[256], si[256];
        DcInfo(d, di, sizeof di);
        if (s) DcInfo(s, si, sizeof si); else si[0] = 0;
        Log("BitBlt caller %08lx dst (%d,%d %dx%d) [%s] <- src (%d,%d) [%s] rop %08lx", rva, x, y, w, h, di, sx, sy, si, rop);
    }
    return chainBitBlt(d, x, y, w, h, s, sx, sy, rop);
}

static HBITMAP WINAPI HookCreateDIBSection(HDC dc, const BITMAPINFO* bmi, UINT usage, VOID** bits, HANDLE sec, DWORD off)
{
    DWORD rva = Rva(_ReturnAddress());
    HBITMAP r = realCreateDIBSection(dc, bmi, usage, bits, sec, off);
    if (Budget("CreateDIBSection", rva, 40))
        Log("CreateDIBSection caller %08lx %ldx%ld %ubpp -> %p", rva, bmi->bmiHeader.biWidth, bmi->bmiHeader.biHeight, bmi->bmiHeader.biBitCount, r);
    return r;
}

static HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA* lf)
{
    DWORD rva = Rva(_ReturnAddress());
    HFONT r = realCreateFontIndirectA(lf);
    if (Budget("CreateFontIndirectA", rva, 40))
        Log("CreateFontIndirectA caller %08lx h%ld wt%ld q%u '%s'", rva, lf->lfHeight, lf->lfWeight, lf->lfQuality, lf->lfFaceName);
    return r;
}

// ---------------------------------------------------------------- registry, commands, work area
typedef LONG(NTAPI* NtQueryKey_t)(HANDLE, int, PVOID, ULONG, PULONG);
typedef LSTATUS(WINAPI* RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LSTATUS(WINAPI* RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef BOOL(WINAPI* PostMessageA_t)(HWND, UINT, WPARAM, LPARAM);
typedef int(WINAPI* TranslateAcceleratorA_t)(HWND, HACCEL, LPMSG);
typedef BOOL(WINAPI* SystemParametersInfoA_t)(UINT, UINT, PVOID, UINT);
static RegOpenKeyExA_t chainRegOpenKeyExA;
static RegQueryValueExA_t realRegQueryValueExA;
static PostMessageA_t realPostMessageA;
static TranslateAcceleratorA_t realTranslateAcceleratorA;
static SystemParametersInfoA_t realSystemParametersInfoA;

static void KeyName(HKEY k, char* out, size_t n)
{
    static NtQueryKey_t q = (NtQueryKey_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryKey");
    BYTE buf[1024];
    ULONG got = 0;
    out[0] = 0;
    if (q && q(k, 3, buf, sizeof buf - 2, &got) >= 0)        // KeyNameInformation: ULONG length (bytes) + WCHAR name
    {
        ULONG len = *(ULONG*)buf / 2;
        wchar_t* w = (wchar_t*)(buf + 4);
        w[len] = 0;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
    }
}

static LSTATUS WINAPI HookRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out)
{
    LSTATUS r = chainRegOpenKeyExA(root, sub, opt, sam, out);
    char name[600];
    if (r == ERROR_SUCCESS && out) KeyName(*out, name, sizeof name); else name[0] = 0;
    Log("RegOpenKeyExA caller %08lx '%s' -> %ld real key '%s'", Rva(_ReturnAddress()), sub ? sub : "", r, name);
    return r;
}

static LSTATUS WINAPI HookRegQueryValueExA(HKEY k, LPCSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD size)
{
    LSTATUS r = realRegQueryValueExA(k, name, res, type, data, size);
    DWORD rva = Rva(_ReturnAddress());
    if (Budget("RegQueryValueExA", rva, 60))
        Log("RegQueryValueExA caller %08lx '%s' -> %ld value %ld", rva, name ? name : "", r,
            (r == ERROR_SUCCESS && data && size && *size == 4) ? *(LONG*)data : -1);
    return r;
}

static BOOL WINAPI HookPostMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMAND || m == WM_SYSCOMMAND) Log("PostMessageA caller %08lx msg %04x wParam %08lx", Rva(_ReturnAddress()), m, (DWORD)wp);
    return realPostMessageA(w, m, wp, lp);
}

static int WINAPI HookTranslateAcceleratorA(HWND w, HACCEL a, LPMSG msg)
{
    int r = realTranslateAcceleratorA(w, a, msg);
    if (r && msg) Log("accelerator used: msg %04x wParam %08lx", msg->message, (DWORD)msg->wParam);
    return r;
}

static BOOL WINAPI HookSystemParametersInfoA(UINT action, UINT ui, PVOID pv, UINT win)
{
    BOOL r = realSystemParametersInfoA(action, ui, pv, win);
    if (action == SPI_GETWORKAREA && pv)
    {
        RECT* rc = (RECT*)pv;
        Log("work area caller %08lx -> (%ld,%ld)-(%ld,%ld)", Rva(_ReturnAddress()), rc->left, rc->top, rc->right, rc->bottom);
    }
    return r;
}

void InstallTrace()
{
    HookImport("DDRAW.dll", "DirectDrawCreateEx", (void*)HookDDCreateEx, (void**)&realDDCreateEx);
    HookImport("GDI32.dll", "TextOutA", (void*)HookTextOutA, (void**)&realTextOutA);
    HookImport("GDI32.dll", "BitBlt", (void*)HookBitBlt, (void**)&chainBitBlt);        // the display's hook (if any) comes after
    HookImport("GDI32.dll", "CreateDIBSection", (void*)HookCreateDIBSection, (void**)&realCreateDIBSection);
    HookImport("GDI32.dll", "CreateFontIndirectA", (void*)HookCreateFontIndirectA, (void**)&realCreateFontIndirectA);
    HookImport("ADVAPI32.dll", "RegOpenKeyExA", (void*)HookRegOpenKeyExA, (void**)&chainRegOpenKeyExA);   // after the proxy's own
    HookImport("ADVAPI32.dll", "RegQueryValueExA", (void*)HookRegQueryValueExA, (void**)&realRegQueryValueExA);
    HookImport("USER32.dll", "PostMessageA", (void*)HookPostMessageA, (void**)&realPostMessageA);
    HookImport("USER32.dll", "TranslateAcceleratorA", (void*)HookTranslateAcceleratorA, (void**)&realTranslateAcceleratorA);
    HookImport("USER32.dll", "SystemParametersInfoA", (void*)HookSystemParametersInfoA, (void**)&realSystemParametersInfoA);
    Log("trace hooks installed");
}
