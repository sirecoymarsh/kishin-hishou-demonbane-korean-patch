// khd4k.dll stage B (uhd=1, uhd_text=1): the dialogue at the output resolution, checked against the engine's frame.
//
// The engine composes its 800x600 frame in software (system.dll, base 0x10000000):
//   0x5ca10 DrawChar(layer, hdc, x, y, pChar, color)          stdcall, ret 0x18 - stamps one glyph into the surface
//           layer+0xB4 (W): colour where the glyph mask is not 0, alpha = mask (overwrite). Shadow 010101 at +2,+2 first.
//   0x52480 CopyRect  ecx = flag, eax = first row, edx = rows; stack (src, dst, x, width), ret 0x10 - W -> D (layer+0x160)
//   0x03a90 FillRect  eax = rect object (RECT at +4) or 0 = all; stack (surface, color), ret 8
//   0x56450 DrawLayer ecx = image (+4 colour surface, +8 alpha surface or 0, +0x3C opacity 0..100, +0x40 key, -2 = none);
//           stack (dst, dx, dy, sx, sy, ax, ay, w, h), ret 0x24 - every picture that reaches the presented frame
// A surface: +4 BITMAPINFOHEADER*, +0xC bits (bottom-up), +0x4C stride; a surface with alpha has its 8-bit alpha surface
// embedded at +0x5C.
//
// Output scale K = P/5: every 5x5 block of the 800x600 frame becomes P x P output pixels (P = 18 for a 2160-line
// monitor -> 2880x2160), so the result goes to the window 1:1. We keep:
//   * planes at K (colour + alpha) for the dialogue surfaces W and D, stamped with the engine's font at 24*K px;
//   * FO = the presented frame at K, S1 = a shadow of the presented frame at 1x that only our mirrored draws touch
//     (each DrawLayer is replayed into S1 by the engine's own code);
//   * pending: blocks whose S1 changed by a picture draw - FO gets them by scaling S1 up, lazily (before a dialogue draw
//     over them, and at present).
// A DrawLayer whose source has a plane (the dialogue) is blended into FO at K. At present every block where the
// engine's frame differs from S1 was written by something we do not mirror: it is rebuilt from the engine's frame.
#include "common.h"

static const int FW = 800, FH = 600, B1 = 5, BX = FW / B1, BY = FH / B1;
static int P = 0;                                  // output pixels per 5-pixel block
static int OW, OH;                                 // output frame size = 160P x 120P
static inline int Sc(int v) { return v * P / B1; } // 1x coordinate -> output coordinate (floor for v >= 0)

// ---------------------------------------------------------------- engine surfaces
struct View1 { BYTE* bits; int w, h, stride, bpp; };

static bool View(DWORD s, View1* v)
{
    __try
    {
        if (s < 0x10000) return false;
        BITMAPINFOHEADER* bi = *(BITMAPINFOHEADER**)(UINT_PTR)(s + 4);
        BYTE* bits = *(BYTE**)(UINT_PTR)(s + 0xC);
        if ((UINT_PTR)bi < 0x10000 || bi->biSize != 40 || !bits) return false;
        v->bits = bits;
        v->w = bi->biWidth;
        v->h = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
        v->stride = *(int*)(UINT_PTR)(s + 0x4C);
        v->bpp = bi->biBitCount;
        return v->w > 0 && v->h > 0 && v->stride > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------- planes at K for the dialogue surfaces
struct Plane { DWORD surf; int w, h, pw, ph; BYTE* c; BYTE* a; };   // c: BGR, rows top-down, stride 3*pw; a: pw per row
static Plane planes[24];
static int nPlanes;
static CRITICAL_SECTION lk;

static Plane* FindPlane(DWORD surf)
{
    for (int i = 0; i < nPlanes; i++) if (planes[i].surf == surf) return &planes[i];
    return NULL;
}

static Plane* FindPlaneByAlpha(DWORD alphaSurf)
{
    for (int i = 0; i < nPlanes; i++) if (planes[i].surf + 0x5C == alphaSurf) return &planes[i];
    return NULL;
}

static Plane* MakePlane(DWORD surf)
{
    Plane* p = FindPlane(surf);
    View1 v;
    if (!View(surf, &v)) return NULL;
    if (p && p->w == v.w && p->h == v.h) return p;
    if (!p)
    {
        if (nPlanes >= 24) return NULL;
        p = &planes[nPlanes++];
    }
    else
    {
        VirtualFree(p->c, 0, MEM_RELEASE);
        VirtualFree(p->a, 0, MEM_RELEASE);
    }
    p->surf = surf;
    p->w = v.w;
    p->h = v.h;
    p->pw = Sc(v.w) + 1;
    p->ph = Sc(v.h) + 1;
    p->c = (BYTE*)VirtualAlloc(NULL, (SIZE_T)3 * p->pw * p->ph, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    p->a = (BYTE*)VirtualAlloc(NULL, (SIZE_T)p->pw * p->ph, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Log("uhd: plane for surface %08lx (%dx%d -> %dx%d)", surf, v.w, v.h, p->pw, p->ph);
    return p;
}

// ---------------------------------------------------------------- a glyph at K, the way 0x5da10 renders it at 4x
static HDC gdc;
static BYTE* gbits;
static const int GW = 256, GH = 256;
static LOGFONTA lastLf;
static HFONT gfont;

static bool RenderGlyph(HDC engineDc, const BYTE* ch, int* outW, int* outH)
{
    LOGFONTA lf = {};
    if (!GetObjectA(GetCurrentObject(engineDc, OBJ_FONT), sizeof lf, &lf)) return false;
    LONG weight = lf.lfWeight;
    lf.lfHeight = (lf.lfHeight * P - (lf.lfHeight < 0 ? 2 : -2)) / B1;     // rounded
    lf.lfWidth = (lf.lfWidth * P + 2) / B1;
    lf.lfWeight = FW_NORMAL;                  // the engine draws normal weight and thickens by offset passes
    lf.lfQuality = ANTIALIASED_QUALITY;
    if (!gfont || memcmp(&lf, &lastLf, sizeof lf) != 0)
    {
        if (gfont) DeleteObject(gfont);
        gfont = CreateFontIndirectA(&lf);
        lastLf = lf;
        SelectObject(gdc, gfont);
    }
    int len = IsDBCSLeadByteEx(932, ch[0]) && ch[1] ? 2 : 1;
    SIZE sz = {};
    GetTextExtentPoint32A(gdc, (LPCSTR)ch, len, &sz);
    int w = sz.cx + 2, h = sz.cy;
    if (w <= 0 || h <= 0) return false;
    if (w > GW) w = GW;
    if (h > GH) h = GH;
    memset(gbits, 0, GW * GH * 4);
    SetBkMode(gdc, TRANSPARENT);
    SetTextColor(gdc, RGB(255, 255, 255));
    TextOutA(gdc, 0, 0, (LPCSTR)ch, len);
    if (weight >= 500) TextOutA(gdc, 1, 0, (LPCSTR)ch, len);
    if (weight >= 600) TextOutA(gdc, 0, 1, (LPCSTR)ch, len);
    if (weight >= 700) TextOutA(gdc, 1, 1, (LPCSTR)ch, len);
    GdiFlush();
    *outW = w;
    *outH = h;
    return true;
}

static void StampGlyph(Plane* p, HDC engineDc, int x, int y, const BYTE* ch, DWORD color)
{
    int w, h;
    if (!RenderGlyph(engineDc, ch, &w, &h)) return;
    BYTE b = (BYTE)(color >> 16), g = (BYTE)(color >> 8), r = (BYTE)color;
    int X0 = Sc(x), Y0 = Sc(y);
    for (int gy = 0; gy < h; gy++)
    {
        int Y = Y0 + gy;
        if (Y < 0 || Y >= p->ph) continue;
        const BYTE* src = gbits + gy * GW * 4;                    // top-down 32-bit DIB
        BYTE* dc = p->c + (SIZE_T)Y * 3 * p->pw;
        BYTE* da = p->a + (SIZE_T)Y * p->pw;
        for (int gx = 0; gx < w; gx++)
        {
            int X = X0 + gx;
            if (X < 0 || X >= p->pw) continue;
            BYTE cov = src[gx * 4 + 1];
            if (!cov) continue;
            dc[X * 3] = b;
            dc[X * 3 + 1] = g;
            dc[X * 3 + 2] = r;
            da[X] = cov;
        }
    }
}

// ---------------------------------------------------------------- the presented frame at K and its 1x shadow
static DWORD frameSurf;
static BYTE* FO;                        // BGR, top-down, OW x OH (a DIB section, so it can be blitted)
static HDC fodc;
static BYTE* S1;                        // the shadow: same layout as the engine's frame DIB
static BYTE fakeDst[0x100];             // the engine's frame surface object with its bits pointing at S1
static int f1Stride;
static bool pend[BY][BX];               // FO block still to be scaled up from S1
static bool oldOn[BY][BX];              // stage C: the block is in a transition (its start kept in OLD1 / OLDK)
static BYTE lastW[BY][BX];              // test (uhd_check): what last wrote the FO block - 1 scaled up, 2 adopted, 3 transition,
                                        // 4 adopted late, 5 text at K
static BYTE blkSrc[BY][BX];             // stage C: where the FO block came from - 0 scaled up from S1, 1 a twin's C (4x),
                                        // 2 scaled up and a later adoption failed too
static volatile LONG foDirty = 2;       // 2 = all of FO to the window (a repaint), 1 = the dirty blocks below
static int dX0 = BX, dY0 = BY, dX1 = -1, dY1 = -1;   // FO blocks written since the last present (under lk)

static inline void DirtyBlock(int bx, int by)
{
    if (bx < dX0) dX0 = bx;
    if (by < dY0) dY0 = by;
    if (bx > dX1) dX1 = bx;
    if (by > dY1) dY1 = by;
    InterlockedCompareExchange(&foDirty, 1, 0);
}

static inline void DirtyAll() { InterlockedExchange(&foDirty, 2); }
static int iTab[64], wTab[64];          // per output offset in a block: source pixel (relative, -1..5) and weight /256

static void BuildTables()
{
    for (int u = 0; u < P; u++)
    {
        int s = ((2 * u + 1) * B1 * 128) / P - 128;                 // ((u + 0.5) * 5/P - 0.5) * 256
        int i = s >> 8;                                             // floor (arithmetic shift)
        iTab[u] = i;
        wTab[u] = s - (i << 8);
    }
}

static inline const BYTE* Row1(const BYTE* f, int y) { return f + (FH - 1 - (y < 0 ? 0 : (y >= FH ? FH - 1 : y))) * f1Stride; }

// one 5x5 block of a 1x frame (engine layout) scaled up into an output-size BGR picture (FO or a stage C twin;
// bilinear, edge clamped)
static void UpBlockInto(BYTE* out, const BYTE* f, int bx, int by)
{
    int x0 = bx * B1, y0 = by * B1;
    for (int v = 0; v < P; v++)
    {
        int sy = y0 + iTab[v], wy = wTab[v];
        const BYTE* r0 = Row1(f, sy);
        const BYTE* r1 = Row1(f, sy + 1);
        BYTE* d = out + ((SIZE_T)(by * P + v) * OW + bx * P) * 3;
        for (int u = 0; u < P; u++, d += 3)
        {
            int sx = x0 + iTab[u], wx = wTab[u];
            int xa = sx < 0 ? 0 : (sx >= FW ? FW - 1 : sx), xb = sx + 1 >= FW ? FW - 1 : (sx + 1 < 0 ? 0 : sx + 1);
            const BYTE *a0 = r0 + xa * 3, *b0 = r0 + xb * 3, *a1 = r1 + xa * 3, *b1 = r1 + xb * 3;
            for (int c = 0; c < 3; c++)
            {
                int t = a0[c] * (256 - wx) + b0[c] * wx;
                int q = a1[c] * (256 - wx) + b1[c] * wx;
                d[c] = (BYTE)((t * (256 - wy) + q * wy + 32768) >> 16);
            }
        }
    }
}

static void UpBlock(const BYTE* f, int bx, int by) { UpBlockInto(FO, f, bx, by); }

// ---------------------------------------------------------------- stage C: twins of the surfaces pictures are composed in
// The engine composes a scene in an 800x600 surface (backgrounds, CGs, effects, the text window and its text) and moves
// it to the presented frame by other code (transitions); it also redraws the text box region into both. A twin keeps,
// for such a surface: E = what our followed draws left at 1x (kept equal to the engine's pixels - blocks where they
// differ are taken from the engine and scaled up), C = the same at K. Draws from a 4x picture go into C from the
// picture at K. When a block of the presented frame has to be made at K and it equals the twin's block at 1x, it is
// taken from C (AdoptBlock) - so a scene shows at 4x once a transition has finished, without following the transition.
struct Twin
{
    BYTE* bits;                   // the engine surface's pixels (its identity - surface objects can be temporary copies)
    int stride;
    BYTE* E;                      // 1x, the engine's layout (bottom-up)
    BYTE fake[0x100];             // a surface object whose bits are E, to replay the engine's own draw into it
    BYTE* C;                      // K, BGR top-down OW x OH
    bool pendT[BY][BX];           // C block still to be scaled up from E
    BYTE hdT[BY][BX];             // C block has a 4x picture or K text in it since it was last scaled up (test map)
    DWORD used;
};
static Twin twins[3];
static int nTwins;
static LONG twinDraws, twinHd, twinPlane, twinUp, adopted, adoptMiss, adoptLate;
static double msTwin, msTwinMax;

static Twin* FindTwin(BYTE* bits)
{
    for (int i = 0; i < nTwins; i++) if (twins[i].bits == bits) return &twins[i];
    return NULL;
}

static Twin* MakeTwin(DWORD surf, const View1& v)
{
    if (v.w != FW || v.h != FH || v.bpp != 24 || v.stride < FW * 3 || !OW) return NULL;
    Twin* t = NULL;
    if (nTwins < 3) t = &twins[nTwins++];
    else
    {
        t = &twins[0];
        for (int i = 1; i < 3; i++) if (twins[i].used < t->used) t = &twins[i];
        Log("uhd: twin of %p dropped for %p", t->bits, v.bits);
    }
    if (!t->E) t->E = (BYTE*)VirtualAlloc(NULL, (SIZE_T)v.stride * FH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!t->C) t->C = (BYTE*)VirtualAlloc(NULL, (SIZE_T)OW * OH * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!t->E || !t->C) { Log("uhd: no memory for a twin"); return NULL; }
    t->bits = v.bits;
    t->stride = v.stride;
    memcpy(t->E, v.bits, (SIZE_T)v.stride * FH);
    for (int by = 0; by < BY; by++) for (int bx = 0; bx < BX; bx++) t->pendT[by][bx] = true;
    memset(t->hdT, 0, sizeof t->hdT);
    memcpy(t->fake, (void*)(UINT_PTR)surf, sizeof t->fake);
    *(BYTE**)(t->fake + 0xC) = t->E;
    t->used = GetTickCount();
    Log("uhd: twin for surface %08lx (bits %p) at %dx%d", surf, v.bits, OW, OH);
    return t;
}

static void BlockRange(int x, int y, int w, int h, int* bx0, int* by0, int* bx1, int* by1)
{
    *bx0 = max(0, x / B1);
    *by0 = max(0, y / B1);
    *bx1 = min(BX - 1, (x + w - 1) / B1);
    *by1 = min(BY - 1, (y + h - 1) / B1);
}

static inline bool BlockSame(const BYTE* a, const BYTE* b, int stride, int bx, int by)
{
    for (int r = 0; r < B1; r++)
    {
        SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * stride + bx * B1 * 3;
        if (memcmp(a + o, b + o, B1 * 3) != 0) return false;
    }
    return true;
}

// E := the engine's pixels where they differ (written by code we do not follow); those C blocks go back to scaling up
static void TwinSync(Twin* t, int bx0, int by0, int bx1, int by1)
{
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++)
            if (!BlockSame(t->E, t->bits, t->stride, bx, by))
            {
                for (int r = 0; r < B1; r++)
                {
                    SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * t->stride + bx * B1 * 3;
                    memcpy(t->E + o, t->bits + o, B1 * 3);
                }
                t->pendT[by][bx] = true;
            }
}

static void TwinFlush(Twin* t, int bx0, int by0, int bx1, int by1)
{
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++)
            if (t->pendT[by][bx])
            {
                UpBlockInto(t->C, t->E, bx, by);
                t->pendT[by][bx] = false;
                t->hdT[by][bx] = 0;
                InterlockedIncrement(&twinUp);
            }
}

// a block of the presented frame (S1 = the engine's frame there) to be made at K: from a twin that holds the same at 1x
static bool adoptHd;                    // the last adopted block had 4x / K text in it
static bool AdoptBlock(int bx, int by)
{
    if (!nTwins || !S1) return false;
    for (int i = 0; i < nTwins; i++)
    {
        Twin* t = &twins[i];
        if (!t->bits || t->stride != f1Stride) continue;
        __try
        {
            TwinSync(t, bx, by, bx, by);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { t->bits = NULL; continue; }   // the surface went away
        if (!BlockSame(S1, t->E, f1Stride, bx, by)) continue;
        TwinFlush(t, bx, by, bx, by);
        for (int v = 0; v < P; v++)
        {
            SIZE_T o = ((SIZE_T)(by * P + v) * OW + bx * P) * 3;
            memcpy(FO + o, t->C + o, (SIZE_T)P * 3);
        }
        adoptHd = t->hdT[by][bx] != 0;
        adopted++;
        return true;
    }
    adoptMiss++;
    return false;
}

// 11-18: a twin holds the 4x in this block now (not waiting to be scaled up) - worth taking it again (no pixel checks here,
// AdoptBlock does those)
static LONG adoptStale;
static bool TwinHdReady(int bx, int by)
{
    for (int i = 0; i < nTwins; i++)
    {
        const Twin* t = &twins[i];
        if (t->bits && t->stride == f1Stride && t->hdT[by][bx] && !t->pendT[by][bx]) return true;
    }
    return false;
}

static void MarkPending(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int bx0 = x / B1, by0 = y / B1, bx1 = (x + w - 1) / B1, by1 = (y + h - 1) / B1;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= BX) bx1 = BX - 1;
    if (by1 >= BY) by1 = BY - 1;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++) { pend[by][bx] = true; oldOn[by][bx] = false; }
}

// scale up pending blocks (in a 1x rectangle, or all) from S1; one ring more, since bilinear reads neighbours
static void Flush(int x, int y, int w, int h)
{
    int bx0 = (x - 1) / B1, by0 = (y - 1) / B1, bx1 = (x + w) / B1, by1 = (y + h) / B1;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= BX) bx1 = BX - 1;
    if (by1 >= BY) by1 = BY - 1;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++)
            if (pend[by][bx])
            {
                if (cfg.stagec && AdoptBlock(bx, by)) { blkSrc[by][bx] = adoptHd ? 1 : 3; lastW[by][bx] = 2; }
                else { UpBlock(S1, bx, by); blkSrc[by][bx] = 0; lastW[by][bx] = 1; }
                pend[by][bx] = false;
                DirtyBlock(bx, by);
            }
}

static bool MakeFO()
{
    if (FO) return true;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = OW;
    bi.bmiHeader.biHeight = -OH;                                    // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { Log("uhd: no memory for the output frame"); return false; }
    FO = (BYTE*)bits;
    fodc = CreateCompatibleDC(NULL);
    SelectObject(fodc, bmp);
    BuildTables();
    return true;
}

// stage C: the presented frame known only by its pixels (title screens and scenes come by transitions, with no draw
// into the frame yet) - FO and S1 start from them; the first draw into the frame does StartFrame as before
static BYTE* frameBits0;
static bool StartFrameBits(BYTE* f)
{
    if (frameH != FH || frameStride < FW * 3 || !MakeFO()) return false;
    f1Stride = frameStride;
    if (S1) VirtualFree(S1, 0, MEM_RELEASE);
    S1 = (BYTE*)VirtualAlloc(NULL, (SIZE_T)f1Stride * FH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!S1) return false;
    memcpy(S1, f, (SIZE_T)f1Stride * FH);
    MarkPending(0, 0, FW, FH);
    frameSurf = 0;
    frameBits0 = f;
    DirtyAll();
    Log("uhd: presented frame known by its bits %p (stride %d); output frame %dx%d", f, f1Stride, OW, OH);
    return true;
}

static bool StartFrame(DWORD dst)
{
    View1 v;
    if (!View(dst, &v) || v.w != FW || v.h != FH || v.bpp != 24 || P < B1) return false;
    if (!FO)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = OW;
        bi.bmiHeader.biHeight = -OH;                                // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        void* bits = NULL;
        HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!bmp) { Log("uhd: no memory for the output frame"); return false; }
        FO = (BYTE*)bits;
        fodc = CreateCompatibleDC(NULL);
        SelectObject(fodc, bmp);
        BuildTables();
    }
    f1Stride = v.stride;
    if (S1) VirtualFree(S1, 0, MEM_RELEASE);
    S1 = (BYTE*)VirtualAlloc(NULL, (SIZE_T)v.stride * FH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memcpy(S1, v.bits, (SIZE_T)v.stride * FH);
    MarkPending(0, 0, FW, FH);
    memcpy(fakeDst, (void*)(UINT_PTR)dst, sizeof fakeDst);
    *(BYTE**)(fakeDst + 0xC) = S1;
    frameSurf = dst;
    DirtyAll();
    Log("uhd: presented frame is surface %08lx (bits %p, stride %d); output frame %dx%d (%d px per 5-px block)", dst, v.bits,
        v.stride, OW, OH, P);
    return true;
}

// ---------------------------------------------------------------- a dialogue DrawLayer at K
static BYTE OpacityByte(int idx) { return *((BYTE*)sys + 0xCE4C0 + idx * 4); }

// returns true when this draw can be mirrored (the caller then replays it into S1)
static bool Mirror(DWORD img, const DWORD* a)
{
    DWORD sc = *(DWORD*)(UINT_PTR)(img + 4), sa = *(DWORD*)(UINT_PTR)(img + 8);
    int opIdx = *(int*)(UINT_PTR)(img + 0x3C), key = *(int*)(UINT_PTR)(img + 0x40);
    int dx = (int)a[1], dy = (int)a[2], sx = (int)a[3], sy = (int)a[4], w = (int)a[7], h = (int)a[8];
    Plane* pl = FindPlane(sc);
    if (!pl)                                         // a picture: the engine draws it into S1, FO scales it up later
    {
        MarkPending(dx, dy, w, h);
        return true;
    }
    // key -1 = the source's top-left colour is transparent (0x54f40): for a text plane that is where alpha is 0 anyway
    // (the engine draws shadows 010101 so they are never that colour), so it is the same blend (11-07)
    if ((key != -2 && key != -1) || opIdx < 0 || opIdx > 100 || (int)a[5] != sx || (int)a[6] != sy || !sa) return false;
    BYTE op = OpacityByte(opIdx);
    // clip like the engine: to the frame and to the source image
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (sx + w > pl->w) w = pl->w - sx;
    if (sy + h > pl->h) h = pl->h - sy;
    if (dx + w > FW) w = FW - dx;
    if (dy + h > FH) h = FH - dy;
    if (w <= 0 || h <= 0 || !op) return true;
    Flush(dx, dy, w, h);                             // the pictures under the text first
    {
        int bx0, by0, bx1, by1;
        BlockRange(dx, dy, w, h, &bx0, &by0, &bx1, &by1);
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++) { oldOn[by][bx] = false; lastW[by][bx] = 5; }
    }
    int X0 = Sc(dx), Y0 = Sc(dy), X1 = Sc(dx + w), Y1 = Sc(dy + h), PX = Sc(sx), PY = Sc(sy);
    for (int Y = Y0; Y < Y1; Y++)
    {
        int py = PY + (Y - Y0);
        if (py >= pl->ph) py = pl->ph - 1;
        const BYTE* sc3 = pl->c + (SIZE_T)py * 3 * pl->pw;
        const BYTE* sa1 = pl->a + (SIZE_T)py * pl->pw;
        BYTE* d = FO + ((SIZE_T)Y * OW + X0) * 3;
        for (int X = X0; X < X1; X++, d += 3)
        {
            int px = PX + (X - X0);
            if (px >= pl->pw) px = pl->pw - 1;
            int t = (sa1[px] * op) >> 8;
            if (t <= 0) continue;
            const BYTE* s = sc3 + px * 3;
            for (int c = 0; c < 3; c++) d[c] = (BYTE)(((s[c] - d[c]) * t + (d[c] << 8)) >> 8);
        }
    }
    {
        int bx0, by0, bx1, by1;
        BlockRange(dx, dy, w, h, &bx0, &by0, &bx1, &by1);
        DirtyBlock(bx0, by0);
        DirtyBlock(bx1, by1);
    }
    return true;
}

// ---------------------------------------------------------------- hooks
static BYTE *trDrawChar, *trCopy, *trFill, *trLayer;
static double msDraw, msDrawMax, msPresent, msPresentMax;
static LONG nDraw, nPresent, nFrames, framesWithRebuild, blocksRebuilt;

static double Ms()
{
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static void __stdcall PreDrawChar(DWORD* a)
{
    if (P < B1) return;
    EnterCriticalSection(&lk);
    Plane* p = MakePlane(a[0] + 0xB4);
    if (p && a[4]) StampGlyph(p, (HDC)(UINT_PTR)a[1], (int)a[2], (int)a[3], (const BYTE*)(UINT_PTR)a[4], a[5]);
    LeaveCriticalSection(&lk);
}

__declspec(naked) static void HookDrawChar()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        call PreDrawChar
        popad
        jmp dword ptr [trDrawChar]
    }
}

static void __stdcall PreCopy(DWORD row0, DWORD flag, DWORD rows, DWORD* a)
{
    if (P < B1) return;
    EnterCriticalSection(&lk);
    Plane* s = FindPlane(a[0]);
    if (s)
    {
        Plane* d = MakePlane(a[1]);
        int x = (int)a[2], w = (int)a[3], y = (int)row0, n = (int)rows;
        if (d && flag == 0 && d->w == s->w && d->h == s->h)
        {
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { n += y; y = 0; }
            if (x + w > s->w) w = s->w - x;
            if (y + n > s->h) n = s->h - y;
            int X0 = Sc(x), X1 = Sc(x + w), Y0 = Sc(y), Y1 = Sc(y + n);
            for (int Y = Y0; Y < Y1 && X1 > X0; Y++)
            {
                SIZE_T o = (SIZE_T)Y * s->pw + X0;
                memcpy(d->c + o * 3, s->c + o * 3, (SIZE_T)(X1 - X0) * 3);
                memcpy(d->a + o, s->a + o, (SIZE_T)(X1 - X0));
            }
        }
        else if (d) memset(d->a, 0, (SIZE_T)d->pw * d->ph);         // an unknown kind of copy: show nothing at K
    }
    LeaveCriticalSection(&lk);
}

__declspec(naked) static void HookCopy()
{
    __asm {
        pushad
        lea ebx, [esp + 36]
        push ebx
        push edx
        push ecx
        push eax
        call PreCopy
        popad
        jmp dword ptr [trCopy]
    }
}

static void __stdcall PreFill(DWORD rectObj, DWORD* a)
{
    if (P < B1) return;
    EnterCriticalSection(&lk);
    Plane* p = FindPlane(a[0]);
    bool alpha = false;
    if (!p) { p = FindPlaneByAlpha(a[0]); alpha = p != NULL; }
    if (p)
    {
        int x0 = 0, y0 = 0, x1 = p->w, y1 = p->h;
        if (rectObj)
        {
            RECT* r = (RECT*)(UINT_PTR)(rectObj + 4);
            x0 = max(0, (int)r->left); y0 = max(0, (int)r->top); x1 = min(p->w, (int)r->right); y1 = min(p->h, (int)r->bottom);
        }
        DWORD color = a[1];
        int X0 = Sc(x0), X1 = x1 >= p->w ? p->pw : Sc(x1), Y0 = Sc(y0), Y1 = y1 >= p->h ? p->ph : Sc(y1);
        for (int Y = Y0; Y < Y1; Y++)
        {
            SIZE_T o = (SIZE_T)Y * p->pw + X0;
            if (alpha) memset(p->a + o, (BYTE)color, (SIZE_T)(X1 - X0));
            else
                for (int X = 0; X < X1 - X0; X++)
                {
                    p->c[(o + X) * 3] = (BYTE)(color >> 16);
                    p->c[(o + X) * 3 + 1] = (BYTE)(color >> 8);
                    p->c[(o + X) * 3 + 2] = (BYTE)color;
                }
        }
    }
    LeaveCriticalSection(&lk);
}

__declspec(naked) static void HookFill()
{
    __asm {
        pushad
        lea ebx, [esp + 36]
        push ebx
        push eax
        call PreFill
        popad
        jmp dword ptr [trFill]
    }
}

// calls the engine's own DrawLayer (the trampoline) with the given image and 9 arguments
#pragma warning(push)
#pragma warning(disable : 4100)       // the parameters are read through ebp
__declspec(naked) static void __stdcall CallDrawLayer(DWORD img, const DWORD* a)
{
    __asm {
        push ebp
        mov ebp, esp
        push esi
        mov esi, [ebp + 12]
        push dword ptr [esi + 32]
        push dword ptr [esi + 28]
        push dword ptr [esi + 24]
        push dword ptr [esi + 20]
        push dword ptr [esi + 16]
        push dword ptr [esi + 12]
        push dword ptr [esi + 8]
        push dword ptr [esi + 4]
        push dword ptr [esi]
        mov ecx, [ebp + 8]
        call dword ptr [trLayer]
        pop esi
        pop ebp
        ret 8
    }
}
#pragma warning(pop)

// ---------------------------------------------------------------- stage C groundwork (uhd_pictrace)
// 0x4d290 LoadPicture: ecx = flag, edx = the surface to fill (colour; alpha surface embedded at +0x5C), stack (name, ?, ?)
// - opens the archive entry by name, sniffs PNG / BM / JPEG and decodes into the surface.
struct PicName { DWORD surf; char name[96]; };
static PicName picNames[512];
static LONG picNext, picLogged, layerLogged;
static BYTE* trLoad;

static const char* PicNameOf(DWORD surf)
{
    for (int i = 0; i < 512; i++) if (picNames[i].surf == surf && surf) return picNames[i].name;
    return NULL;
}

static void __stdcall PreLoad(DWORD flag, DWORD surf, DWORD* stack)
{
    __try
    {
        const char* name = (const char*)(UINT_PTR)stack[0];
        if (cfg.stagec) HiresOnLoad(surf, name);
        if (!cfg.pictrace) return;
        PicName* p = NULL;
        for (int i = 0; i < 512 && !p; i++) if (picNames[i].surf == surf) p = &picNames[i];
        if (!p) p = &picNames[InterlockedIncrement(&picNext) % 512];
        p->surf = surf;
        lstrcpynA(p->name, name ? name : "?", sizeof p->name);
        if (InterlockedIncrement(&picLogged) <= 4000)
            Log("pic: load '%s' -> surf %08lx (flag %lu, a2 %08lx, a3 %08lx, from %05lx)", p->name, surf, flag, stack[1], stack[2],
                Rva((void*)(UINT_PTR)stack[-1]));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(naked) static void HookLoad()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        push edx
        push ecx
        call PreLoad
        popad
        jmp dword ptr [trLoad]
    }
}

// ---------------------------------------------------------------- stage C: composites of 4x pictures (button strips)
// 0x4f430 (MMX) / 0x4fdb0: one surface and its alpha copied or blended into another - stdcall, ret 0x24, eax = sy:
//   (src, dst, dx, dy, sx, w, h, key, opacity 0..100)
// A button's strip (normal / hover / pressed side by side) is made so from three loaded pictures and then drawn with
// DrawLayer like a picture. A copy from a 4x picture gives the destination a composite at K (BGRA): the copied rectangle
// from the picture at K where the engine's result is a plain copy of the source, everything else scaled up from the
// engine's pixels. A checksum of the destination's 1x pixels (colour and alpha) after our last update tells whether
// the engine has changed it by other code since. A destination with a twin (a scene surface) gets the copy in C.
struct Comp
{
    BYTE* bits;            // the destination's colour pixels (its identity)
    int w, h, stride;
    BYTE* abits;           // its 8-bit alpha or NULL
    int astride;
    BYTE* k;               // BGRA at K, top-down, kw x kh
    int kw, kh, kP;
    DWORD sum;
    DWORD used;
    bool hd;               // some part is from a 4x picture
};
static Comp comps[48];
static SIZE_T compBytes;
static const SIZE_T COMP_MAX = (SIZE_T)96 << 20;
static LONG compCopies, compCopiesHd, compNotPlain, compUses, compStale, compTwin;
static BYTE *trCompM, *trCompN;

static bool ViewAlpha(DWORD s, View1* a) { return View(s + 0x5C, a) && a->bpp == 8; }

static inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static DWORD SumPixels(const BYTE* c, int cs, int w, int h, const BYTE* al, int as)
{
    DWORD x = 2166136261u;
    int rb = w * 3;
    for (int r = 0; r < h; r++)
    {
        const BYTE* row = c + (SIZE_T)r * cs;
        int n = rb / 4;
        for (int i = 0; i < n; i++) x = (x ^ ((const DWORD*)row)[i]) * 16777619u;
        for (int i = n * 4; i < rb; i++) x = (x ^ row[i]) * 16777619u;
        if (!al) continue;
        const BYTE* ar = al + (SIZE_T)r * as;
        int m = w / 4;
        for (int i = 0; i < m; i++) x = (x ^ ((const DWORD*)ar)[i]) * 16777619u;
        for (int i = m * 4; i < w; i++) x = (x ^ ar[i]) * 16777619u;
    }
    return x;
}

static DWORD CompSum(const Comp* c) { return SumPixels(c->bits, c->stride, c->w, c->h, c->abits, c->astride); }

static Comp* FindComp(const BYTE* bits)
{
    for (int i = 0; i < 48; i++) if (comps[i].bits && comps[i].bits == bits) return &comps[i];
    return NULL;
}

static void FreeComp(Comp* c)
{
    if (c->k) { VirtualFree(c->k, 0, MEM_RELEASE); compBytes -= (SIZE_T)c->kw * c->kh * 4; }
    c->k = NULL;
    c->bits = NULL;
    c->hd = false;
}

// 1x pixels of the composite's surface (bottom-up) scaled up into its K picture over the 1x rectangle [x0,x1)x[y0,y1)
// (bilinear, edge clamped, alpha straight like the engine's)
static void CompUp(Comp* c, int x0, int y0, int x1, int y1)
{
    x0 = Clamp(x0, 0, c->w);
    y0 = Clamp(y0, 0, c->h);
    int X0 = Sc(x0), Y0 = Sc(y0), X1 = x1 >= c->w ? c->kw : Sc(x1), Y1 = y1 >= c->h ? c->kh : Sc(y1);
    for (int Y = Y0; Y < Y1; Y++)
    {
        int s = ((2 * Y + 1) * B1 * 128) / P - 128, sy = s >> 8, wy = s & 255;
        int ya = Clamp(sy, 0, c->h - 1), yb = Clamp(sy + 1, 0, c->h - 1);
        const BYTE* r0 = c->bits + (SIZE_T)(c->h - 1 - ya) * c->stride;
        const BYTE* r1 = c->bits + (SIZE_T)(c->h - 1 - yb) * c->stride;
        const BYTE* q0 = c->abits ? c->abits + (SIZE_T)(c->h - 1 - ya) * c->astride : NULL;
        const BYTE* q1 = c->abits ? c->abits + (SIZE_T)(c->h - 1 - yb) * c->astride : NULL;
        BYTE* d = c->k + ((SIZE_T)Y * c->kw + X0) * 4;
        for (int X = X0; X < X1; X++, d += 4)
        {
            int t = ((2 * X + 1) * B1 * 128) / P - 128, sx = t >> 8, wx = t & 255;
            int xa = Clamp(sx, 0, c->w - 1), xb = Clamp(sx + 1, 0, c->w - 1);
            for (int ch = 0; ch < 3; ch++)
            {
                int u = r0[xa * 3 + ch] * (256 - wx) + r0[xb * 3 + ch] * wx;
                int q = r1[xa * 3 + ch] * (256 - wx) + r1[xb * 3 + ch] * wx;
                d[ch] = (BYTE)((u * (256 - wy) + q * wy + 32768) >> 16);
            }
            if (q0)
            {
                int u = q0[xa] * (256 - wx) + q0[xb] * wx, q = q1[xa] * (256 - wx) + q1[xb] * wx;
                d[3] = (BYTE)((u * (256 - wy) + q * wy + 32768) >> 16);
            }
            else d[3] = 255;
        }
    }
}

static Comp* NewComp(const View1& dv, const View1* av)
{
    int kw = Sc(dv.w), kh = Sc(dv.h);
    SIZE_T need = (SIZE_T)kw * kh * 4;
    if (need > COMP_MAX / 2) return NULL;
    Comp* c = NULL;
    for (;;)
    {
        Comp* free_ = NULL;
        Comp* old = NULL;
        for (int i = 0; i < 48; i++)
        {
            if (!comps[i].bits) { if (!free_) free_ = &comps[i]; continue; }
            if (!old || comps[i].used < old->used) old = &comps[i];
        }
        if (free_ && compBytes + need <= COMP_MAX) { c = free_; break; }
        if (!old) return NULL;
        FreeComp(old);
    }
    c->k = (BYTE*)VirtualAlloc(NULL, need, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!c->k) return NULL;
    compBytes += need;
    c->bits = dv.bits;
    c->w = dv.w;
    c->h = dv.h;
    c->stride = dv.stride;
    c->abits = av ? av->bits : NULL;
    c->astride = av ? av->stride : 0;
    c->kw = kw;
    c->kh = kh;
    c->kP = P;
    c->hd = false;
    CompUp(c, 0, 0, c->w, c->h);
    return c;
}

// test (11-18): where an exception in our drawing code happened (what, code, offset in khd4k.dll, the address it touched)
extern "C" IMAGE_DOS_HEADER __ImageBase;
static int ExcLog(const char* what, EXCEPTION_POINTERS* ep)
{
    static LONG n;
    if (InterlockedIncrement(&n) <= 60)
    {
        const EXCEPTION_RECORD* r = ep->ExceptionRecord;
        bool acc = r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2;
        Log("uhd: %s: exception %08lx at khd4k+%05lx%s%p", what, r->ExceptionCode,
            (DWORD)((const BYTE*)r->ExceptionAddress - (const BYTE*)&__ImageBase),
            acc ? (r->ExceptionInformation[0] ? " writing " : " reading ") : " ", acc ? (void*)r->ExceptionInformation[1] : NULL);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// a surface's composite record, only while it still describes the same colour AND alpha memory (11-18: the engine
// makes button strips again on every page; a new strip can get the old colour memory while its alpha comes from elsewhere
// and the old alpha is freed - reading it (CompSum) faulted, 46 "twin draw failed" at a time on the DUEL voice pages)
static LONG compAlphaMoved;
static Comp* CompOf(DWORD s, const View1& v)
{
    Comp* c = FindComp(v.bits);
    if (!c) return NULL;
    View1 av = {};
    const BYTE* ab = ViewAlpha(s, &av) && av.w == v.w && av.h == v.h ? av.bits : NULL;
    if (c->w != v.w || c->h != v.h || c->stride != v.stride || c->abits != ab || (ab && c->astride != av.stride))
    {
        compAlphaMoved++;
        FreeComp(c);
        return NULL;
    }
    return c;
}

// the picture at K behind a surface that is a composite with 4x parts (still as we left it), else NULL
static const BYTE* CompAtK(DWORD s, const View1& sv, int* kw, int* kh, int* alpha)
{
    Comp* c = CompOf(s, sv);
    if (!c || !c->hd) return NULL;
    if (c->kP != P || CompSum(c) != c->sum)
    {
        compStale++;
        FreeComp(c);
        return NULL;
    }
    c->used = GetTickCount();
    compUses++;
    *kw = c->kw;
    *kh = c->kh;
    *alpha = c->abits != NULL;
    return c->k;
}

static void BlendKIntoTwin(Twin* t, const BYTE* src4, int sw, int sh, bool srcAlpha, int sxK, int syK, int X0, int Y0, int X1,
                           int Y1, BYTE op);

struct CompCtx { int active, sy; DWORD src, dst; int dx, dy, sx, w, h; Twin* t; int op; };

// the destination's rectangle before the engine writes it (colour rows then alpha rows), to check its blend afterwards
static BYTE* snap;
static SIZE_T snapCap;
static LONG compBlend;

static void __stdcall CompPre(DWORD eaxv, const DWORD* a, CompCtx* x)
{
    x->active = 0;
    if (!cfg.stagec || P <= B1 || !OW) return;
    EnterCriticalSection(&lk);
    __try
    {
        View1 sv, dv, da = {};
        if (View(a[0], &sv) && View(a[1], &dv) && sv.bpp == 24 && dv.bpp == 24 && a[8] <= 100)
        {
            int dx = (int)a[2], dy = (int)a[3], sx = (int)a[4], sy = (int)eaxv, w = (int)a[5], h = (int)a[6];
            if (sx < 0) { dx -= sx; w += sx; sx = 0; }
            if (sy < 0) { dy -= sy; h += sy; sy = 0; }
            if (dx < 0) { sx -= dx; w += dx; dx = 0; }
            if (dy < 0) { sy -= dy; h += dy; dy = 0; }
            if (sx + w > sv.w) w = sv.w - sx;
            if (sy + h > sv.h) h = sv.h - sy;
            if (dx + w > dv.w) w = dv.w - dx;
            if (dy + h > dv.h) h = dv.h - dy;
            int kw, kh, al;
            Twin* t = dv.w == FW && dv.h == FH ? FindTwin(dv.bits) : NULL;
            bool hd = HiresAtK(a[0], sv.bits, sv.stride, sv.w, sv.h, P, &kw, &kh, &al) != NULL;
            SIZE_T need = (SIZE_T)w * h * 4;
            if (w > 0 && h > 0 && (hd || t || FindComp(dv.bits)))
            {
                if (need > snapCap)
                {
                    if (snap) VirtualFree(snap, 0, MEM_RELEASE);
                    snapCap = (need + 0xFFFF) & ~(SIZE_T)0xFFFF;
                    snap = (BYTE*)VirtualAlloc(NULL, snapCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                    if (!snap) snapCap = 0;
                }
                if (snap)
                {
                    x->active = 1;
                    x->src = a[0];
                    x->dst = a[1];
                    x->dx = dx;
                    x->dy = dy;
                    x->sx = sx;
                    x->sy = sy;
                    x->w = w;
                    x->h = h;
                    x->t = t;
                    x->op = *((BYTE*)sys + 0xCDD80 + a[8] * 4);  // the copy's own opacity table (0x100cdd80)
                    bool dHasA = ViewAlpha(a[1], &da) && da.w == dv.w && da.h == dv.h;
                    for (int r = 0; r < h; r++)
                    {
                        memcpy(snap + (SIZE_T)r * w * 3, dv.bits + (SIZE_T)(dv.h - 1 - (dy + r)) * dv.stride + dx * 3, (SIZE_T)w * 3);
                        if (dHasA) memcpy(snap + (SIZE_T)w * h * 3 + (SIZE_T)r * w, da.bits + (SIZE_T)(da.h - 1 - (dy + r)) * da.stride + dx, w);
                    }
                    if (t)                                       // C in line with E there before the engine writes
                    {
                        int bx0, by0, bx1, by1;
                        BlockRange(dx, dy, w, h, &bx0, &by0, &bx1, &by1);
                        TwinSync(t, bx0, by0, bx1, by1);
                        TwinFlush(t, bx0, by0, bx1, by1);
                    }
                    else
                    {
                        Comp* c = CompOf(a[1], dv);              // changed behind our back: start it again
                        if (c && (c->kP != P || CompSum(c) != c->sum)) FreeComp(c);
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { x->active = 0; }
    LeaveCriticalSection(&lk);
}

// what the engine did, checked pixel by pixel against the source and the rectangle as it was:
//   1 copy  - alpha byte for byte (255 without a source alpha), colour where it can be seen (alpha > 0)
//   2 blend - colour = (d * 256 + (s - d) * t) >> 8, t = alpha * opacity >> 8 (DrawLayer's own blend), alpha copied
//             (button strips: the three states are blended onto the black strip with their alpha)
//   0 something else - that rectangle is scaled up from the engine's pixels
static int CompMode(const CompCtx* x, const View1& sv, const View1& dv, const View1* sa, const View1* da, int* mx, int* my)
{
    bool copy = true, blend = true;
    for (int r = 0; r < x->h && (copy || blend); r++)
    {
        const BYTE* s = sv.bits + (SIZE_T)(sv.h - 1 - (x->sy + r)) * sv.stride + x->sx * 3;
        const BYTE* d = dv.bits + (SIZE_T)(dv.h - 1 - (x->dy + r)) * dv.stride + x->dx * 3;
        const BYTE* s1 = sa ? sa->bits + (SIZE_T)(sa->h - 1 - (x->sy + r)) * sa->stride + x->sx : NULL;
        const BYTE* d1 = da ? da->bits + (SIZE_T)(da->h - 1 - (x->dy + r)) * da->stride + x->dx : NULL;
        const BYTE* d0 = snap + (SIZE_T)r * x->w * 3;
        for (int i = 0; i < x->w && (copy || blend); i++)
        {
            int A = s1 ? s1[i] : 255, t = (A * x->op) >> 8;
            bool aCopied = d1 ? d1[i] == A : true;
            const BYTE *ps = s + i * 3, *pd = d + i * 3, *p0 = d0 + i * 3;
            if (copy) copy = aCopied && ((d1 && A == 0) || (ps[0] == pd[0] && ps[1] == pd[1] && ps[2] == pd[2]));
            if (blend)
                for (int c = 0; c < 3 && blend; c++) blend = aCopied && pd[c] == (BYTE)((p0[c] * 256 + (ps[c] - p0[c]) * t) >> 8);
            if (!copy && !blend) { *mx = i; *my = r; }
        }
    }
    return copy ? 1 : (blend ? 2 : 0);
}

static void __stdcall CompPost(CompCtx* x)
{
    if (!x->active) return;
    EnterCriticalSection(&lk);
    __try
    {
        View1 sv, dv, sa = {}, da = {};
        if (View(x->src, &sv) && View(x->dst, &dv))
        {
            bool sHasA = ViewAlpha(x->src, &sa) && sa.w == sv.w && sa.h == sv.h;
            bool dHasA = ViewAlpha(x->dst, &da) && da.w == dv.w && da.h == dv.h;
            int mx = 0, my = 0;
            int mode = CompMode(x, sv, dv, sHasA ? &sa : NULL, dHasA ? &da : NULL, &mx, &my);
            compCopies++;
            if (mode == 2) compBlend++;
            if (!mode) compNotPlain++;
            static LONG notPlainLogged;
            if (!mode && InterlockedIncrement(&notPlainLogged) <= 12)
            {
                const BYTE* s = sv.bits + (SIZE_T)(sv.h - 1 - (x->sy + my)) * sv.stride + (x->sx + mx) * 3;
                const BYTE* d = dv.bits + (SIZE_T)(dv.h - 1 - (x->dy + my)) * dv.stride + (x->dx + mx) * 3;
                const BYTE* d0 = snap + ((SIZE_T)my * x->w + mx) * 3;
                int A = sHasA ? sa.bits[(SIZE_T)(sa.h - 1 - (x->sy + my)) * sa.stride + x->sx + mx] : -1;
                int D = dHasA ? da.bits[(SIZE_T)(da.h - 1 - (x->dy + my)) * da.stride + x->dx + mx] : -1;
                int D0 = dHasA ? snap[(SIZE_T)x->w * x->h * 3 + (SIZE_T)my * x->w + mx] : -1;
                Log("uhd: composite copy neither copy nor blend: %dx%d -> %dx%d d(%d,%d) s(%d,%d) %dx%d op %d, first at (%d,%d): src %02x%02x%02x "
                    "a %d, before %02x%02x%02x a %d, after %02x%02x%02x a %d", sv.w, sv.h, dv.w, dv.h, x->dx, x->dy, x->sx, x->sy, x->w,
                    x->h, x->op, mx, my, s[2], s[1], s[0], A, d0[2], d0[1], d0[0], D0, d[2], d[1], d[0], D);
            }
            int kw = 0, kh = 0, al = 0;
            const BYTE* hk = mode ? HiresAtK(x->src, sv.bits, sv.stride, sv.w, sv.h, P, &kw, &kh, &al) : NULL;
            bool srcA = sHasA && al;
            int X0 = Sc(x->dx), Y0 = Sc(x->dy), SX = Sc(x->sx), SY = Sc(x->sy);
            if (x->t)
            {
                Twin* t = x->t;
                for (int r = 0; r < x->h; r++)                   // E := the engine's pixels there
                {
                    SIZE_T o = (SIZE_T)(FH - 1 - (x->dy + r)) * t->stride + x->dx * 3;
                    memcpy(t->E + o, t->bits + o, (SIZE_T)x->w * 3);
                }
                int bx0, by0, bx1, by1;
                BlockRange(x->dx, x->dy, x->w, x->h, &bx0, &by0, &bx1, &by1);
                if (hk)
                {
                    BlendKIntoTwin(t, hk, kw, kh, mode == 2 && srcA, SX, SY, X0, Y0, Sc(x->dx + x->w), Sc(x->dy + x->h),
                                   (BYTE)(mode == 2 ? x->op : 255));
                    compCopiesHd++;
                    for (int by = by0; by <= by1; by++)
                        for (int bx = bx0; bx <= bx1; bx++) t->hdT[by][bx] = 1;
                }
                else
                    for (int by = by0; by <= by1; by++)
                        for (int bx = bx0; bx <= bx1; bx++) t->pendT[by][bx] = true;
                compTwin++;
            }
            else
            {
                Comp* c = CompOf(x->dst, dv);
                if (!c && hk) c = NewComp(dv, dHasA ? &da : NULL);
                if (c)
                {
                    if (hk)
                    {
                        int X1 = x->dx + x->w >= c->w ? c->kw : Sc(x->dx + x->w);
                        int Y1 = x->dy + x->h >= c->h ? c->kh : Sc(x->dy + x->h);
                        for (int Y = Y0; Y < Y1; Y++)
                        {
                            const BYTE* srow = hk + (SIZE_T)Clamp(SY + (Y - Y0), 0, kh - 1) * kw * 4;
                            BYTE* d = c->k + ((SIZE_T)Y * c->kw + X0) * 4;
                            for (int X = X0; X < X1; X++, d += 4)
                            {
                                const BYTE* p = srow + Clamp(SX + (X - X0), 0, kw - 1) * 4;
                                int A = srcA ? p[3] : 255;
                                if (mode == 1)
                                {
                                    d[0] = p[0];
                                    d[1] = p[1];
                                    d[2] = p[2];
                                }
                                else
                                {
                                    int tt = (A * x->op) >> 8;
                                    for (int ch = 0; ch < 3; ch++) d[ch] = (BYTE)((d[ch] * 256 + (p[ch] - d[ch]) * tt) >> 8);
                                }
                                d[3] = dHasA ? (BYTE)A : 255;
                            }
                        }
                        c->hd = true;
                        compCopiesHd++;
                    }
                    else CompUp(c, x->dx - 1, x->dy - 1, x->dx + x->w + 1, x->dy + x->h + 1);   // with a ring of neighbours
                    c->sum = CompSum(c);
                    c->used = GetTickCount();
                }
            }
        }
    }
    __except (ExcLog("composite copy failed", GetExceptionInformation())) {}
    LeaveCriticalSection(&lk);
}

// a wrapper (the original is called from here, so its result can be checked); our CompCtx lives in this frame
static_assert(sizeof(CompCtx) <= 64, "CompCtx lives in 64 bytes of the hook's frame");

__declspec(naked) static void HookCompM()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 64
        pushad
        lea ecx, [ebp - 64]
        push ecx
        lea edx, [ebp + 8]
        push edx
        push eax
        call CompPre
        popad
        push dword ptr [ebp + 40]
        push dword ptr [ebp + 36]
        push dword ptr [ebp + 32]
        push dword ptr [ebp + 28]
        push dword ptr [ebp + 24]
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        call dword ptr [trCompM]
        pushad
        lea ecx, [ebp - 64]
        push ecx
        call CompPost
        popad
        mov esp, ebp
        pop ebp
        ret 0x24
    }
}

__declspec(naked) static void HookCompN()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 64
        pushad
        lea ecx, [ebp - 64]
        push ecx
        lea edx, [ebp + 8]
        push edx
        push eax
        call CompPre
        popad
        push dword ptr [ebp + 40]
        push dword ptr [ebp + 36]
        push dword ptr [ebp + 32]
        push dword ptr [ebp + 28]
        push dword ptr [ebp + 24]
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        call dword ptr [trCompN]
        pushad
        lea ecx, [ebp - 64]
        push ecx
        call CompPost
        popad
        mov esp, ebp
        pop ebp
        ret 0x24
    }
}

// ---------------------------------------------------------------- stage C: a DrawLayer into a twinned surface at K
// t = alpha * opacity >> 8, d = (d * 256 + (s - d) * t) >> 8 - the engine's own row blend (0x54e20 / 0x54f40)
static void BlendKIntoTwin(Twin* t, const BYTE* src4, int sw, int sh, bool srcAlpha, int sxK, int syK, int X0, int Y0, int X1,
                           int Y1, BYTE op)
{
    for (int Y = Y0; Y < Y1; Y++)
    {
        int py = syK + (Y - Y0);
        if (py < 0) py = 0;
        if (py >= sh) py = sh - 1;
        const BYTE* srow = src4 + (SIZE_T)py * sw * 4;
        BYTE* d = t->C + ((SIZE_T)Y * OW + X0) * 3;
        int px = sxK;
        if (!srcAlpha && op >= 254 && sxK >= 0 && sxK + (X1 - X0) <= sw)
        {
            const BYTE* s = srow + sxK * 4;                      // opaque at full opacity: a straight copy (the blend
            for (int X = X0; X < X1; X++, d += 3, s += 4)        // would leave d within 1/128 of s)
            {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
            }
            continue;
        }
        for (int X = X0; X < X1; X++, d += 3, px++)
        {
            const BYTE* s = srow + (px < 0 ? 0 : (px >= sw ? sw - 1 : px)) * 4;
            int tt = ((srcAlpha ? s[3] : 255) * op) >> 8;
            if (tt <= 0) continue;
            d[0] = (BYTE)((d[0] * 256 + (s[0] - d[0]) * tt) >> 8);
            d[1] = (BYTE)((d[1] * 256 + (s[1] - d[1]) * tt) >> 8);
            d[2] = (BYTE)((d[2] * 256 + (s[2] - d[2]) * tt) >> 8);
        }
    }
}

static void BlendPlaneIntoTwin(Twin* t, const Plane* pl, int PX, int PY, int X0, int Y0, int X1, int Y1, BYTE op)
{
    for (int Y = Y0; Y < Y1; Y++)
    {
        int py = PY + (Y - Y0);
        if (py >= pl->ph) py = pl->ph - 1;
        const BYTE* sc3 = pl->c + (SIZE_T)py * 3 * pl->pw;
        const BYTE* sa1 = pl->a + (SIZE_T)py * pl->pw;
        BYTE* d = t->C + ((SIZE_T)Y * OW + X0) * 3;
        for (int X = X0; X < X1; X++, d += 3)
        {
            int px = PX + (X - X0);
            if (px >= pl->pw) px = pl->pw - 1;
            int tt = (sa1[px] * op) >> 8;
            if (tt <= 0) continue;
            const BYTE* s = sc3 + px * 3;
            for (int c = 0; c < 3; c++) d[c] = (BYTE)(((s[c] - d[c]) * tt + (d[c] << 8)) >> 8);
        }
    }
}

// called (under lk) for a DrawLayer whose destination is not the presented frame
static void TwinLayer(DWORD img, const DWORD* a)
{
    View1 dv, sv;
    if (P <= B1 || !OW || !View(a[0], &dv) || dv.w != FW || dv.h != FH || dv.bpp != 24) return;
    DWORD sc = *(DWORD*)(UINT_PTR)(img + 4), sa = *(DWORD*)(UINT_PTR)(img + 8);
    int opIdx = *(int*)(UINT_PTR)(img + 0x3C), key = *(int*)(UINT_PTR)(img + 0x40);
    if (!View(sc, &sv)) return;
    Twin* t = FindTwin(dv.bits);
    int kw = 0, kh = 0, hAlpha = 0;
    const BYTE* hk = HiresAtK(sc, sv.bits, sv.stride, sv.w, sv.h, P, &kw, &kh, &hAlpha);
    if (!hk) hk = CompAtK(sc, sv, &kw, &kh, &hAlpha);            // a button strip made of 4x pictures
    if (!t)
    {
        if (!hk) return;                                         // only a 4x picture starts a twin
        t = MakeTwin(a[0], dv);
        if (!t) return;
    }
    t->used = GetTickCount();
    twinDraws++;
    int dx = (int)a[1], dy = (int)a[2], sx = (int)a[3], sy = (int)a[4], w = (int)a[7], h = (int)a[8];
    bool same = (int)a[5] == sx && (int)a[6] == sy;
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (sx + w > sv.w) w = sv.w - sx;
    if (sy + h > sv.h) h = sv.h - sy;
    if (dx + w > FW) w = FW - dx;
    if (dy + h > FH) h = FH - dy;
    // the engine's own draw into E (E stays what the engine will have once this draw is done)
    memcpy(t->fake, (void*)(UINT_PTR)a[0], sizeof t->fake);
    *(BYTE**)(t->fake + 0xC) = t->E;
    // an opaque 4x picture at full opacity overwrites whole blocks in C: no need to scale them up first
    bool cover = hk && key == -2 && same && opIdx >= 0 && opIdx <= 100 && OpacityByte(opIdx) >= 254 && !(hAlpha && sa) &&
                 Sc(sx) >= 0 && Sc(sx) + Sc(dx + w) - Sc(dx) <= kw;
    int cbx0 = (dx + B1 - 1) / B1, cby0 = (dy + B1 - 1) / B1, cbx1 = (dx + w) / B1 - 1, cby1 = (dy + h) / B1 - 1;
    if (!cover) { cbx0 = cby0 = 1; cbx1 = cby1 = 0; }
    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    if (w > 0 && h > 0)
    {
        BlockRange(dx, dy, w, h, &bx0, &by0, &bx1, &by1);
        TwinSync(t, bx0, by0, bx1, by1);                         // E stays exact (the blend keeps 2/256 of what was there)
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++)
                if (bx < cbx0 || bx > cbx1 || by < cby0 || by > cby1) TwinFlush(t, bx, by, bx, by);
    }
    // a draw we cannot make at K: only the blocks it really changes go back to scaling up (a mostly transparent overlay,
    // like the screen frame over a 4x background, must not cost the background its 4x where it lets it through)
    static BYTE* snapE;
    bool willDo = opIdx >= 0 && opIdx <= 100 && same &&
                  ((hk && key == -2) || (FindPlane(sc) != NULL && sa && (key == -2 || key == -1)));
    int rowB = (bx1 - bx0 + 1) * B1 * 3;
    if (!willDo && w > 0 && h > 0)
    {
        if (!snapE) snapE = (BYTE*)VirtualAlloc(NULL, (SIZE_T)FW * FH * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (snapE)
            for (int y = by0 * B1; y < (by1 + 1) * B1; y++)
                memcpy(snapE + (SIZE_T)(y - by0 * B1) * rowB, t->E + (SIZE_T)(FH - 1 - y) * t->stride + bx0 * B1 * 3, rowB);
    }
    DWORD b[9];
    memcpy(b, a, sizeof b);
    b[0] = (DWORD)(UINT_PTR)t->fake;
    CallDrawLayer(img, b);
    if (w <= 0 || h <= 0) return;
    bool done = false;
    if (!willDo && snapE)
    {
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++)
            {
                bool same5 = true;
                for (int r = 0; r < B1 && same5; r++)
                {
                    int y = by * B1 + r;
                    same5 = memcmp(snapE + (SIZE_T)(y - by0 * B1) * rowB + (bx - bx0) * B1 * 3,
                                   t->E + (SIZE_T)(FH - 1 - y) * t->stride + bx * B1 * 3, B1 * 3) == 0;
                }
                if (!same5) t->pendT[by][bx] = true;
            }
        return;
    }
    if (opIdx >= 0 && opIdx <= 100 && same)
    {
        BYTE op = OpacityByte(opIdx);
        int X0 = Sc(dx), Y0 = Sc(dy), X1 = Sc(dx + w), Y1 = Sc(dy + h);
        Plane* pl;
        if (hk && key == -2)
        {
            BlendKIntoTwin(t, hk, kw, kh, hAlpha && sa, Sc(sx), Sc(sy), X0, Y0, X1, Y1, op);
            twinHd++;
            done = true;
            for (int by = cby0; by <= cby1; by++)
                for (int bx = cbx0; bx <= cbx1; bx++) t->pendT[by][bx] = false;
        }
        else if ((pl = FindPlane(sc)) != NULL && sa && (key == -2 || key == -1))
        {
            BlendPlaneIntoTwin(t, pl, Sc(sx), Sc(sy), X0, Y0, X1, Y1, op);
            twinPlane++;
            done = true;
        }
        if (done)
            for (int by = by0; by <= by1; by++)
                for (int bx = bx0; bx <= bx1; bx++) t->hdT[by][bx] = 1;
    }
    if (!done)                                                   // C follows E there by scaling up
        for (int by = by0; by <= by1; by++)
            for (int bx = bx0; bx <= bx1; bx++) t->pendT[by][bx] = true;
}

// 0x4e690 (MMX) / 0x4ed70: one surface blended into another, cdecl 9 args (s1, s2, a3..a7, key, opacity 0..100) -
// logged with both surfaces' bits so a temporary surface object made on the stack still shows which pixels it is
static BYTE *trBlendM, *trBlendN;
static LONG blendLogged;

static void __stdcall PreBlend(DWORD which, DWORD* a)
{
    __try
    {
        if (InterlockedIncrement(&blendLogged) > 3000) return;
        View1 v1 = {}, v2 = {};
        View(a[0], &v1);
        View(a[1], &v2);
        const char* n2 = PicNameOf(a[1]);
        Log("pic: blend%c s1 %08lx bits %p (%dx%d)%s  s2 %08lx bits %p (%dx%d)%s  a3..a7 %ld %ld %ld %ld %ld key %ld op %ld  name2 %s "
            "from %05lx", which ? 'M' : 'N', a[0], v1.bits, v1.w, v1.h, v1.bits && v1.bits == frameBits ? " FRAME" : "", a[1], v2.bits,
            v2.w, v2.h, v2.bits && v2.bits == frameBits ? " FRAME" : "", a[2], a[3], a[4], a[5], a[6], a[7], a[8], n2 ? n2 : "-",
            Rva((void*)(UINT_PTR)a[-1]));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(naked) static void HookBlendM()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        push 1
        call PreBlend
        popad
        jmp dword ptr [trBlendM]
    }
}

__declspec(naked) static void HookBlendN()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        push 0
        call PreBlend
        popad
        jmp dword ptr [trBlendN]
    }
}

static void TraceLayer(DWORD img, const DWORD* a, bool toFrame)
{
    __try
    {
        if (InterlockedIncrement(&layerLogged) > 6000) return;
        DWORD c = *(DWORD*)(UINT_PTR)(img + 4), al = *(DWORD*)(UINT_PTR)(img + 8);
        int op = *(int*)(UINT_PTR)(img + 0x3C), key = *(int*)(UINT_PTR)(img + 0x40);
        View1 v = {};
        View(c, &v);
        const char* n = PicNameOf(c);
        View1 dv = {};
        View(a[0], &dv);
        Log("pic: layer img %08lx colour %08lx (%dx%d %dbpp) bits %p alpha %08lx op %d key %d -> %s dst %08lx bits %p (%dx%d) d(%ld,%ld) "
            "s(%ld,%ld) a(%ld,%ld) %ldx%ld  name %s", img, c, v.w, v.h, v.bpp, v.bits, al, op, key, toFrame ? "FRAME" : "other", a[0],
            dv.bits, dv.w, dv.h, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], n ? n : "-");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void __stdcall PreLayer(DWORD img, DWORD* a)
{
    DWORD dst = a[0];
    if (cfg.pictrace)
    {
        View1 fv;
        TraceLayer(img, a, frameBits && View(dst, &fv) && fv.bits == frameBits);
    }
    BYTE* f = frameBits;
    if (P < B1) return;
    if (cfg.stagec)
    {
        View1 v;
        if (View(dst, &v) && v.bits != f)                        // a scene being composed elsewhere: its twin follows
        {
            EnterCriticalSection(&lk);
            double t0 = Ms();
            __try { TwinLayer(img, a); }
            __except (ExcLog("twin draw failed", GetExceptionInformation())) {}
            double dt = Ms() - t0;
            msTwin += dt;
            if (dt > msTwinMax) msTwinMax = dt;
            LeaveCriticalSection(&lk);
            return;
        }
    }
    if (!f) return;
    if (dst != frameSurf)
    {
        View1 v;
        if (!View(dst, &v) || v.bits != f) return;               // only the presented frame
        EnterCriticalSection(&lk);
        bool ok = StartFrame(dst);
        LeaveCriticalSection(&lk);
        if (!ok) return;
    }
    EnterCriticalSection(&lk);
    double t0 = Ms();
    if (Mirror(img, a))
    {
        DWORD b[9];
        memcpy(b, a, sizeof b);
        b[0] = (DWORD)(UINT_PTR)fakeDst;
        CallDrawLayer(img, b);                                   // the same draw, by the engine, into the shadow
        double dt = Ms() - t0;
        msDraw += dt;
        if (dt > msDrawMax) msDrawMax = dt;
        nDraw++;
    }
    LeaveCriticalSection(&lk);
}

__declspec(naked) static void HookLayer()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        push ecx
        call PreLayer
        popad
        jmp dword ptr [trLayer]
    }
}

// ---------------------------------------------------------------- stage C: transitions followed at K
// The engine moves a new scene to the frame with transitions (fades, rule wipes) by code we do not follow. Every step
// leaves each pixel between what the block showed before it started changing (OLD1 / OLDK, kept per block) and where
// it is going (a twin's E, or black / white for a fade through them): F = O + (N - O) * a, a per pixel (a rule's mask)
// or one a for the block (a fade). We solve a from the 1x pixels - first one a for the whole block (fades: steady,
// no noise), else per pixel - check the block is explained to within a few levels, and make the step at K from the
// block's start: FO = OLDK + (N_K - OLDK) * a scaled up (N_K = the twin's C, or the colour). Starting from OLD each
// step (not from the last step) keeps the 1x rounding from adding up at K. Blocks nothing explains are scaled up.
static LONG transFollowed, transMissed;
static BYTE *OLD1, *OLDK;                    // the frame (1x, engine layout) and FO (K) of blocks being changed

// A present's block work in two phases: the 1x decisions in order (they read and write the twins' E, S1, OLD1 and the
// block flags), then the K work - steps of transitions, adoptions from a twin's C, scaling up from S1 - by a pool of
// threads, each on its own blocks (they read the 1x layers, settled by then, and write only their block of FO / C /
// OLDK). At 4K a whole-screen transition step is some 7 million output pixels.
enum { J_EXPLAIN = 1, J_ADOPT = 2, J_UP = 3 };
struct PJob { short bx, by; BYTE type, twin; bool flat, snap; int col, wb; short wq[B1][B1]; };
static PJob* pjobs;
static int pjCount;
static volatile LONG pjNext, pjLeft;
static HANDLE pjStart, pjFinish;
static int pjThreads;

// phase 1 of a transition step: which candidate explains the block and its a (per block or per pixel)
static bool ExplainDecide(int bx, int by, const BYTE* f, PJob* j)
{
    if (!OLD1 || !OLDK) return false;
    bool takeOld = false;
    if (!oldOn[by][bx])
    {
        if (pend[by][bx]) return false;                          // FO is not S1 at K there yet
        for (int r = 0; r < B1; r++)                             // the block before it started changing
        {
            SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * f1Stride + bx * B1 * 3;
            memcpy(OLD1 + o, S1 + o, B1 * 3);
        }
        oldOn[by][bx] = true;
        takeOld = true;                                          // OLDK from FO in phase 2, before the block is written
    }
    for (int cand = 0; cand < nTwins + 2; cand++)
    {
        Twin* t = cand < nTwins ? &twins[cand] : NULL;
        int col = cand == nTwins ? 0 : 255;                      // after the twins: black, then white
        if (t)
        {
            if (!t->bits || t->stride != f1Stride) continue;
            __try { TwinSync(t, bx, by, bx, by); }
            __except (EXCEPTION_EXECUTE_HANDLER) { t->bits = NULL; continue; }
        }
        // one a for the block: least squares over the pixels where old and new differ
        long long sdd = 0, snn = 0;
        bool moved = false, flat = true;
        for (int r = 0; r < B1; r++)
        {
            SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * f1Stride + bx * B1 * 3;
            const BYTE *s = OLD1 + o, *fp = f + o, *n = t ? t->E + o : NULL;
            for (int i = 0; i < B1 * 3; i++)
            {
                int d = fp[i] - s[i], nv = (n ? n[i] : col) - s[i];
                sdd += d * nv;
                snn += nv * nv;
                if (d) moved = true;
            }
        }
        if (!moved) continue;
        int wb = snn <= 0 ? 0 : (sdd <= 0 ? 0 : (sdd >= snn ? 256 : (int)((sdd * 256 + snn / 2) / snn)));
        bool ok = true;
        for (int pass = 0; pass < 2; pass++)                     // pass 0: the block's a, pass 1: per pixel
        {
            ok = true;
            flat = pass == 0;
            for (int r = 0; r < B1 && ok; r++)
            {
                SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * f1Stride + bx * B1 * 3;
                const BYTE *s = OLD1 + o, *fp = f + o, *n = t ? t->E + o : NULL;
                for (int i = 0; i < B1 && ok; i++)
                {
                    int dd = 0, nn = 0, dmax = 0, sv[3], dv[3], nv[3];
                    for (int c = 0; c < 3; c++)
                    {
                        sv[c] = s[i * 3 + c];
                        dv[c] = fp[i * 3 + c] - sv[c];
                        nv[c] = (n ? n[i * 3 + c] : col) - sv[c];
                        dd += dv[c] * nv[c];
                        nn += nv[c] * nv[c];
                        int ad = dv[c] < 0 ? -dv[c] : dv[c];
                        if (ad > dmax) dmax = ad;
                    }
                    int w = wb;
                    if (pass == 1) w = nn < 48 ? 0 : (dd <= 0 ? 0 : (dd >= nn ? 256 : (int)(((long long)dd * 256 + nn / 2) / nn)));
                    if (nn < 48 && pass == 1) { ok = dmax <= 2; j->wq[r][i] = 0; continue; }
                    for (int c = 0; c < 3 && ok; c++)
                    {
                        int pred = sv[c] + ((nv[c] * w + (nv[c] >= 0 ? 128 : -128)) / 256);
                        int e = pred - (sv[c] + dv[c]);
                        ok = e >= -3 && e <= 3;
                    }
                    j->wq[r][i] = (short)w;
                }
            }
            if (ok) break;
        }
        if (!ok) continue;
        j->bx = (short)bx;
        j->by = (short)by;
        j->type = J_EXPLAIN;
        j->twin = (BYTE)(t ? cand : 255);
        j->flat = flat;
        j->snap = takeOld;
        j->col = col;
        j->wb = wb;
        transFollowed++;
        return true;
    }
    transMissed++;
    return false;
}

// phase 2 of a transition step: the same step at K, from the block's start
static void ApplyExplain(const PJob& j)
{
    int bx = j.bx, by = j.by;
    if (j.snap)
        for (int v = 0; v < P; v++)
        {
            SIZE_T o = ((SIZE_T)(by * P + v) * OW + bx * P) * 3;
            memcpy(OLDK + o, FO + o, (SIZE_T)P * 3);
        }
    Twin* t = j.twin != 255 ? &twins[j.twin] : NULL;
    if (t) TwinFlush(t, bx, by, bx, by);
    for (int v = 0; v < P; v++)
    {
        int sy = iTab[v], wy = wTab[v];
        int ya = sy < 0 ? 0 : (sy >= B1 ? B1 - 1 : sy), yb = sy + 1 >= B1 ? B1 - 1 : (sy + 1 < 0 ? 0 : sy + 1);
        SIZE_T o = ((SIZE_T)(by * P + v) * OW + bx * P) * 3;
        BYTE* d = FO + o;
        const BYTE* ok0 = OLDK + o;
        const BYTE* nk = t ? t->C + o : NULL;
        for (int u = 0; u < P; u++, d += 3)
        {
            int w = j.wb;
            if (!j.flat)
            {
                int sx = iTab[u], wx = wTab[u];
                int xa = sx < 0 ? 0 : (sx >= B1 ? B1 - 1 : sx), xb = sx + 1 >= B1 ? B1 - 1 : (sx + 1 < 0 ? 0 : sx + 1);
                int top = j.wq[ya][xa] * (256 - wx) + j.wq[ya][xb] * wx, bot = j.wq[yb][xa] * (256 - wx) + j.wq[yb][xb] * wx;
                w = (top * (256 - wy) + bot * wy + 32768) >> 16;
            }
            for (int c = 0; c < 3; c++)
            {
                int o0 = ok0[u * 3 + c], nvk = nk ? nk[u * 3 + c] : j.col;
                d[c] = (BYTE)(o0 + (((nvk - o0) * w + (nvk >= o0 ? 128 : -128)) / 256));
            }
        }
    }
}

static void RunJob(const PJob& j)
{
    if (j.type == J_EXPLAIN) ApplyExplain(j);
    else if (j.type == J_ADOPT)
    {
        Twin* t = &twins[j.twin];
        TwinFlush(t, j.bx, j.by, j.bx, j.by);
        for (int v = 0; v < P; v++)
        {
            SIZE_T o = ((SIZE_T)(j.by * P + v) * OW + j.bx * P) * 3;
            memcpy(FO + o, t->C + o, (SIZE_T)P * 3);
        }
        blkSrc[j.by][j.bx] = t->hdT[j.by][j.bx] ? 1 : 3;
    }
    else UpBlock(S1, j.bx, j.by);
}

static void RunJobs()
{
    for (;;)
    {
        LONG i = InterlockedExchangeAdd(&pjNext, 64);
        if (i >= pjCount) break;
        int e = min(pjCount, (int)i + 64);
        for (int k = i; k < e; k++) RunJob(pjobs[k]);
    }
}

static DWORD WINAPI JobWorker(LPVOID)
{
    while (WaitForSingleObject(pjStart, INFINITE) == WAIT_OBJECT_0)
    {
        RunJobs();
        if (InterlockedDecrement(&pjLeft) == 0) SetEvent(pjFinish);
    }
    return 0;
}

// phase 2: all the K work of this present, the calling thread helping
static void RunAllJobs()
{
    if (!pjCount) return;
    if (!pjThreads && pjCount >= 256)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        int n = (int)si.dwNumberOfProcessors - 2;
        pjThreads = n < 1 ? 1 : (n > 7 ? 7 : n);
        pjStart = CreateSemaphoreW(NULL, 0, 64, NULL);
        pjFinish = CreateEventW(NULL, FALSE, FALSE, NULL);
        for (int i = 0; i < pjThreads; i++)
        {
            HANDLE h = CreateThread(NULL, 0, JobWorker, NULL, 0, NULL);
            if (h) CloseHandle(h);
        }
        Log("uhd: %d threads for the K work of a present", pjThreads + 1);
    }
    pjNext = 0;
    if (pjThreads && pjCount >= 256)
    {
        pjLeft = pjThreads;
        ResetEvent(pjFinish);
        ReleaseSemaphore(pjStart, pjThreads, NULL);
        RunJobs();
        WaitForSingleObject(pjFinish, INFINITE);
    }
    else RunJobs();
    pjCount = 0;
}

// the pending blocks of the whole frame (at present): 1x decisions now, the K work as jobs
static void FlushAllJobs()
{
    for (int by = 0; by < BY; by++)
        for (int bx = 0; bx < BX; bx++)
        {
            if (!pend[by][bx]) continue;
            PJob& j = pjobs[pjCount];
            j.bx = (short)bx;
            j.by = (short)by;
            j.type = J_UP;
            if (cfg.stagec && nTwins && S1)
                for (int i = 0; i < nTwins; i++)
                {
                    Twin* t = &twins[i];
                    if (!t->bits || t->stride != f1Stride) continue;
                    __try { TwinSync(t, bx, by, bx, by); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { t->bits = NULL; continue; }
                    if (!BlockSame(S1, t->E, f1Stride, bx, by)) continue;
                    j.type = J_ADOPT;
                    j.twin = (BYTE)i;
                    break;
                }
            if (j.type == J_ADOPT) { adopted++; lastW[by][bx] = 2; }
            else
            {
                if (cfg.stagec && nTwins) adoptMiss++;
                blkSrc[by][bx] = 0;
                lastW[by][bx] = 1;
            }
            pjCount++;
            pend[by][bx] = false;
            DirtyBlock(bx, by);
        }
}

// test (uhd_check): every FO block averaged back to 1x against S1 - FO must be S1 at K (4x detail aside); log the blocks
// that are not, with what last wrote them, to find a path that leaves FO behind
static void CheckFO()
{
    static LONG logged, presents;
    presents++;
    int bad = 0;
    for (int by = 0; by < BY; by++)
        for (int bx = 0; bx < BX; bx++)
        {
            if (pend[by][bx]) continue;
            int sum = 0, fx0 = 0, fy0 = 0, sv[3] = {}, fv[3] = {};
            for (int r = 0; r < B1; r++)
                for (int i = 0; i < B1; i++)
                {
                    int x = bx * B1 + i, y = by * B1 + r;
                    int X0 = Sc(x), X1 = Sc(x + 1), Y0 = Sc(y), Y1 = Sc(y + 1), n = (X1 - X0) * (Y1 - Y0);
                    if (n <= 0) continue;
                    int acc[3] = {};
                    for (int Y = Y0; Y < Y1; Y++)
                        for (int X = X0; X < X1; X++)
                            for (int c = 0; c < 3; c++) acc[c] += FO[((SIZE_T)Y * OW + X) * 3 + c];
                    const BYTE* s = S1 + (SIZE_T)(FH - 1 - y) * f1Stride + x * 3;
                    for (int c = 0; c < 3; c++)
                    {
                        int v = acc[c] / n;
                        sum += v > s[c] ? v - s[c] : s[c] - v;
                        if (r == 2 && i == 2) { sv[c] = s[c]; fv[c] = v; }
                    }
                    (void)fx0;
                    (void)fy0;
                }
            if (sum / 75 <= 40) continue;
            bad++;
            if (logged < 400 && bad <= 4)
            {
                logged++;
                Log("check: present %ld block (%d,%d) FO differs from S1 by %d (last written by %d, src %d, transition %d); centre S1 "
                    "%02x%02x%02x FO %02x%02x%02x", presents, bx, by, sum / 75, lastW[by][bx], blkSrc[by][bx], oldOn[by][bx], sv[2],
                    sv[1], sv[0], fv[2], fv[1], fv[0]);
            }
        }
    if (bad) Log("check: present %ld - %d blocks where FO is not S1", presents, bad);
}

// ---------------------------------------------------------------- output (called by display.cpp)
// The window gives us an image height; we use the largest multiple of 120 lines (P px per 5-px block). Returns P.
int UhdSetOutput(int imgH)
{
    int p = imgH / BY;
    if (p > 60) p = 60;
    EnterCriticalSection(&lk);
    if (p != P)
    {
        P = p;
        OW = BX * P;
        OH = BY * P;
        frameSurf = 0;                                           // set up again at the next frame draw
        FO = NULL;                                               // (a new section; size changes are rare)
        if (fodc) { DeleteDC(fodc); fodc = NULL; }
        for (int i = 0; i < nPlanes; i++) { VirtualFree(planes[i].c, 0, MEM_RELEASE); VirtualFree(planes[i].a, 0, MEM_RELEASE); }
        nPlanes = 0;
        for (int i = 0; i < nTwins; i++)                         // stage C twins are at the old K
        {
            if (twins[i].C) VirtualFree(twins[i].C, 0, MEM_RELEASE);
            twins[i].C = NULL;
            twins[i].bits = NULL;
        }
        nTwins = 0;
        for (int i = 0; i < 48; i++) if (comps[i].bits) FreeComp(&comps[i]);
        if (OLDK) { VirtualFree(OLDK, 0, MEM_RELEASE); OLDK = NULL; }
        memset(oldOn, 0, sizeof oldOn);
        Log("uhd: output %dx%d (%d px per 5-px block)", OW, OH, P);
        if (cfg.stagec) HiresSetP(P);
    }
    LeaveCriticalSection(&lk);
    return P;
}

// Brings FO in line with the engine's frame, then draws it at (x, y). False = not ready (stage A draws instead).
bool UhdPresent(HDC d, int x, int y)
{
    BYTE* f = frameBits;
    if (!f || P < B1) return false;
    if (!cfg.stagec && (!FO || !S1 || !frameSurf)) return false;
    EnterCriticalSection(&lk);
    double t0 = Ms();
    View1 v;
    bool known = frameSurf && FO && S1 && View(frameSurf, &v) && v.bits == f;
    if (!known && !(cfg.stagec && (frameBits0 == f && FO && S1 && !frameSurf ? true : StartFrameBits(f))))
    {
        LeaveCriticalSection(&lk);
        return false;
    }
    if (!pjobs) pjobs = (PJob*)VirtualAlloc(NULL, sizeof(PJob) * BX * BY, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pjobs) { LeaveCriticalSection(&lk); return false; }
    pjCount = 0;
    if (cfg.stagec && !OLD1)
        OLD1 = (BYTE*)VirtualAlloc(NULL, (SIZE_T)f1Stride * FH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (cfg.stagec && !OLDK)
    {
        OLDK = (BYTE*)VirtualAlloc(NULL, (SIZE_T)OW * OH * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        memset(oldOn, 0, sizeof oldOn);
    }
    LONG rebuilt = 0;
    for (int by = 0; by < BY; by++)
        for (int bx = 0; bx < BX; bx++)
        {
            bool same = true;
            for (int r = 0; r < B1 && same; r++)
            {
                SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * f1Stride + bx * B1 * 3;
                same = memcmp(f + o, S1 + o, B1 * 3) == 0;
            }
            if (same) { oldOn[by][bx] = false; continue; }
            bool followed = cfg.stagec && nTwins && ExplainDecide(bx, by, f, &pjobs[pjCount]);   // (before S1 takes the new pixels)
            if (followed) pjCount++;
            for (int r = 0; r < B1; r++)
            {
                SIZE_T o = (SIZE_T)(FH - 1 - (by * B1 + r)) * f1Stride + bx * B1 * 3;
                memcpy(S1 + o, f + o, B1 * 3);
            }
            if (followed) { blkSrc[by][bx] = 4; DirtyBlock(bx, by); lastW[by][bx] = 3; }
            else { pend[by][bx] = true; oldOn[by][bx] = false; }
            rebuilt++;
        }
    FlushAllJobs();                                              // the rest of the frame's pending blocks
    RunAllJobs();                                                // the K work of all of them, on the pool
    if (cfg.stagec && nTwins && !rebuilt)                        // a still frame: blocks scaled up while the frame and
        for (int by = 0; by < BY; by++)                          // the scene surface were half way through the same
            for (int bx = 0; bx < BX; bx++)                      // redraw (the text box) may match the twin now
                if (blkSrc[by][bx] == 0 || blkSrc[by][bx] == 4)          // (4: a transition's last step may be 1 level off)
                {
                    if (AdoptBlock(bx, by)) { blkSrc[by][bx] = adoptHd ? 1 : 3; adoptLate++; DirtyBlock(bx, by); lastW[by][bx] = 4; }
                    else blkSrc[by][bx] = 2;
                }
                else if (blkSrc[by][bx] == 3 && TwinHdReady(bx, by))   // 11-18: taken from a twin while its block was still
                {                                                        // scaled up; the twin has the 4x there now, and the
                    if (AdoptBlock(bx, by) && adoptHd)                   // frame's 1x pixels did not change, so nothing else
                    {                                                    // would take it again (menus kept 2~15% blurred)
                        blkSrc[by][bx] = 1;
                        adoptStale++;
                        DirtyBlock(bx, by);
                        lastW[by][bx] = 5;
                    }
                }
    if (cfg.stagec && cfg.check) CheckFO();
    nFrames++;
    if (rebuilt) { framesWithRebuild++; blocksRebuilt += rebuilt; }
    LONG dmode = InterlockedExchange(&foDirty, 0);
    bool draw = dmode != 0;
    if (dmode == 2 || (dmode == 1 && (dX1 - dX0 + 1) * (dY1 - dY0 + 1) * 2 > BX * BY)) BitBlt(d, x, y, OW, OH, fodc, 0, 0, SRCCOPY);
    else if (dmode == 1 && dX1 >= dX0)                           // only the blocks written since the last present
        BitBlt(d, x + dX0 * P, y + dY0 * P, (dX1 - dX0 + 1) * P, (dY1 - dY0 + 1) * P, fodc, dX0 * P, dY0 * P, SRCCOPY);
    dX0 = BX;
    dY0 = BY;
    dX1 = dY1 = -1;
    static LONG presentsAll, dumps;
    static double lastDump;
    bool dueDump = cfg.dumpfo >= 1000 ? Ms() - lastDump >= cfg.dumpfo : (cfg.dumpfo > 0 && ++presentsAll % cfg.dumpfo == 0);
    if (dueDump && dumps < ((SIZE_T)OW * OH * 3 <= (6u << 20) ? 80 : 24))              // test: the output frame as it is
    {                                                                                // (uhd_dumpfo >= 1000 = every that many ms)
        lastDump = Ms();
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\cap\\fo_%03ld.bmp", abDir, dumps++);
        HANDLE fh = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (fh != INVALID_HANDLE_VALUE)
        {
            DWORD rowB = ((DWORD)OW * 3 + 3) & ~3u, img = rowB * OH, wr;
            BITMAPFILEHEADER fhd = {};
            BITMAPINFOHEADER ih = {};
            fhd.bfType = 0x4D42;
            fhd.bfOffBits = sizeof fhd + sizeof ih;
            fhd.bfSize = fhd.bfOffBits + img;
            ih.biSize = sizeof ih;
            ih.biWidth = OW;
            ih.biHeight = -OH;
            ih.biPlanes = 1;
            ih.biBitCount = 24;
            WriteFile(fh, &fhd, sizeof fhd, &wr, NULL);
            WriteFile(fh, &ih, sizeof ih, &wr, NULL);
            for (int r = 0; r < OH; r++) WriteFile(fh, FO + (SIZE_T)r * OW * 3, rowB, &wr, NULL);   // OW*3 is a multiple of 4 here
            CloseHandle(fh);
            Log("uhd: output frame dumped to %ls", path);
            if (cfg.stagec)                                      // the engine's own 1x frame too (to compare)
            {
                swprintf_s(path, L"%s\\cap\\fo_%03ld_1x.bmp", abDir, dumps - 1);
                fh = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                if (fh != INVALID_HANDLE_VALUE)
                {
                    BITMAPFILEHEADER ff = {};
                    BITMAPINFOHEADER fi = {};
                    ff.bfType = 0x4D42;
                    ff.bfOffBits = sizeof ff + sizeof fi;
                    ff.bfSize = ff.bfOffBits + FW * 3 * FH;
                    fi.biSize = sizeof fi;
                    fi.biWidth = FW;
                    fi.biHeight = FH;                            // bottom-up, as the engine keeps it
                    fi.biPlanes = 1;
                    fi.biBitCount = 24;
                    WriteFile(fh, &ff, sizeof ff, &wr, NULL);
                    WriteFile(fh, &fi, sizeof fi, &wr, NULL);
                    for (int r = 0; r < FH; r++) WriteFile(fh, f + (SIZE_T)r * f1Stride, FW * 3, &wr, NULL);
                    CloseHandle(fh);
                }
            }
            if (cfg.stagec)                                      // and where each block came from (1 px per block):
            {                                                    // green 4x, red scaled up, dark red scaled up twice
                swprintf_s(path, L"%s\\cap\\fo_%03ld_map.bmp", abDir, dumps - 1);
                fh = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                if (fh != INVALID_HANDLE_VALUE)
                {
                    BYTE row[BX * 3];
                    BITMAPFILEHEADER mf = {};
                    BITMAPINFOHEADER mi = {};
                    mf.bfType = 0x4D42;
                    mf.bfOffBits = sizeof mf + sizeof mi;
                    mf.bfSize = mf.bfOffBits + BX * 3 * BY;
                    mi.biSize = sizeof mi;
                    mi.biWidth = BX;
                    mi.biHeight = -BY;
                    mi.biPlanes = 1;
                    mi.biBitCount = 24;
                    WriteFile(fh, &mf, sizeof mf, &wr, NULL);
                    WriteFile(fh, &mi, sizeof mi, &wr, NULL);
                    LONG cnt[5] = {};
                    for (int by = 0; by < BY; by++)
                    {
                        for (int bx = 0; bx < BX; bx++)
                        {
                            BYTE m = blkSrc[by][bx];
                            row[bx * 3] = m == 4 ? 220 : 0;      // 1 green: 4x / K text, 3 yellow: from a twin but
                            row[bx * 3 + 1] = m == 1 ? 200 : (m == 3 ? 200 : (m == 4 ? 200 : 0));   // scaled up there, 0 red /
                            row[bx * 3 + 2] = m == 0 ? 230 : (m == 2 ? 120 : (m == 3 ? 230 : 0));   // 2 dark red: frame scaled up
                            cnt[m > 4 ? 0 : m]++;
                        }
                        WriteFile(fh, row, sizeof row, &wr, NULL);
                    }
                    CloseHandle(fh);
                    Log("uhd: map %03ld: 4x %.1f%%, transition at K %.1f%%, twin scaled up %.1f%%, frame scaled up %.1f%%", dumps - 1,
                        cnt[1] * 100.0 / (BX * BY), cnt[4] * 100.0 / (BX * BY), cnt[3] * 100.0 / (BX * BY),
                        (cnt[0] + cnt[2]) * 100.0 / (BX * BY));
                }
            }
        }
    }
    double dt = Ms() - t0;
    if (draw) { msPresent += dt; if (dt > msPresentMax) msPresentMax = dt; nPresent++; }
    static double lastReport;
    double now = Ms();
    if (now - lastReport > 3000)
    {
        Log("uhd: %ld frames, %ld with rebuilt blocks (%ld blocks); mirrored draws %ld avg %.2f ms max %.1f ms; presents %ld avg %.1f ms max %.1f ms",
            nFrames, framesWithRebuild, blocksRebuilt, nDraw, nDraw ? msDraw / nDraw : 0.0, msDrawMax, nPresent,
            nPresent ? msPresent / nPresent : 0.0, msPresentMax);
        if (cfg.stagec)
        {
            char hs[320];
            HiresStats(hs, sizeof hs);
            Log("uhd: stage C twins %d; twin draws %ld (4x %ld, text %ld) %.1f ms in all, max %.1f ms; twin blocks scaled up %ld; "
                "frame blocks adopted %ld (late %ld, again at 4x %ld), missed %ld; transition blocks at K %ld, not %ld; composite copies %ld (4x %ld, blends %ld, other %ld, into twins %ld), used %ld, "
                "stale %ld (alpha moved %ld), %u MB | %s", nTwins, twinDraws, twinHd, twinPlane, msTwin, msTwinMax, twinUp, adopted, adoptLate, adoptStale, adoptMiss,
                transFollowed, transMissed, compCopies, compCopiesHd, compBlend, compNotPlain, compTwin, compUses, compStale, compAlphaMoved, (unsigned)(compBytes >> 20), hs);
            compCopies = compCopiesHd = compBlend = compNotPlain = compTwin = compUses = compStale = compAlphaMoved = 0;
            twinDraws = twinHd = twinPlane = twinUp = adopted = adoptMiss = adoptLate = adoptStale = transFollowed = transMissed = 0;
            msTwin = msTwinMax = 0;
        }
        lastReport = now;
        msDraw = msDrawMax = msPresent = msPresentMax = 0;
        nDraw = nPresent = nFrames = framesWithRebuild = blocksRebuilt = 0;
    }
    LeaveCriticalSection(&lk);
    return true;
}

void UhdRepaint() { DirtyAll(); }

// ---------------------------------------------------------------- install
static bool Hook(DWORD rva, const BYTE* expect, int n, void* hook, BYTE** tramp, const char* name)
{
    if (memcmp((BYTE*)sys + rva, expect, n) != 0 || !InlineHook(rva, n, hook, tramp))
    {
        Log("uhd: %s (%05lx) NOT hooked - unexpected bytes", name, rva);
        return false;
    }
    return true;
}

void InstallUhd()
{
    InitializeCriticalSection(&lk);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = GW;
    bi.bmiHeader.biHeight = -GH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = NULL;
    gdc = CreateCompatibleDC(NULL);
    HBITMAP gbmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    gbits = (BYTE*)bits;
    SelectObject(gdc, gbmp);
    const BYTE eChar[7] = { 0x6A, 0xFF, 0x68, 0xBC, 0xFE, 0x0B, 0x10 };      // push -1; push 0x100bfebc
    const BYTE eCopy[5] = { 0x83, 0xEC, 0x08, 0x85, 0xC9 };                  // sub esp,8; test ecx,ecx
    const BYTE eFill[8] = { 0x83, 0xEC, 0x14, 0x55, 0x8B, 0x6C, 0x24, 0x1C }; // sub esp,0x14; push ebp; mov ebp,[esp+0x1c]
    const BYTE eLayer[7] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x10 };     // mov eax,[esp+4]; sub esp,0x10
    bool ok = Hook(0x5ca10, eChar, 7, (void*)HookDrawChar, &trDrawChar, "DrawChar")
              & Hook(0x52480, eCopy, 5, (void*)HookCopy, &trCopy, "CopyRect")
              & Hook(0x03a90, eFill, 8, (void*)HookFill, &trFill, "FillRect")
              & Hook(0x56450, eLayer, 7, (void*)HookLayer, &trLayer, "DrawLayer");
    Log("uhd: stage B (dialogue at the output resolution) %s", ok ? "installed" : "INCOMPLETE");
    if (cfg.stagec || cfg.pictrace)
    {
        const BYTE eLoad[7] = { 0x6A, 0xFF, 0x68, 0x5B, 0x0F, 0x0C, 0x10 };  // push -1; push 0x100c0f5b
        bool lh = Hook(0x4d290, eLoad, 7, (void*)HookLoad, &trLoad, "LoadPicture");
        Log("uhd: picture loads %s", lh ? "followed" : "NOT followed");
        if (cfg.stagec)
        {
            wchar_t dir[MAX_PATH];
            if (cfg.hiresDir[0]) lstrcpynW(dir, cfg.hiresDir, MAX_PATH);
            else swprintf_s(dir, L"%s\\..\\hires", abDir);
            HiresInit(dir);
            const BYTE eCompM[7] = { 0x8B, 0x4C, 0x24, 0x08, 0x83, 0xEC, 0x0C };  // mov ecx,[esp+8]; sub esp,0xc
            const BYTE eCompN[7] = { 0x8B, 0x4C, 0x24, 0x20, 0x83, 0xEC, 0x0C };  // mov ecx,[esp+0x20]; sub esp,0xc
            bool cm = Hook(0x4f430, eCompM, 7, (void*)HookCompM, &trCompM, "CopySurface MMX");
            bool cn = Hook(0x4fdb0, eCompN, 7, (void*)HookCompN, &trCompN, "CopySurface");
            Log("uhd: stage C (pictures at 4x from %ls) %s; composites %s", dir, lh ? "installed" : "NOT installed",
                cm && cn ? "followed" : "NOT followed");
        }
    }
    if (cfg.pictrace)
    {
        const BYTE eBlend[7] = { 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x41, 0x04 };  // mov ecx,[esp+8]; mov eax,[ecx+4]
        bool bm = Hook(0x4e690, eBlend, 7, (void*)HookBlendM, &trBlendM, "BlendSurface MMX");
        bool bn = Hook(0x4ed70, eBlend, 7, (void*)HookBlendN, &trBlendN, "BlendSurface");
        Log("uhd: surface blend trace %s", bm && bn ? "on" : "INCOMPLETE");
    }
}
