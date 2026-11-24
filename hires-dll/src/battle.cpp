// khd4k.dll battles (uhd=1): the battle module (demonbane.dll, Direct3D 8) is loaded by system.dll for each fight; it
// takes over the main window (its own window procedure, demonbane+0x12C0) and makes a windowed D3D8 device on it with a
// back buffer as big as the client - in our big window that stretches its 4:3 picture to 16:9 and leaves its 2D
// (loading screen, HUD), drawn in 800x600 pixels, in the top-left corner.
//  uhd_battle=1 (default, native): the back buffer gets the 4:3 image rectangle's size (2880x2160 on a 2160p monitor),
//    so the 3D is drawn at that resolution; what the module gives in 800x600 pixels is scaled to it while it draws to
//    the back buffer - viewports, clear rectangles and pre-transformed (XYZRHW) vertices, i.e. its 2D. Present copies
//    the back buffer 1:1 into the image rectangle.
//  uhd_battle=0 (stage A): an 800x600 back buffer, presented scaled into the image rectangle.
#include "common.h"
#include <stdlib.h>
#include <string.h>

extern RECT uhdImageRect;                  // display.cpp: where the 800x600 picture goes in the client
volatile LONG battleDrew;                  // set by Present; display.cpp repaints everything after a fight

typedef HMODULE(WINAPI* LoadLibraryA_t)(LPCSTR);
static LoadLibraryA_t nextLoadLibraryA;

typedef void* (WINAPI* Direct3DCreate8_t)(UINT);
static Direct3DCreate8_t realDirect3DCreate8;

// d3d8 structures (no d3d8.h in the current SDKs)
// D3DPRESENT_PARAMETERS: BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount, MultiSampleType,
// SwapEffect, hDeviceWindow, Windowed, EnableAutoDepthStencil, AutoDepthStencilFormat, Flags, ...
struct PP8 { UINT w, h; DWORD fmt; UINT count; DWORD ms, swap; HWND wnd; BOOL windowed, autoDepth; DWORD depthFmt, flags; };
struct VP8 { DWORD x, y, w, h; float minZ, maxZ; };                   // D3DVIEWPORT8
struct R8 { LONG x1, y1, x2, y2; };                                   // D3DRECT
struct SD8 { DWORD fmt, type, usage, pool; UINT size; DWORD ms; UINT w, h; };   // D3DSURFACE_DESC

typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(void*, UINT, DWORD, HWND, DWORD, void*, void**);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(void*, void*);
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(void*, const RECT*, const RECT*, HWND, const void*);
typedef HRESULT(STDMETHODCALLTYPE* GetBackBuffer_t)(void*, UINT, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateTexture_t)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateVertexBuffer_t)(void*, UINT, DWORD, DWORD, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateRenderTarget_t)(void*, UINT, UINT, DWORD, DWORD, BOOL, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateDepthStencilSurface_t)(void*, UINT, UINT, DWORD, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* CopyRects_t)(void*, void*, const RECT*, UINT, void*, const POINT*);
typedef HRESULT(STDMETHODCALLTYPE* GetFrontBuffer_t)(void*, void*);
typedef HRESULT(STDMETHODCALLTYPE* SetRenderTarget_t)(void*, void*, void*);
typedef HRESULT(STDMETHODCALLTYPE* Clear_t)(void*, DWORD, const R8*, DWORD, DWORD, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* SetViewport_t)(void*, const VP8*);
typedef HRESULT(STDMETHODCALLTYPE* GetViewport_t)(void*, VP8*);
typedef HRESULT(STDMETHODCALLTYPE* SetTextureStageState_t)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitive_t)(void*, DWORD, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitive_t)(void*, DWORD, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitiveUP_t)(void*, DWORD, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveUP_t)(void*, DWORD, UINT, UINT, UINT, const void*, DWORD, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* GetVertexShader_t)(void*, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetStreamSource_t)(void*, UINT, void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* GetDesc_t)(void*, SD8*);
typedef ULONG(STDMETHODCALLTYPE* Release_t)(void*);
typedef HRESULT(STDMETHODCALLTYPE* SetTexture_t)(void*, DWORD, void*);
typedef HRESULT(STDMETHODCALLTYPE* SetRenderState_t)(void*, DWORD, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* SetTransform_t)(void*, DWORD, const float*);
typedef HRESULT(STDMETHODCALLTYPE* GetSurface_t)(void*, void**);                     // GetRenderTarget, GetDepthStencilSurface
typedef HRESULT(STDMETHODCALLTYPE* CreateImageSurface_t)(void*, UINT, UINT, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* UpdateTexture_t)(void*, void*, void*);
typedef HRESULT(STDMETHODCALLTYPE* GetLevelDesc_t)(void*, UINT, SD8*);
static SetTexture_t realSetTexture;
static SetRenderState_t realSetRenderState;
static SetTransform_t realSetTransform;
static GetBackBuffer_t realGetBackBuffer;
static GetSurface_t realGetRenderTarget;
static CreateImageSurface_t realCreateImageSurface;
static UpdateTexture_t realUpdateTexture;

static CreateDevice_t realCreateDevice;
static Reset_t realReset;
static Present_t realPresent;
static CreateTexture_t realCreateTexture;
static CreateVertexBuffer_t realCreateVertexBuffer;
static CreateRenderTarget_t realCreateRenderTarget;
static CreateDepthStencilSurface_t realCreateDepthStencilSurface;
static CopyRects_t realCopyRects;
static GetFrontBuffer_t realGetFrontBuffer;
static SetRenderTarget_t realSetRenderTarget;
static Clear_t realClear;
static SetViewport_t realSetViewport;
static GetViewport_t realGetViewport;
static SetTextureStageState_t realSetTextureStageState;
static DrawPrimitive_t realDrawPrimitive;
static DrawIndexedPrimitive_t realDrawIndexedPrimitive;
static DrawPrimitiveUP_t realDrawPrimitiveUP;
static DrawIndexedPrimitiveUP_t realDrawIndexedPrimitiveUP;
static GetVertexShader_t getVertexShader;   // not hooked: asked at each draw
static SetStreamSource_t realSetStreamSource;

static ULONG Rel(void* o) { return ((Release_t)(*(void***)o)[2])(o); }
static bool Desc(void* surf, SD8* d) { return surf && SUCCEEDED(((GetDesc_t)(*(void***)surf)[8])(surf, d)); }

// ---------------------------------------------------------------- the device's state as far as scaling needs it
static bool native;                        // this device has the big back buffer
static float S = 1.0f;                     // back buffer pixels per module pixel
static UINT bbW = 800, bbH = 600;
static void* bbSurf;                       // the back buffer surface (not AddRef'd)
static bool rtIsBB = true;                 // drawing goes to the back buffer
static DWORD curFVF;                       // of the draw being made (0 = a vertex shader); asked from the device at each
                                           // draw - the module switches it with state blocks, which no setter shows
static DWORD magFilter, minFilter;         // stage 0
static BYTE* tmp;                          // scaled vertex copies
static size_t tmpCap;
static LONG frames;
// test runs (uhd_capture): the textures this fight made - all, those of 1024 or more (the originals stop at 512, so
// those come from an add-on pack's bigger ones), and their top levels' pixels
static volatile LONG texMade, texBig;
static volatile LONGLONG texPixels;
// the frame of the fight's first 3D draw: the arena's first frame. Before it the loading screen (2D only) stays as long
// as the agent waits to press a key - 479, 481, 511 frames in three runs (10-16) - so uhd_capfrom 1 counts from here.
static LONG fightAt;
static LONG idxDraws, upDraws;             // test runs: this frame's indexed vertex-buffer (3D) and user-pointer draws
static LONG vbDraws, copies;               // ... its plain vertex-buffer draws and CopyRects

static const DWORD kXYZRHW = 0x004, kPositionMask = 0x00E;

// trace: every 30th frame, every draw of that frame with its state (to find draws that do not follow the screen)
static bool dumping;
static LONG dumpLines, dumps;
static void* tex0;                         // stage 0 texture
static void* tex1;                         // stage 1 texture (the module's environment map goes there)
static void* devPtr;                       // the battle device (for asking its state outside the device hooks)
static DWORD blendOn, srcBlend, dstBlend, zOn, fillMode = 3, alphaTest;

// stage 1 as the device has it now (asked, not tracked - state blocks set it too): colour op, coordinate index, transform flags
typedef HRESULT(STDMETHODCALLTYPE* GetTextureStageState_t)(void*, DWORD, DWORD, DWORD*);
static GetTextureStageState_t getTextureStageState;   // not hooked
static void Stage1(void* self, DWORD* op, DWORD* tci, DWORD* ttf)
{
    *op = 1; *tci = 1; *ttf = 0;
    if (!getTextureStageState) return;
    getTextureStageState(self, 1, 1, op);                    // D3DTSS_COLOROP
    getTextureStageState(self, 1, 11, tci);                  // D3DTSS_TEXCOORDINDEX
    getTextureStageState(self, 1, 24, ttf);                  // D3DTSS_TEXTURETRANSFORMFLAGS
}
static float proj[16], view[16], world[16];

static void TexSize(void* t, UINT* w, UINT* h)
{
    *w = *h = 0;
    SD8 d = {};
    if (t && SUCCEEDED(((GetLevelDesc_t)(*(void***)t)[14])(t, 0, &d))) { *w = d.w; *h = d.h; }
}

// trace: each kind of draw once (draw call, vertex format, primitive, fill mode, blending) - rare effects show up even
// when no frame dump catches them
static void NoteKind(int kindIdx, const char* kind, DWORD type, UINT count, const void* data, UINT)
{
    if (!cfg.trace) return;
    DWORD op1, tci1, ttf1;
    Stage1(devPtr, &op1, &tci1, &ttf1);
    DWORD key = (DWORD)kindIdx | (type & 7) << 2 | (fillMode & 3) << 5 | (blendOn ? 1u : 0u) << 7 | (srcBlend & 15) << 8 |
                (dstBlend & 15) << 12 | (alphaTest ? 1u : 0u) << 16 | (curFVF & 0x7ff) << 17 | (op1 > 1 ? 1u : 0u) << 28 |
                (tci1 >> 16 & 3) << 29;
    if (!Budget("bt:kind", key, 1)) return;
    UINT tw, th, t1w, t1h;
    TexSize(tex0, &tw, &th);
    TexSize(op1 > 1 ? tex1 : NULL, &t1w, &t1h);
    const float* v = (const float*)data;
    Log("battle new draw kind: %s fvf %03lx type %lu n%u fill %lu blend %lu %lu/%lu alphatest %lu tex %ux%u rt %d first (%.1f,%.1f) | stage1 op %lu tci %08lx ttf %lu tex %ux%u",
        kind, curFVF, type, count, fillMode, blendOn, srcBlend, dstBlend, alphaTest, tw, th, rtIsBB, v ? v[0] : 0.0f, v ? v[1] : 0.0f,
        op1, tci1, ttf1, t1w, t1h);
}

static void DumpDraw(const char* kind, DWORD type, UINT count, const void* data, UINT stride)
{
    if (!dumping || ++dumpLines > 400) return;
    UINT tw, th;
    TexSize(tex0, &tw, &th);
    char v[200] = "";
    if (data && count && stride >= 12)
    {
        const float* a = (const float*)data;
        const float* b = (const float*)((const BYTE*)data + (size_t)stride * (count - 1));
        _snprintf_s(v, _TRUNCATE, " v0 (%.1f,%.1f,%.3f) vN (%.1f,%.1f,%.3f)", a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    char m[160] = "";
    if ((curFVF & kPositionMask) != kXYZRHW)
        _snprintf_s(m, _TRUNCATE, " proj %.3f %.3f %.3f %.3f %.3f world41 %.1f %.1f", proj[0], proj[5], proj[10], proj[14], proj[15], world[12], world[13]);
    DWORD op1, tci1, ttf1;
    Stage1(devPtr, &op1, &tci1, &ttf1);
    char e[96] = "";
    if (op1 > 1) _snprintf_s(e, _TRUNCATE, " | stage1 op %lu tci %08lx ttf %lu", op1, tci1, ttf1);
    Log("  %s fvf %03lx t%lu n%u tex %ux%u blend %lu %lu/%lu z %lu rt %d%s%s%s", kind, curFVF, type, count, tw, th, blendOn, srcBlend, dstBlend, zOn, rtIsBB, v, m, e);
}

// test only (uhd_envfix): around each environment-mapped draw (stage 1 on, coordinates from the camera-space reflection
// vector) - 1: those coordinates taken as two through an identity texture transform; 2: the reflection texture alone
static bool envApplied;
static DWORD envOp1, envTtf1;
static void EnvBegin(void* self)
{
    envApplied = false;
    if (!cfg.envfix || !realSetTextureStageState) return;
    DWORD tci1;
    Stage1(self, &envOp1, &tci1, &envTtf1);
    if (envOp1 <= 1 || (tci1 & 0xFFFF0000) != 0x30000) return;
    if (cfg.envfix == 1 && realSetTransform)
    {
        static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        realSetTransform(self, 17, identity);                    // D3DTS_TEXTURE1
        realSetTextureStageState(self, 1, 24, 2);                // TEXTURETRANSFORMFLAGS = COUNT2
        envApplied = true;
    }
    else if (cfg.envfix == 2)
    {
        realSetTextureStageState(self, 1, 1, 2);                 // COLOROP = SELECTARG1: the reflection texture alone
        envApplied = true;
    }
    static LONG told;
    if (envApplied && !told++) Log("environment map test %d applied (stage 1 op %lu tci %08lx ttf %lu)", cfg.envfix, envOp1, tci1, envTtf1);
}

static void EnvEnd(void* self)
{
    if (!envApplied) return;
    if (cfg.envfix == 1) realSetTextureStageState(self, 1, 24, envTtf1);
    else realSetTextureStageState(self, 1, 1, envOp1);
    envApplied = false;
}

static bool Scale2D() { return native && rtIsBB && (curFVF & kPositionMask) == kXYZRHW; }
static DWORD Up(DWORD v) { return (DWORD)(v * S + 0.5f); }
static LONG UpL(LONG v) { return (LONG)(v * S + (v >= 0 ? 0.5f : -0.5f)); }
static DWORD Down(DWORD v) { return (DWORD)(v / S + 0.5f); }

static void FixParams(void* p, const char* when)
{
    PP8* pp = (PP8*)p;
    if (!pp) return;
    LONG iw = uhdImageRect.right - uhdImageRect.left, ih = uhdImageRect.bottom - uhdImageRect.top;
    native = pp->windowed && cfg.battle && iw >= 800 && ih >= 600;
    UINT w = native ? (UINT)iw : 800, h = native ? (UINT)ih : 600;
    Log("battle %s: windowed %d, back buffer %ux%u fmt %lu x%u, multisample %lu, swap %lu, auto depth %d fmt %lu, flags %lx -> %ux%u%s",
        when, pp->windowed, pp->w, pp->h, pp->fmt, pp->count, pp->ms, pp->swap, pp->autoDepth, pp->depthFmt, pp->flags, w, h,
        native ? " (native)" : "");
    if (!pp->windowed) return;
    if (cfg.capFirst) pp->flags |= 1;                             // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER (frame capture)
    pp->w = w;
    pp->h = h;
    bbW = w;
    bbH = h;
    S = native ? (float)iw / 800.0f : 1.0f;
}

static void FindBackBuffer(void* dev)
{
    void* s = NULL;
    bbSurf = NULL;
    if (SUCCEEDED(((GetBackBuffer_t)(*(void***)dev)[16])(dev, 0, 0, &s)) && s)
    {
        bbSurf = s;
        Rel(s);                                                  // the swap chain keeps it
    }
    rtIsBB = true;
    curFVF = 0;
    magFilter = minFilter = 0;
}

// ---------------------------------------------------------------- presenting
// test only (uhd_capture): the finished frame from the back buffer, k x k averaged when native (uhd_capture's /k, default
// 4: 2880x2160 -> 720x540; 1 = every pixel), as a 24-bit BMP autobattle\cap\fNNNNN_native.bmp / _800.bmp
struct LR8 { INT pitch; void* bits; };                                // D3DLOCKED_RECT
typedef HRESULT(STDMETHODCALLTYPE* LockRect_t)(void*, LR8*, const RECT*, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* UnlockRect_t)(void*);

static void CaptureFrame(LONG f, int ri)
{
    if (!bbSurf) return;
    LR8 lr = {};
    if (FAILED(((LockRect_t)(*(void***)bbSurf)[9])(bbSurf, &lr, NULL, 0x10)) || !lr.bits)     // D3DLOCK_READONLY
    {
        static LONG warned;
        if (!warned++) Log("frame capture: the back buffer could not be locked");
        return;
    }
    int k = native ? cfg.capK[ri] : 1, x0 = 0, y0 = 0, ow = (int)bbW / k, oh = (int)bbH / k;
    if (cfg.capW[ri] > 0)                                       // only a rectangle, every pixel
    {
        k = 1;
        x0 = cfg.capX[ri] < 0 ? 0 : (cfg.capX[ri] >= (int)bbW ? (int)bbW - 1 : cfg.capX[ri]);
        y0 = cfg.capY[ri] < 0 ? 0 : (cfg.capY[ri] >= (int)bbH ? (int)bbH - 1 : cfg.capY[ri]);
        ow = cfg.capW[ri] < (int)bbW - x0 ? cfg.capW[ri] : (int)bbW - x0;
        oh = cfg.capH[ri] < (int)bbH - y0 ? cfg.capH[ri] : (int)bbH - y0;
    }
    int rowBytes = (ow * 3 + 3) & ~3;
    BYTE* out = (BYTE*)malloc((size_t)rowBytes * oh);
    if (out)
    {
        for (int y = 0; y < oh; y++)
        {
            BYTE* o = out + (size_t)(oh - 1 - y) * rowBytes;                  // BMP rows bottom-up
            for (int x = 0; x < ow; x++)
            {
                unsigned b = 0, g = 0, r = 0;
                for (int dy = 0; dy < k; dy++)
                {
                    const BYTE* p = (const BYTE*)lr.bits + (size_t)(y0 + y * k + dy) * lr.pitch + (size_t)(x0 + x * k) * 4;
                    for (int dx = 0; dx < k; dx++, p += 4) { b += p[0]; g += p[1]; r += p[2]; }
                }
                o[x * 3] = (BYTE)(b / (k * k)); o[x * 3 + 1] = (BYTE)(g / (k * k)); o[x * 3 + 2] = (BYTE)(r / (k * k));
            }
        }
    }
    ((UnlockRect_t)(*(void***)bbSurf)[10])(bbSurf);
    if (!out) return;
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    swprintf_s(dir, L"%s\\cap", abDir);
    CreateDirectoryW(dir, NULL);
    swprintf_s(path, L"%s\\f%05ld_%s.bmp", dir, f, native ? L"native" : L"800");
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof ih; ih.biWidth = ow; ih.biHeight = oh; ih.biPlanes = 1; ih.biBitCount = 24;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + (DWORD)rowBytes * oh;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD wr;
        WriteFile(file, &fh, sizeof fh, &wr, NULL);
        WriteFile(file, &ih, sizeof ih, &wr, NULL);
        WriteFile(file, out, (DWORD)rowBytes * oh, &wr, NULL);
        CloseHandle(file);
    }
    free(out);
}

static HRESULT STDMETHODCALLTYPE HookPresent(void* self, const RECT* src, const RECT* dst, HWND wnd, const void* dirty)
{
    battleDrew = 1;
    LONG f = InterlockedIncrement(&frames);
    if (dumping) dumping = false;
    else if (cfg.trace && f % 30 == 0 && dumps < 120)
    {
        dumping = true;
        dumpLines = 0;
        dumps++;
        Log("---- battle frame %ld", f + 1);
    }
    LONG cf = !cfg.capFrom ? f : (fightAt ? f - fightAt + 1 : 0);   // the capture frame number (0: none yet)
    for (int i = 0; i < cfg.capN; i++)
        if (cf >= cfg.capA[i] && cf <= cfg.capB[i] && (cf - cfg.capA[i]) % cfg.capS[i] == 0) { CaptureFrame(cf, i); break; }
    if (cfg.capFirst && f % 600 == 0)
        Log("battle frame %ld: %ld textures made, %ld of them 1024 or more, top levels %lld kpixels", f, texMade, texBig,
            texPixels / 1000);
    if (cfg.capFirst && f <= 2400 && f % 5 == 0)                // test runs: what a loading frame and a fight frame draw
        Log("battle frame %ld draws: 3D %ld, 2D %ld, VB %ld, copies %ld", f, idxDraws, upDraws, vbDraws, copies);
    if (cfg.capFirst) idxDraws = upDraws = vbDraws = copies = 0;
    if (!dst && uhdImageRect.right > uhdImageRect.left) dst = &uhdImageRect;
    return realPresent(self, src, dst, wnd, dirty);
}

static HRESULT STDMETHODCALLTYPE HookReset(void* self, void* pp)
{
    FixParams(pp, "reset");
    HRESULT hr = realReset(self, pp);
    if (SUCCEEDED(hr)) FindBackBuffer(self);
    else Log("battle reset failed: %08lx", hr);
    return hr;
}

// ---------------------------------------------------------------- where drawing goes
static HRESULT STDMETHODCALLTYPE HookSetRenderTarget(void* self, void* rt, void* z)
{
    HRESULT hr = realSetRenderTarget(self, rt, z);
    if (SUCCEEDED(hr) && rt) rtIsBB = rt == bbSurf;
    if (cfg.trace && Budget("bt:SetRenderTarget", Rva(_ReturnAddress()), 16))
    {
        SD8 d = {};
        Desc(rt, &d);
        Log("battle SetRenderTarget %p %ux%u (back buffer %d) z %p -> %08lx", rt, d.w, d.h, rt == bbSurf, z, hr);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetViewport(void* self, const VP8* vp)
{
    if (native && rtIsBB && vp && vp->x + vp->w <= 800 && vp->y + vp->h <= 600)
    {
        VP8 v = *vp;
        v.x = Up(vp->x);
        v.y = Up(vp->y);
        v.w = Up(vp->x + vp->w) - v.x;
        v.h = Up(vp->y + vp->h) - v.y;
        if (cfg.trace && Budget("bt:SetViewport", Rva(_ReturnAddress()), 8))
            Log("battle SetViewport (%lu,%lu) %lux%lu -> (%lu,%lu) %lux%lu", vp->x, vp->y, vp->w, vp->h, v.x, v.y, v.w, v.h);
        return realSetViewport(self, &v);
    }
    if (cfg.trace && vp && Budget("bt:SetViewport-as-is", Rva(_ReturnAddress()), 8))
        Log("battle SetViewport (%lu,%lu) %lux%lu as is (back buffer %d)", vp->x, vp->y, vp->w, vp->h, rtIsBB);
    return realSetViewport(self, vp);
}

static HRESULT STDMETHODCALLTYPE HookGetViewport(void* self, VP8* vp)             // the module sees its 800x600
{
    HRESULT hr = realGetViewport(self, vp);
    if (SUCCEEDED(hr) && native && rtIsBB && vp && vp->x + vp->w <= bbW && vp->y + vp->h <= bbH)
    {
        DWORD x2 = Down(vp->x + vp->w), y2 = Down(vp->y + vp->h);
        vp->x = Down(vp->x);
        vp->y = Down(vp->y);
        vp->w = x2 - vp->x;
        vp->h = y2 - vp->y;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookClear(void* self, DWORD n, const R8* r, DWORD flags, DWORD color, float z, DWORD stencil)
{
    R8 buf[32];
    if (native && rtIsBB && n && r && n <= 32)
    {
        for (DWORD i = 0; i < n; i++)
        {
            buf[i].x1 = UpL(r[i].x1);
            buf[i].y1 = UpL(r[i].y1);
            buf[i].x2 = UpL(r[i].x2);
            buf[i].y2 = UpL(r[i].y2);
        }
        r = buf;
    }
    return realClear(self, n, r, flags, color, z, stencil);
}

// ---------------------------------------------------------------- 2D: pre-transformed vertices
static void AskFVF(void* self)
{
    DWORD h = 0;
    if (getVertexShader && SUCCEEDED(getVertexShader(self, &h))) curFVF = (h & 1) ? 0 : h;   // shader handles are odd
}

static UINT VertexCount(DWORD type, UINT n)
{
    switch (type)
    {
    case 1: return n;                      // point list
    case 2: return 2 * n;                  // line list
    case 3: return n + 1;                  // line strip
    case 4: return 3 * n;                  // triangle list
    case 5: case 6: return n + 2;          // triangle strip, fan
    }
    return 0;
}

static const void* ScaledCopy(const void* data, UINT count, UINT stride)
{
    size_t need = (size_t)count * stride;
    if (!count || stride < 8) return data;
    if (need > tmpCap)
    {
        size_t cap = need < 65536 ? 65536 : need * 2;
        BYTE* t = (BYTE*)realloc(tmp, cap);
        if (!t) return data;
        tmp = t;
        tmpCap = cap;
    }
    memcpy(tmp, data, need);
    for (UINT i = 0; i < count; i++)
    {
        float* p = (float*)(tmp + (size_t)i * stride);           // pixel edges stay on pixel edges
        p[0] = (p[0] + 0.5f) * S - 0.5f;
        p[1] = (p[1] + 0.5f) * S - 0.5f;
    }
    return tmp;
}

static void Trace2D(const char* fn, DWORD type, UINT count, const void* data, UINT stride)
{
    if (!cfg.trace || !Budget(fn, (DWORD)(curFVF << 8) | type, 6)) return;
    const float* v = (const float*)data;
    Log("battle %s fvf %03lx type %lu vertices %u stride %u filter mag %lu min %lu, rt is back buffer %d, first (%.1f,%.1f)",
        fn, curFVF, type, count, stride, magFilter, minFilter, rtIsBB, v ? v[0] : 0.0f, v ? v[1] : 0.0f);
}

static HRESULT STDMETHODCALLTYPE HookDrawPrimitiveUP(void* self, DWORD type, UINT n, const void* data, UINT stride)
{
    UINT c = VertexCount(type, n);
    if (cfg.capFirst) upDraws++;
    AskFVF(self);
    if ((curFVF & kPositionMask) == kXYZRHW)
    {
        Trace2D("bt:DrawPrimitiveUP", type, c, data, stride);
        if (Scale2D() && data) data = ScaledCopy(data, c, stride);
    }
    NoteKind(0, "UP", type, c, data, stride);
    DumpDraw("UP", type, c, data, stride);
    EnvBegin(self);
    HRESULT hr = realDrawPrimitiveUP(self, type, n, data, stride);
    EnvEnd(self);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookDrawIndexedPrimitiveUP(void* self, DWORD type, UINT minIndex, UINT numVertices, UINT n,
                                                           const void* idx, DWORD idxFmt, const void* data, UINT stride)
{
    AskFVF(self);
    if ((curFVF & kPositionMask) == kXYZRHW)
    {
        Trace2D("bt:DrawIndexedPrimitiveUP", type, minIndex + numVertices, data, stride);
        if (Scale2D() && data) data = ScaledCopy(data, minIndex + numVertices, stride);
    }
    NoteKind(1, "IdxUP", type, minIndex + numVertices, data, stride);
    DumpDraw("IdxUP", type, minIndex + numVertices, data, stride);
    EnvBegin(self);
    HRESULT hr = realDrawIndexedPrimitiveUP(self, type, minIndex, numVertices, n, idx, idxFmt, data, stride);
    EnvEnd(self);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookDrawPrimitive(void* self, DWORD type, UINT start, UINT n)
{
    if (cfg.capFirst) vbDraws++;
    if (cfg.trace) AskFVF(self);
    if ((curFVF & kPositionMask) == kXYZRHW && cfg.trace && Budget("bt:DrawPrimitive-XYZRHW", Rva(_ReturnAddress()), 6))
        Log("battle DrawPrimitive from a vertex buffer with fvf %03lx type %lu start %u count %u - NOT scaled", curFVF, type, start, n);
    NoteKind(2, "VB", type, n, NULL, 0);
    DumpDraw("VB", type, n, NULL, 0);
    EnvBegin(self);
    HRESULT hr = realDrawPrimitive(self, type, start, n);
    EnvEnd(self);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookDrawIndexedPrimitive(void* self, DWORD type, UINT minIndex, UINT numVertices, UINT start, UINT n)
{
    if (cfg.capFirst)
    {
        idxDraws++;
        if (!fightAt)
        {
            fightAt = frames + 1;
            Log("battle: first 3D draw in frame %ld%s", fightAt, cfg.capFrom ? " - captures count from here" : "");
        }
    }
    if (cfg.trace) AskFVF(self);
    if ((curFVF & kPositionMask) == kXYZRHW && cfg.trace && Budget("bt:DrawIndexedPrimitive-XYZRHW", Rva(_ReturnAddress()), 6))
        Log("battle DrawIndexedPrimitive from a vertex buffer with fvf %03lx type %lu vertices %u - NOT scaled", curFVF, type, numVertices);
    NoteKind(3, "IdxVB", type, numVertices, NULL, 0);
    DumpDraw("IdxVB", type, numVertices, NULL, 0);
    EnvBegin(self);
    HRESULT hr = realDrawIndexedPrimitive(self, type, minIndex, numVertices, start, n);
    EnvEnd(self);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetTextureStageState(void* self, DWORD stage, DWORD type, DWORD value)
{
    if (stage == 0 && type == 16) magFilter = value;             // D3DTSS_MAGFILTER
    if (stage == 0 && type == 17) minFilter = value;             // D3DTSS_MINFILTER
    // stage 1 and up: how a second texture (an environment map, say) is combined and where its coordinates come from
    if (cfg.trace && stage > 0 && (type <= 4 || type == 11 || type == 24) &&
        Budget("bt:stage-state", stage << 24 | type << 16 | (value & 0xff) | (value >> 16 & 0xff) << 8, 1))
        Log("battle stage %lu state %lu = %08lx", stage, type, value);
    return realSetTextureStageState(self, stage, type, value);
}

static HRESULT STDMETHODCALLTYPE HookSetStreamSource(void* self, UINT stream, void* vb, UINT stride)
{
    return realSetStreamSource(self, stream, vb, stride);
}

// trace: each stage-1 texture once, as a 32-bit BMP autobattle\cap\stage1_<pointer>.bmp (A8R8G8B8/X8R8G8B8 only) - what
// the environment map holds
static void* dumpedTex[16];
static void DumpStageTexture(void* t)
{
    for (int i = 0; i < 16; i++) if (dumpedTex[i] == t) return;
    for (int i = 0; i < 16; i++) if (!dumpedTex[i]) { dumpedTex[i] = t; break; }
    typedef DWORD(STDMETHODCALLTYPE* GetType_t)(void*);
    typedef HRESULT(STDMETHODCALLTYPE* TexLock_t)(void*, UINT, LR8*, const RECT*, DWORD);
    typedef HRESULT(STDMETHODCALLTYPE* TexUnlock_t)(void*, UINT);
    if (((GetType_t)(*(void***)t)[10])(t) != 3) return;          // a plain 2D texture
    SD8 d = {};
    if (FAILED(((GetLevelDesc_t)(*(void***)t)[14])(t, 0, &d))) return;
    Log("stage-1 texture %p: %ux%u fmt %lu pool %lu usage %lx", t, d.w, d.h, d.fmt, d.pool, d.usage);
    bool dxt = d.fmt == 0x31545844 || d.fmt == 0x33545844 || d.fmt == 0x35545844;   // DXT1, DXT3, DXT5
    if ((d.fmt != 21 && d.fmt != 22 && !dxt) || d.w > 1024 || d.h > 1024) return;
    LR8 lr = {};
    if (FAILED(((TexLock_t)(*(void***)t)[16])(t, 0, &lr, NULL, 0x10)) || !lr.bits) { Log("  (could not lock it)"); return; }
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    swprintf_s(dir, L"%s\\cap", abDir);
    CreateDirectoryW(dir, NULL);
    if (dxt)                                                     // compressed: level 0 as a DDS file
    {
        UINT bw = (d.w + 3) / 4, bh = (d.h + 3) / 4, block = d.fmt == 0x31545844 ? 8 : 16, rowBytes = bw * block;
        DWORD hdr[32] = {};
        hdr[0] = 0x20534444;                                     // "DDS "
        hdr[1] = 124; hdr[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000; hdr[3] = d.h; hdr[4] = d.w; hdr[5] = rowBytes * bh;
        hdr[19] = 32; hdr[20] = 0x4; hdr[21] = d.fmt;            // pixel format: FOURCC
        hdr[27] = 0x1000;                                        // DDSCAPS_TEXTURE
        swprintf_s(path, L"%s\\stage1_%p.dds", dir, t);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD wr;
            WriteFile(file, hdr, sizeof hdr, &wr, NULL);
            for (UINT y = 0; y < bh; y++) WriteFile(file, (const BYTE*)lr.bits + (size_t)y * lr.pitch, rowBytes, &wr, NULL);
            CloseHandle(file);
        }
        ((TexUnlock_t)(*(void***)t)[17])(t, 0);
        return;
    }
    swprintf_s(path, L"%s\\stage1_%p.bmp", dir, t);
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof ih; ih.biWidth = (LONG)d.w; ih.biHeight = -(LONG)d.h; ih.biPlanes = 1; ih.biBitCount = 32;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + d.w * d.h * 4;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD wr;
        WriteFile(file, &fh, sizeof fh, &wr, NULL);
        WriteFile(file, &ih, sizeof ih, &wr, NULL);
        for (UINT y = 0; y < d.h; y++) WriteFile(file, (const BYTE*)lr.bits + (size_t)y * lr.pitch, d.w * 4, &wr, NULL);
        CloseHandle(file);
    }
    ((TexUnlock_t)(*(void***)t)[17])(t, 0);
}

static HRESULT STDMETHODCALLTYPE HookSetTexture(void* self, DWORD stage, void* t)
{
    if (stage == 0) tex0 = t;
    if (stage == 1) tex1 = t;
    if (cfg.trace && stage == 1 && t) DumpStageTexture(t);
    if (cfg.trace && stage > 0 && t && Budget("bt:SetTexture-stage", stage, 3))
    {
        typedef DWORD(STDMETHODCALLTYPE* GetType_t)(void*);
        DWORD type = ((GetType_t)(*(void***)t)[10])(t);              // IDirect3DResource8::GetType: 3 texture, 4 volume, 5 cube
        UINT w = 0, h = 0;
        if (type == 3) TexSize(t, &w, &h);
        Log("battle texture on stage %lu: type %lu %ux%u (caller %08lx)", stage, type, w, h, Rva(_ReturnAddress()));
    }
    return realSetTexture(self, stage, t);
}

static HRESULT STDMETHODCALLTYPE HookSetRenderState(void* self, DWORD state, DWORD value)
{
    switch (state)
    {
    case 7: zOn = value; break;                                  // D3DRS_ZENABLE
    case 19: srcBlend = value; break;                            // D3DRS_SRCBLEND
    case 20: dstBlend = value; break;                            // D3DRS_DESTBLEND
    case 27: blendOn = value; break;                             // D3DRS_ALPHABLENDENABLE
    case 8: fillMode = value; break;                             // D3DRS_FILLMODE (1 point, 2 wireframe, 3 solid)
    case 15: alphaTest = value; break;                           // D3DRS_ALPHATESTENABLE
    }
    // states whose look depends on the pixel size: fill mode, line pattern, z bias, point sizes and sprites, alpha ref
    if (cfg.trace && (state == 8 || state == 10 || state == 47 || (state >= 154 && state <= 161) || state == 166 || state == 24) &&
        Budget("bt:state", state << 16 | (value & 0xffff), 1))
        Log("battle render state %lu = %08lx (caller %08lx)", state, value, Rva(_ReturnAddress()));
    return realSetRenderState(self, state, value);
}

static HRESULT STDMETHODCALLTYPE HookSetTransform(void* self, DWORD state, const float* mtx)
{
    if (mtx)
    {
        if (state == 3) memcpy(proj, mtx, sizeof proj);
        else if (state == 2) memcpy(view, mtx, sizeof view);
        else if (state == 256) memcpy(world, mtx, sizeof world);
    }
    return realSetTransform(self, state, mtx);
}

static HRESULT STDMETHODCALLTYPE HookGetBackBuffer(void* self, UINT i, DWORD type, void** out)
{
    if (cfg.trace && Budget("bt:GetBackBuffer", Rva(_ReturnAddress()), 6)) Log("battle GetBackBuffer %u (caller %08lx)", i, Rva(_ReturnAddress()));
    return realGetBackBuffer(self, i, type, out);
}

static HRESULT STDMETHODCALLTYPE HookGetRenderTarget(void* self, void** out)
{
    if (cfg.trace && Budget("bt:GetRenderTarget", Rva(_ReturnAddress()), 6)) Log("battle GetRenderTarget (caller %08lx)", Rva(_ReturnAddress()));
    return realGetRenderTarget(self, out);
}

static HRESULT STDMETHODCALLTYPE HookCreateImageSurface(void* self, UINT w, UINT h, DWORD fmt, void** out)
{
    HRESULT hr = realCreateImageSurface(self, w, h, fmt, out);
    if (cfg.trace && Budget("bt:CreateImageSurface", Rva(_ReturnAddress()), 8)) Log("battle CreateImageSurface %ux%u fmt %lu -> %08lx", w, h, fmt, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookUpdateTexture(void* self, void* src, void* dst)
{
    if (cfg.trace && Budget("bt:UpdateTexture", Rva(_ReturnAddress()), 8)) Log("battle UpdateTexture");
    return realUpdateTexture(self, src, dst);
}

// ---------------------------------------------------------------- resources (logged; sizes the module ties to the screen)
static HRESULT STDMETHODCALLTYPE HookCreateTexture(void* self, UINT w, UINT h, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void** out)
{
    HRESULT hr = realCreateTexture(self, w, h, levels, usage, fmt, pool, out);
    if (cfg.trace && (usage & 3) && Budget("bt:CreateTexture-target", Rva(_ReturnAddress()), 16))
        Log("battle CreateTexture %ux%u levels %u usage %lx fmt %lu pool %lu -> %08lx", w, h, levels, usage, fmt, pool, hr);
    if (cfg.trace && Budget("bt:CreateTexture-kind", fmt << 8 | (usage & 0xf) << 4 | (pool & 0xf), 1))
        Log("battle texture kind: fmt %lu usage %lx pool %lu levels %u (first %ux%u) -> %08lx", fmt, usage, pool, levels, w, h, hr);
    if (SUCCEEDED(hr) && cfg.capFirst)
    {
        InterlockedIncrement(&texMade);
        InterlockedExchangeAdd64(&texPixels, (LONGLONG)w * h);
        if ((w >= 1024 || h >= 1024) && InterlockedIncrement(&texBig) <= 12)
            Log("battle texture %ux%u levels %u fmt %08lx pool %lu made (a big one - from an add-on pack)", w, h, levels, fmt, pool);
    }
    if (FAILED(hr) && Budget("bt:CreateTexture-failed", fmt << 8 | (usage & 0xff), 4))
        Log("battle CreateTexture FAILED %ux%u levels %u usage %lx fmt %lu pool %lu -> %08lx (caller %08lx)", w, h, levels, usage, fmt, pool, hr,
            Rva(_ReturnAddress()));
    return hr;
}

// cube and volume textures (an environment map would be one): every kind once when tracing, failures always
typedef HRESULT(STDMETHODCALLTYPE* CreateCubeTexture_t)(void*, UINT, UINT, DWORD, DWORD, DWORD, void**);
typedef HRESULT(STDMETHODCALLTYPE* CreateVolumeTexture_t)(void*, UINT, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
static CreateCubeTexture_t realCreateCubeTexture;
static CreateVolumeTexture_t realCreateVolumeTexture;

static HRESULT STDMETHODCALLTYPE HookCreateCubeTexture(void* self, UINT edge, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void** out)
{
    HRESULT hr = realCreateCubeTexture(self, edge, levels, usage, fmt, pool, out);
    if (FAILED(hr) || (cfg.trace && Budget("bt:CreateCubeTexture", fmt << 8 | (usage & 0xff), 2)))
        Log("battle CreateCubeTexture %u levels %u usage %lx fmt %lu pool %lu -> %08lx (caller %08lx)", edge, levels, usage, fmt, pool, hr,
            Rva(_ReturnAddress()));
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateVolumeTexture(void* self, UINT w, UINT h, UINT d, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void** out)
{
    HRESULT hr = realCreateVolumeTexture(self, w, h, d, levels, usage, fmt, pool, out);
    if (FAILED(hr) || (cfg.trace && Budget("bt:CreateVolumeTexture", fmt << 8 | (usage & 0xff), 2)))
        Log("battle CreateVolumeTexture %ux%ux%u levels %u usage %lx fmt %lu pool %lu -> %08lx", w, h, d, levels, usage, fmt, pool, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateVertexBuffer(void* self, UINT len, DWORD usage, DWORD fvf, DWORD pool, void** out)
{
    HRESULT hr = realCreateVertexBuffer(self, len, usage, fvf, pool, out);
    if (cfg.trace && Budget("bt:CreateVertexBuffer", (fvf << 4) | (usage & 0xf), 4))
        Log("battle CreateVertexBuffer %u bytes usage %lx fvf %03lx pool %lu -> %08lx", len, usage, fvf, pool, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateRenderTarget(void* self, UINT w, UINT h, DWORD fmt, DWORD ms, BOOL lockable, void** out)
{
    HRESULT hr = realCreateRenderTarget(self, w, h, fmt, ms, lockable, out);
    if (cfg.trace) Log("battle CreateRenderTarget %ux%u fmt %lu ms %lu lockable %d -> %08lx", w, h, fmt, ms, lockable, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateDepthStencilSurface(void* self, UINT w, UINT h, DWORD fmt, DWORD ms, void** out)
{
    UINT ow = w, oh = h;
    if (native && w == 800 && h == 600) { w = bbW; h = bbH; }   // a depth buffer for the back buffer must be as big
    HRESULT hr = realCreateDepthStencilSurface(self, w, h, fmt, ms, out);
    Log("battle CreateDepthStencilSurface %ux%u -> %ux%u fmt %lu -> %08lx", ow, oh, w, h, fmt, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCopyRects(void* self, void* src, const RECT* r, UINT n, void* dst, const POINT* pts)
{
    if (cfg.capFirst) copies++;
    if (cfg.trace && Budget("bt:CopyRects", Rva(_ReturnAddress()), 8))
    {
        SD8 a = {}, b = {};
        Desc(src, &a);
        Desc(dst, &b);
        Log("battle CopyRects %ux%u%s -> %ux%u%s, %u rects, first (%ld,%ld)-(%ld,%ld)", a.w, a.h, src == bbSurf ? " (back buffer)" : "",
            b.w, b.h, dst == bbSurf ? " (back buffer)" : "", n, r ? r->left : 0, r ? r->top : 0, r ? r->right : 0, r ? r->bottom : 0);
    }
    return realCopyRects(self, src, r, n, dst, pts);
}

static HRESULT STDMETHODCALLTYPE HookGetFrontBuffer(void* self, void* dst)
{
    Log("battle GetFrontBuffer");
    return realGetFrontBuffer(self, dst);
}

// ---------------------------------------------------------------- the device
// trace: what the module learns about the card and whether its shaders are made (a failed shader may send an effect
// down a plainer path)
typedef HRESULT(STDMETHODCALLTYPE* CreateVertexShader_t)(void*, const DWORD*, const DWORD*, DWORD*, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* CreatePixelShader_t)(void*, const DWORD*, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* DevGetDeviceCaps_t)(void*, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* D3DGetDeviceCaps_t)(void*, UINT, DWORD, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetPixelShader_t)(void*, DWORD);
static CreateVertexShader_t realCreateVertexShader;
static CreatePixelShader_t realCreatePixelShader;
static DevGetDeviceCaps_t realDevGetDeviceCaps;
static D3DGetDeviceCaps_t realD3DGetDeviceCaps;
static SetPixelShader_t realSetPixelShader;

static void LogCaps(const char* who, const DWORD* c, HRESULT hr)
{
    if (!cfg.trace || !c || FAILED(hr) || !Budget("bt:caps", (DWORD)(UINT_PTR)who, 2)) return;
    // D3DCAPS8 DWORD offsets: 9 RasterCaps, 14 ShadeCaps, 15 TextureCaps, 36 TextureOpCaps, 37 MaxTextureBlendStages,
    // 38 MaxSimultaneousTextures, 39 VertexProcessingCaps, 49 VertexShaderVersion, 51 PixelShaderVersion, 7 DevCaps
    Log("battle caps (%s): devcaps %08lx raster %08lx shade %08lx texture %08lx texop %08lx stages %lu/%lu vproc %08lx vs %08lx ps %08lx",
        who, c[7], c[9], c[14], c[15], c[36], c[37], c[38], c[39], c[49], c[51]);
}

static HRESULT STDMETHODCALLTYPE HookDevGetDeviceCaps(void* self, DWORD* caps)
{
    HRESULT hr = realDevGetDeviceCaps(self, caps);
    LogCaps("device", caps, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookD3DGetDeviceCaps(void* self, UINT adapter, DWORD type, DWORD* caps)
{
    HRESULT hr = realD3DGetDeviceCaps(self, adapter, type, caps);
    LogCaps(type == 1 ? "d3d HAL" : "d3d other", caps, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateVertexShader(void* self, const DWORD* decl, const DWORD* fn, DWORD* handle, DWORD usage)
{
    HRESULT hr = realCreateVertexShader(self, decl, fn, handle, usage);
    if (cfg.trace || FAILED(hr))
        Log("battle CreateVertexShader version %08lx usage %lx -> %08lx handle %lx (caller %08lx)", fn ? fn[0] : 0, usage, hr,
            handle ? *handle : 0, Rva(_ReturnAddress()));
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreatePixelShader(void* self, const DWORD* fn, DWORD* handle)
{
    HRESULT hr = realCreatePixelShader(self, fn, handle);
    if (cfg.trace || FAILED(hr))
        Log("battle CreatePixelShader version %08lx -> %08lx handle %lx (caller %08lx)", fn ? fn[0] : 0, hr, handle ? *handle : 0,
            Rva(_ReturnAddress()));
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetPixelShader(void* self, DWORD handle)
{
    if (cfg.trace && handle && Budget("bt:SetPixelShader", handle, 2)) Log("battle SetPixelShader %lx", handle);
    return realSetPixelShader(self, handle);
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(void* self, UINT adapter, DWORD type, HWND focus, DWORD flags, void* pp, void** dev)
{
    Log("battle CreateDevice adapter %u type %lu behaviour flags %08lx", adapter, type, flags);
    FixParams(pp, "device");
    HRESULT hr = realCreateDevice(self, adapter, type, focus, flags, pp, dev);
    if (SUCCEEDED(hr) && dev && *dev)
    {
        void** vt = *(void***)*dev;
        Patch(&vt[14], (void*)HookReset, (void**)&realReset);
        Patch(&vt[15], (void*)HookPresent, (void**)&realPresent);
        Patch(&vt[20], (void*)HookCreateTexture, (void**)&realCreateTexture);
        Patch(&vt[21], (void*)HookCreateVolumeTexture, (void**)&realCreateVolumeTexture);
        Patch(&vt[22], (void*)HookCreateCubeTexture, (void**)&realCreateCubeTexture);
        Patch(&vt[23], (void*)HookCreateVertexBuffer, (void**)&realCreateVertexBuffer);
        Patch(&vt[25], (void*)HookCreateRenderTarget, (void**)&realCreateRenderTarget);
        Patch(&vt[26], (void*)HookCreateDepthStencilSurface, (void**)&realCreateDepthStencilSurface);
        Patch(&vt[28], (void*)HookCopyRects, (void**)&realCopyRects);
        Patch(&vt[30], (void*)HookGetFrontBuffer, (void**)&realGetFrontBuffer);
        Patch(&vt[31], (void*)HookSetRenderTarget, (void**)&realSetRenderTarget);
        Patch(&vt[36], (void*)HookClear, (void**)&realClear);
        Patch(&vt[40], (void*)HookSetViewport, (void**)&realSetViewport);
        Patch(&vt[41], (void*)HookGetViewport, (void**)&realGetViewport);
        Patch(&vt[63], (void*)HookSetTextureStageState, (void**)&realSetTextureStageState);
        Patch(&vt[70], (void*)HookDrawPrimitive, (void**)&realDrawPrimitive);
        Patch(&vt[71], (void*)HookDrawIndexedPrimitive, (void**)&realDrawIndexedPrimitive);
        Patch(&vt[72], (void*)HookDrawPrimitiveUP, (void**)&realDrawPrimitiveUP);
        Patch(&vt[73], (void*)HookDrawIndexedPrimitiveUP, (void**)&realDrawIndexedPrimitiveUP);
        getVertexShader = (GetVertexShader_t)vt[77];
        getTextureStageState = (GetTextureStageState_t)vt[62];
        devPtr = *dev;
        Patch(&vt[83], (void*)HookSetStreamSource, (void**)&realSetStreamSource);
        Patch(&vt[16], (void*)HookGetBackBuffer, (void**)&realGetBackBuffer);
        Patch(&vt[27], (void*)HookCreateImageSurface, (void**)&realCreateImageSurface);
        Patch(&vt[29], (void*)HookUpdateTexture, (void**)&realUpdateTexture);
        Patch(&vt[32], (void*)HookGetRenderTarget, (void**)&realGetRenderTarget);
        Patch(&vt[37], (void*)HookSetTransform, (void**)&realSetTransform);
        Patch(&vt[50], (void*)HookSetRenderState, (void**)&realSetRenderState);
        Patch(&vt[61], (void*)HookSetTexture, (void**)&realSetTexture);
        Patch(&vt[7], (void*)HookDevGetDeviceCaps, (void**)&realDevGetDeviceCaps);
        Patch(&vt[75], (void*)HookCreateVertexShader, (void**)&realCreateVertexShader);
        Patch(&vt[87], (void*)HookCreatePixelShader, (void**)&realCreatePixelShader);
        Patch(&vt[88], (void*)HookSetPixelShader, (void**)&realSetPixelShader);
        FindBackBuffer(*dev);
        frames = 0;
        texMade = texBig = 0;
        texPixels = 0;
        fightAt = 0;
        Log("battle device made: back buffer surface %p, %s", bbSurf, native ? "native resolution" : "800x600 scaled");
    }
    else Log("battle device creation failed: %08lx", hr);
    return hr;
}

static void* WINAPI HookDirect3DCreate8(UINT sdk)
{
    void* d3d = realDirect3DCreate8(sdk);
    if (d3d)
    {
        void** vt = *(void***)d3d;                               // IDirect3D8: GetDeviceCaps 13, CreateDevice 15
        Patch(&vt[15], (void*)HookCreateDevice, (void**)&realCreateDevice);
        Patch(&vt[13], (void*)HookD3DGetDeviceCaps, (void**)&realD3DGetDeviceCaps);
    }
    return d3d;
}

// The module sizes its 2D from the main window's client (demonbane+0xc516 keeps it for the layout, e.g. the full-screen
// dimming quad); in our big window that is 4096x2160 instead of the 800x600 it was made for. It gets 800x600, like
// system.dll does (display.cpp).
typedef BOOL(WINAPI* GetClientRect_t)(HWND, LPRECT);
static GetClientRect_t modGetClientRect;

static BOOL WINAPI HookModGetClientRect(HWND h, LPRECT r)
{
    if (h && r && h == UhdMainWindow()) { SetRect(r, 0, 0, 800, 600); return TRUE; }
    return modGetClientRect(h, r);
}

// At the start of a fight the module makes its handler (demonbane+0x12C0) the window procedure (+0x20E0) - the copy's
// dinput8.dll then puts its keep-active procedure in front of that - and at the end it puts the host's procedure back
// (+0x1050, SetWindowLongA with the engine's own). That last one would drop our procedure (display.cpp): the restore
// sets ours instead, which hands everything on to the engine's. The fight itself is left alone.
typedef LONG(WINAPI* SetWindowLongA_t)(HWND, int, LONG);
static SetWindowLongA_t modSetWindowLongA;
static DWORD battleBase, battleEnd;        // the module's image

// 11-09: another module's subclass in front of ours - the LORE library's URL bridge (KHDLore.dll gameProc, whose next is ours) - was
// dropped by that restore too (the engine's own procedure comes back, ours goes in front of it): after any fight 「LORE」 in the extra
// menu fell through to the engine's url command (ShellExecuteEx http://khd-lore.invalid/library) instead of opening the library.
// So the head the window had when the fight began goes back in front at its end - unless it was ours, the battle module's or the
// copy's dinput8.dll (its keep-active procedure belongs to the fight). With nothing else in front of ours this changes nothing.
extern "C" IMAGE_DOS_HEADER __ImageBase;
static WNDPROC preFightHead;
enum ProcOwner { PROC_NONE, PROC_OURS, PROC_FIGHT, PROC_OTHER };     // PROC_FIGHT: the battle module's or the copy's dinput8.dll
static ProcOwner OwnerOf(WNDPROC p)
{
    DWORD a = (DWORD)(UINT_PTR)p;
    if (!p) return PROC_NONE;
    if (a >= battleBase && a < battleEnd) return PROC_FIGHT;
    HMODULE owner = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &owner) || !owner)
        return PROC_NONE;
    if (owner == (HMODULE)&__ImageBase) return PROC_OURS;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(owner, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"dinput8.dll") == 0 ? PROC_FIGHT : PROC_OTHER;
}

static LONG WINAPI HookModSetWindowLongA(HWND h, int index, LONG value)
{
    DWORD v = (DWORD)value;
    if (index == GWL_WNDPROC && h && h == UhdMainWindow() && v && v >= battleBase && v < battleEnd)
    {
        WNDPROC head = (WNDPROC)GetWindowLongPtrA(h, GWLP_WNDPROC);    // the fight begins: who is in front now
        ProcOwner o = OwnerOf(head);
        if (o == PROC_OTHER) preFightHead = head;
        else if (o == PROC_OURS || o == PROC_NONE) preFightHead = NULL;
        // PROC_FIGHT: a second call while the fight starts - what was in front before it stays recorded
    }
    else if (index == GWL_WNDPROC && h && h == UhdMainWindow() && v)
    {
        LONG front = (LONG)(UINT_PTR)UhdFrontProc((WNDPROC)(UINT_PTR)v);
        WNDPROC keep = preFightHead;
        preFightHead = NULL;
        if (keep && keep != (WNDPROC)(UINT_PTR)v && OwnerOf(keep) == PROC_OTHER)
        {
            Log("fight over: the procedure in front before the fight (%p, another module's) goes back in front of ours", keep);
            front = (LONG)(UINT_PTR)keep;
        }
        else if (front != value) Log("fight over: the engine's window procedure goes back behind ours (mouse stays mapped)");
        value = front;
    }
    return modSetWindowLongA(h, index, value);
}

static void HookBattleModule(HMODULE m)
{
    IMAGE_NT_HEADERS* mh = (IMAGE_NT_HEADERS*)((BYTE*)m + ((IMAGE_DOS_HEADER*)m)->e_lfanew);
    battleBase = (DWORD)(UINT_PTR)m;
    battleEnd = battleBase + mh->OptionalHeader.SizeOfImage;
    if (HookModuleImport(m, "USER32.dll", "GetClientRect", (void*)HookModGetClientRect, (void**)&modGetClientRect))
        Log("battle module: GetClientRect answers 800x600 for the main window");
    if (HookModuleImport(m, "USER32.dll", "SetWindowLongA", (void*)HookModSetWindowLongA, (void**)&modSetWindowLongA))
        Log("battle module: SetWindowLongA watched (our window procedure stays in front after the fight)");
    if (cfg.offscreen) UhdQuietWindows(m);
    BYTE* b = (BYTE*)m;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(b + d.VirtualAddress); d.VirtualAddress && imp->Name; imp++)
    {
        if (_stricmp((char*)(b + imp->Name), "d3d8.dll") != 0 || !imp->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*)(b + imp->OriginalFirstThunk);
        void** slots = (void**)(b + imp->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; i++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            if (strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(b + names[i].u1.AddressOfData))->Name, "Direct3DCreate8") == 0)
            {
                if (Patch(&slots[i], (void*)HookDirect3DCreate8, (void**)&realDirect3DCreate8)) Log("battle module: Direct3DCreate8 hooked");
                return;
            }
        }
    }
    Log("battle module: no d3d8 import found");
}

static HMODULE WINAPI HookLoadLibraryA(LPCSTR name)
{
    HMODULE m = nextLoadLibraryA(name);
    if (m && name)
    {
        const char* file = strrchr(name, '\\');
        file = file ? file + 1 : name;
        if (_stricmp(file, "demonbane.dll") == 0) HookBattleModule(m);
    }
    return m;
}

void InstallBattle()
{
    HookImport("KERNEL32.dll", "LoadLibraryA", (void*)HookLoadLibraryA, (void**)&nextLoadLibraryA);   // after the proxy's own
    Log("battles: %s", cfg.battle ? "drawn at the image rectangle's resolution (native)" : "800x600 scaled into the image rectangle (stage A)");
}
