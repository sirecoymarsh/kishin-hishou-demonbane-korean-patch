// khd4k.dll stage C (11-07): high-resolution pictures for the 4K output.
//
// The pictures were made 4x (or 2x for the 1920-class backgrounds) outside the game (Y:\AI\khd-4k-art, per picture the
// upscaler that reproduced the original best). They live in a folder (autobattle.ini uhd_hires, default <game>\hires)
// with index.tsv in cp932, one line per picture:
//   name \t file \t w1 \t h1 \t scale \t alpha \t sig
//   name  the archive name as the engine asks for it, normalised: ASCII lower case, '\' -> '/', no extension
//   sig   12x9 grid of the 1x picture (centres of the cells), B G R each, 324 bytes as hex - the engine's own decode of
//         the same file may differ by a little (another JPEG decoder), so the check is a mean difference <= 8
// The engine loads a picture with 0x4d290 (name -> surface); we remember surface -> name and, the first time the surface
// is drawn from, check that it still holds that picture (sig) and decode the file (WIC, 32-bit BGRA). For drawing at the
// output scale K the picture is also kept resampled to K once (Sc(w1) x Sc(h1)), so a draw is a plain blend.
// Column 8 (optional) = the picture in bands inside a pack file (column 2 then names the pack):
// "offset+colour length[+alpha length],..." -
// see "packs" below; without it the file is one JPEG / PNG.
#include "common.h"
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

struct Band { unsigned long long off; DWORD len, alen; };   // colour (JPEG, or PNG with alpha), then 8-bit alpha (PNG) if alen
struct HdPic
{
    char name[128];
    wchar_t file[128];
    int w1, h1, s, alpha;
    BYTE sig[324];
    BYTE* px;            // BGRA at s, top-down (one-file pictures, only while resampling)
    BYTE* kpx;           // BGRA at K (kw x kh), top-down
    int kP, kw, kh;      // the P it was resampled for
    DWORD used;
    Band* bands;         // a pack: the picture's bands (nBands), else NULL
    int nBands;
    volatile LONG busy;  // bands still being made into kpx
    volatile LONG bad;   // a band failed
    HANDLE ready;        // manual reset, set when busy reaches 0
    double t0;           // when its bands were queued
};
static int nWorkers;                 // band threads (packs)
static volatile LONG curP;           // the output P (UhdSetOutput), for starting bands at load time
static void StartWorkers();
static bool StartBands(HdPic& h, int P);
static HdPic* pics;
static int nPics;
static wchar_t hiresDir[MAX_PATH];
struct Assoc { DWORD surf; BYTE* bits; int pic; int state; DWORD fp; };   // state 0 = check at first use, 1 = good, -1 = not it;
                                                                          // fp = the surface's fingerprint when it was checked
static Assoc assoc[1024];
static LONG assocNext;
static CRITICAL_SECTION hlk;
static IWICImagingFactory* wic;
static SIZE_T cacheBytes;
static const SIZE_T CACHE_MAX = (SIZE_T)360 << 20;
static LONG nDecoded, nDecodeFail, nSigFail, nHits, nChanged;
static double msDec, msDecMax, msRes, msResMax;   // decode / resample to K times (the first draw of a picture waits)

static double NowMs()
{
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

// ---------------------------------------------------------------- names
// ASCII lower case and '\' -> '/' outside double-byte characters (a cp932 trail byte can be 'A'..'Z' or '\'), the
// extension cut after the last '.' of the last part
void HiresNormalise(const char* in, char* out, int cap)
{
    int n = 0, lastSlash = -1, lastDot = -1;
    for (int i = 0; in[i] && n < cap - 1; i++)
    {
        BYTE c = (BYTE)in[i];
        if (IsDBCSLeadByteEx(932, c) && in[i + 1])
        {
            if (n + 2 >= cap) break;
            out[n++] = (char)c;
            out[n++] = in[++i];
            continue;
        }
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (BYTE)(c - 'A' + 'a');
        if (c == '/') lastSlash = n;
        if (c == '.') lastDot = n;
        out[n++] = (char)c;
    }
    if (lastDot > lastSlash) n = lastDot;
    out[n] = 0;
}

static int HexByte(const char* p)
{
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    return v(p[0]) * 16 + v(p[1]);
}

// ---------------------------------------------------------------- index
void HiresInit(const wchar_t* dir)
{
    InitializeCriticalSection(&hlk);
    lstrcpynW(hiresDir, dir, MAX_PATH);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\index.tsv", dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { Log("hires: no index at %ls - stage C has no pictures", path); return; }
    DWORD size = GetFileSize(f, NULL), got = 0;
    char* text = (char*)VirtualAlloc(NULL, size + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ReadFile(f, text, size, &got, NULL);
    CloseHandle(f);
    text[got] = 0;
    int lines = 0;
    for (DWORD i = 0; i < got; i++) if (text[i] == '\n') lines++;
    pics = (HdPic*)VirtualAlloc(NULL, sizeof(HdPic) * (lines + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    char* p = text;
    while (*p)
    {
        char* e = strchr(p, '\n');
        if (e) *e = 0;
        char* col[8] = {};
        int nc = 0;
        for (char* q = p; nc < 8; )
        {
            col[nc++] = q;
            char* t = strchr(q, '\t');
            if (!t) break;
            *t = 0;
            q = t + 1;
        }
        if (nc >= 7 && strlen(col[6]) >= 648)
        {
            HdPic& h = pics[nPics];
            HiresNormalise(col[0], h.name, sizeof h.name);
            MultiByteToWideChar(932, 0, col[1], -1, h.file, 128);
            h.w1 = atoi(col[2]);
            h.h1 = atoi(col[3]);
            h.s = atoi(col[4]);
            h.alpha = atoi(col[5]);
            for (int i = 0; i < 324; i++) h.sig[i] = (BYTE)HexByte(col[6] + i * 2);
            h.bands = NULL;
            h.nBands = 0;
            if (nc == 8 && col[7][0])
            {
                int nb = 1;
                for (const char* q = col[7]; *q; q++) if (*q == ',') nb++;
                h.bands = (Band*)HeapAlloc(GetProcessHeap(), 0, sizeof(Band) * nb);
                char* q = col[7];
                while (h.bands && *q && h.nBands < nb)
                {
                    char* plus = NULL;
                    h.bands[h.nBands].off = _strtoui64(q, &plus, 10);
                    if (!plus || *plus != '+') break;
                    char* end = NULL;
                    h.bands[h.nBands].len = strtoul(plus + 1, &end, 10);
                    h.bands[h.nBands].alen = 0;
                    if (end && *end == '+') h.bands[h.nBands].alen = strtoul(end + 1, &end, 10);
                    h.nBands++;
                    q = end && *end == ',' ? end + 1 : end;
                    if (!q) break;
                }
                int H = h.h1 * h.s;
                if (h.nBands != (H + 255) / 256) { Log("hires: %s - %d bands, want %d", h.name, h.nBands, (H + 255) / 256); h.nBands = 0; }
            }
            if (h.w1 > 0 && h.h1 > 0 && (h.s == 2 || h.s == 4)) nPics++;
        }
        if (!e) break;
        p = e + 1;
    }
    VirtualFree(text, 0, MEM_RELEASE);
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    int packed = 0;
    for (int i = 0; i < nPics; i++) if (pics[i].nBands) packed++;
    if (packed) StartWorkers();
    Log("hires: %d pictures in %ls (WIC %s), %d in bands (%d threads)", nPics, dir, SUCCEEDED(hr) ? "ready" : "MISSING", packed,
        nWorkers);
}

static int FindPic(const char* norm)
{
    for (int i = 0; i < nPics; i++) if (strcmp(pics[i].name, norm) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------- the engine loaded a picture into a surface
void HiresOnLoad(DWORD surf, const char* name)
{
    if (!nPics || !name) return;
    char norm[128];
    HiresNormalise(name, norm, sizeof norm);
    int pi = FindPic(norm);
    EnterCriticalSection(&hlk);
    Assoc* a = NULL;
    for (int i = 0; i < 1024 && !a; i++) if (assoc[i].surf == surf) a = &assoc[i];
    if (!a) a = &assoc[(assocNext++) % 1024];
    a->surf = surf;
    a->bits = NULL;
    a->pic = pi;
    a->state = pi >= 0 ? 0 : -1;
    int P = curP;
    if (pi >= 0 && pics[pi].nBands && P > 5) StartBands(pics[pi], P);   // before the engine has even decoded it
    LeaveCriticalSection(&hlk);
}

// ---------------------------------------------------------------- decoding
static void Evict(SIZE_T need, const HdPic* keep)
{
    while (cacheBytes + need > CACHE_MAX)
    {
        int old = -1;
        for (int i = 0; i < nPics; i++)
            if (&pics[i] != keep && !pics[i].busy && (pics[i].px || pics[i].kpx) && (old < 0 || pics[i].used < pics[old].used))
                old = i;
        if (old < 0) return;
        HdPic& h = pics[old];
        if (h.px) { VirtualFree(h.px, 0, MEM_RELEASE); cacheBytes -= (SIZE_T)h.w1 * h.s * h.h1 * h.s * 4; h.px = NULL; }
        if (h.kpx) { VirtualFree(h.kpx, 0, MEM_RELEASE); cacheBytes -= (SIZE_T)h.kw * h.kh * 4; h.kpx = NULL; }
    }
}

static bool Decode(HdPic& h)
{
    if (h.px) return true;
    if (!wic) return false;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\%s", hiresDir, h.file);
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* fr = NULL;
    IWICFormatConverter* cv = NULL;
    bool ok = false;
    UINT w = 0, ht = 0;
    if (SUCCEEDED(wic->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(cv->GetSize(&w, &ht)) && (int)w == h.w1 * h.s && (int)ht == h.h1 * h.s)
    {
        SIZE_T bytes = (SIZE_T)w * ht * 4;
        Evict(bytes, &h);
        h.px = (BYTE*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (h.px && SUCCEEDED(cv->CopyPixels(NULL, w * 4, (UINT)bytes, h.px))) { cacheBytes += bytes; ok = true; }
        else if (h.px) { VirtualFree(h.px, 0, MEM_RELEASE); h.px = NULL; }
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (ok) InterlockedIncrement(&nDecoded);
    else if (InterlockedIncrement(&nDecodeFail) <= 20) Log("hires: could not decode %ls (%ux%u, want %dx%d)", path, w, ht, h.w1 * h.s, h.h1 * h.s);
    return ok;
}

// resampled to the output scale once: output pixel X covers 1x [X*5/P, (X+1)*5/P) -> in the picture [X*5s/P, ...)
// - box average over that footprint (the picture is denser than the output, 4/3.6 or more), alpha straight
static bool ToK(HdPic& h, int P)
{
    if (h.kpx && h.kP == P) return true;
    if (h.kpx) { VirtualFree(h.kpx, 0, MEM_RELEASE); cacheBytes -= (SIZE_T)h.kw * h.kh * 4; h.kpx = NULL; }
    int kw = h.w1 * P / 5, kh = h.h1 * P / 5, W = h.w1 * h.s, H = h.h1 * h.s;
    SIZE_T bytes = (SIZE_T)kw * kh * 4;
    Evict(bytes, &h);
    h.kpx = (BYTE*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!h.kpx) return false;
    cacheBytes += bytes;
    h.kw = kw;
    h.kh = kh;
    h.kP = P;
    // fixed point 16.16 footprint per output pixel
    const int fx = (int)(((long long)W << 16) / kw), fy = (int)(((long long)H << 16) / kh);
    for (int Y = 0; Y < kh; Y++)
    {
        int y0 = (int)(((long long)Y * fy) >> 16), y1 = (int)(((long long)(Y + 1) * fy + 0xFFFF) >> 16);
        if (y1 > H) y1 = H;
        if (y1 <= y0) y1 = y0 + 1;
        BYTE* d = h.kpx + (SIZE_T)Y * kw * 4;
        for (int X = 0; X < kw; X++, d += 4)
        {
            int x0 = (int)(((long long)X * fx) >> 16), x1 = (int)(((long long)(X + 1) * fx + 0xFFFF) >> 16);
            if (x1 > W) x1 = W;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned s0 = 0, s1 = 0, s2 = 0, s3 = 0, n = 0;
            for (int y = y0; y < y1; y++)
            {
                const BYTE* p = h.px + ((SIZE_T)y * W + x0) * 4;
                for (int x = x0; x < x1; x++, p += 4) { s0 += p[0]; s1 += p[1]; s2 += p[2]; s3 += p[3]; n++; }
            }
            d[0] = (BYTE)((s0 + n / 2) / n);
            d[1] = (BYTE)((s1 + n / 2) / n);
            d[2] = (BYTE)((s2 + n / 2) / n);
            d[3] = (BYTE)((s3 + n / 2) / n);
        }
    }
    return true;
}

// ---------------------------------------------------------------- packs: bands decoded at once by a pool of threads
// A picture in a pack is stored in bands of STRIP rows of the 4x picture, each band with OVER rows more above and below
// (clamped to the picture) so that every output row's footprint lies inside the band that owns it: output row Y belongs
// to band i when its footprint starts in [i*STRIP, (i+1)*STRIP). Each band is its own JPEG / PNG; a worker reads it,
// decodes it (WIC, its own factory) and resamples its rows straight into the picture at K - the full 4x picture is
// never held. The bands of a picture start when the engine loads it (if the output size is known by then), so by its
// first draw some or all of the work is done; the draw waits for the rest.
static const int STRIP = 256, OVER = 8;
struct Job { int pic, band; };
static Job jobQ[8192];
static LONG jobHead, jobTail;
static CRITICAL_SECTION jlk;
static HANDLE jsem;
static LONG nMade, nMadeFail;
static double msMake, msMakeMax, msWait, msWaitMax;
static LONG nWaits;

static void BandRows(const HdPic& h, int i, int* r0, int* r1)
{
    int H = h.h1 * h.s;
    *r0 = i * STRIP - OVER;
    *r1 = (i + 1) * STRIP + OVER;
    if (*r0 < 0) *r0 = 0;
    if (*r1 > H) *r1 = H;
}

// the band's decoded rows (BGRA, W x (r1 - r0)) -> the output rows it owns, box average like ToK
static void ResampleBand(HdPic& h, int i, const BYTE* buf, int r0, int r1)
{
    int W = h.w1 * h.s, H = h.h1 * h.s, kw = h.kw, kh = h.kh;
    const int fx = (int)(((long long)W << 16) / kw), fy = (int)(((long long)H << 16) / kh);
    int own0 = i * STRIP, own1 = (i + 1) * STRIP;
    auto y0of = [&](int Y) { return (int)(((long long)Y * fy) >> 16); };
    int Ya = (int)(((long long)own0 << 16) / fy);
    while (Ya > 0 && y0of(Ya - 1) >= own0) Ya--;
    while (Ya < kh && y0of(Ya) < own0) Ya++;
    for (int Y = Ya; Y < kh; Y++)
    {
        int y0 = y0of(Y);
        if (y0 >= own1) break;
        int y1 = (int)(((long long)(Y + 1) * fy + 0xFFFF) >> 16);
        if (y1 > H) y1 = H;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > r1) y1 = r1;                                    // (OVER covers footprints of 8 rows - P >= 3)
        BYTE* d = h.kpx + (SIZE_T)Y * kw * 4;
        for (int X = 0; X < kw; X++, d += 4)
        {
            int x0 = (int)(((long long)X * fx) >> 16), x1 = (int)(((long long)(X + 1) * fx + 0xFFFF) >> 16);
            if (x1 > W) x1 = W;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned s0 = 0, s1 = 0, s2 = 0, s3 = 0, n = 0;
            for (int y = y0; y < y1; y++)
            {
                const BYTE* p = buf + ((SIZE_T)(y - r0) * W + x0) * 4;
                for (int x = x0; x < x1; x++, p += 4) { s0 += p[0]; s1 += p[1]; s2 += p[2]; s3 += p[3]; n++; }
            }
            d[0] = (BYTE)((s0 + n / 2) / n);
            d[1] = (BYTE)((s1 + n / 2) / n);
            d[2] = (BYTE)((s2 + n / 2) / n);
            d[3] = (BYTE)((s3 + n / 2) / n);
        }
    }
}

struct WorkerState
{
    IWICImagingFactory* f;
    BYTE* blob;
    DWORD blobCap;
    BYTE* buf;
    SIZE_T bufCap;
    HANDLE fh;
    wchar_t fhName[128];
};

static bool DoBand(WorkerState& ws, HdPic& h, int i)
{
    if (!ws.f) return false;
    if (ws.fh == INVALID_HANDLE_VALUE || wcscmp(ws.fhName, h.file) != 0)
    {
        if (ws.fh != INVALID_HANDLE_VALUE) CloseHandle(ws.fh);
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\%s", hiresDir, h.file);
        ws.fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
        lstrcpynW(ws.fhName, h.file, 128);
        if (ws.fh == INVALID_HANDLE_VALUE) return false;
    }
    const Band& b = h.bands[i];
    DWORD total = b.len + b.alen;
    if (total > ws.blobCap)
    {
        if (ws.blob) VirtualFree(ws.blob, 0, MEM_RELEASE);
        ws.blobCap = (total + 0xFFFF) & ~0xFFFFu;
        ws.blob = (BYTE*)VirtualAlloc(NULL, ws.blobCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ws.blob) { ws.blobCap = 0; return false; }
    }
    OVERLAPPED ov = {};
    ov.Offset = (DWORD)b.off;
    ov.OffsetHigh = (DWORD)(b.off >> 32);
    DWORD got = 0;
    if (!ReadFile(ws.fh, ws.blob, total, &got, &ov) || got != total) return false;
    int r0, r1, W = h.w1 * h.s;
    BandRows(h, i, &r0, &r1);
    SIZE_T need = (SIZE_T)W * (r1 - r0) * 4, aneed = b.alen ? (SIZE_T)W * (r1 - r0) : 0;
    if (need + aneed > ws.bufCap)
    {
        if (ws.buf) VirtualFree(ws.buf, 0, MEM_RELEASE);
        ws.bufCap = (need + aneed + 0xFFFF) & ~(SIZE_T)0xFFFF;
        ws.buf = (BYTE*)VirtualAlloc(NULL, ws.bufCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ws.buf) { ws.bufCap = 0; return false; }
    }
    IWICStream* st = NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* fr = NULL;
    IWICFormatConverter* cv = NULL;
    UINT w = 0, ht = 0;
    bool ok = SUCCEEDED(ws.f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory(ws.blob, b.len)) &&
              SUCCEEDED(ws.f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(ws.f->CreateFormatConverter(&cv)) &&
              SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(cv->GetSize(&w, &ht)) && (int)w == W && (int)ht == r1 - r0 &&
              SUCCEEDED(cv->CopyPixels(NULL, w * 4, (UINT)need, ws.buf));
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (st) st->Release();
    if (ok && b.alen)                                            // the alpha, 8-bit grey, into the fourth byte
    {
        st = NULL;
        dec = NULL;
        fr = NULL;
        cv = NULL;
        BYTE* a8 = ws.buf + need;
        ok = SUCCEEDED(ws.f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory(ws.blob + b.len, b.alen)) &&
             SUCCEEDED(ws.f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnDemand, &dec)) &&
             SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(ws.f->CreateFormatConverter(&cv)) &&
             SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
             SUCCEEDED(cv->GetSize(&w, &ht)) && (int)w == W && (int)ht == r1 - r0 &&
             SUCCEEDED(cv->CopyPixels(NULL, w, (UINT)aneed, a8));
        if (cv) cv->Release();
        if (fr) fr->Release();
        if (dec) dec->Release();
        if (st) st->Release();
        if (ok)
            for (SIZE_T k = 0; k < aneed; k++) ws.buf[k * 4 + 3] = a8[k];
    }
    if (!ok) return false;
    ResampleBand(h, i, ws.buf, r0, r1);
    return true;
}

static DWORD WINAPI BandWorker(LPVOID)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WorkerState ws = {};
    ws.fh = INVALID_HANDLE_VALUE;
    CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ws.f));
    while (WaitForSingleObject(jsem, INFINITE) == WAIT_OBJECT_0)
    {
        EnterCriticalSection(&jlk);
        Job j = jobQ[jobHead % 8192];
        jobHead++;
        LeaveCriticalSection(&jlk);
        HdPic& h = pics[j.pic];
        bool ok = false;
        __try { ok = DoBand(ws, h, j.band); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (!ok) InterlockedExchange(&h.bad, 1);
        if (InterlockedDecrement(&h.busy) == 0)
        {
            double dt = NowMs() - h.t0;
            EnterCriticalSection(&jlk);
            if (h.bad) nMadeFail++;
            else
            {
                nMade++;
                msMake += dt;
                if (dt > msMakeMax) msMakeMax = dt;
            }
            LeaveCriticalSection(&jlk);
            SetEvent(h.ready);
        }
    }
    return 0;
}

static void StartWorkers()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    nWorkers = (int)si.dwNumberOfProcessors - 2;
    if (nWorkers < 2) nWorkers = 2;
    if (nWorkers > 8) nWorkers = 8;
    InitializeCriticalSection(&jlk);
    jsem = CreateSemaphoreW(NULL, 0, 8192, NULL);
    for (int i = 0; i < nWorkers; i++)
    {
        HANDLE t = CreateThread(NULL, 0, BandWorker, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

// under hlk: the picture's bands queued for P (K buffer made now); false = no memory / no room in the queue
static bool StartBands(HdPic& h, int P)
{
    if (h.busy) return true;                                     // already on its way (for h.kP)
    if (h.kpx && h.kP == P && !h.bad) return true;               // ready
    if (h.kpx) { VirtualFree(h.kpx, 0, MEM_RELEASE); cacheBytes -= (SIZE_T)h.kw * h.kh * 4; h.kpx = NULL; }
    int kw = h.w1 * P / 5, kh = h.h1 * P / 5;
    SIZE_T bytes = (SIZE_T)kw * kh * 4;
    Evict(bytes, &h);
    EnterCriticalSection(&jlk);
    bool room = jobTail - jobHead + h.nBands <= 8192;
    LeaveCriticalSection(&jlk);
    if (!room) return false;
    h.kpx = (BYTE*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!h.kpx) return false;
    cacheBytes += bytes;
    h.kw = kw;
    h.kh = kh;
    h.kP = P;
    h.bad = 0;
    h.t0 = NowMs();
    if (!h.ready) h.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    ResetEvent(h.ready);
    h.busy = h.nBands;
    EnterCriticalSection(&jlk);
    for (int i = 0; i < h.nBands; i++)
    {
        jobQ[jobTail % 8192].pic = (int)(&h - pics);
        jobQ[jobTail % 8192].band = i;
        jobTail++;
    }
    LeaveCriticalSection(&jlk);
    ReleaseSemaphore(jsem, h.nBands, NULL);
    return true;
}

void HiresSetP(int P) { InterlockedExchange(&curP, P); }

// ---------------------------------------------------------------- the picture behind a surface, ready at K
// the surface's 8-bit alpha (embedded at +0x5C) when it has one of the same size, else NULL
static const BYTE* AlphaOf(DWORD surf, int w, int ht, int* stride)
{
    __try
    {
        DWORD a = surf + 0x5C;
        BITMAPINFOHEADER* bi = *(BITMAPINFOHEADER**)(UINT_PTR)(a + 4);
        BYTE* bits = *(BYTE**)(UINT_PTR)(a + 0xC);
        if ((UINT_PTR)bi < 0x10000 || bi->biSize != 40 || !bits || bi->biBitCount != 8 || bi->biWidth != w ||
            (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight) != ht)
            return NULL;
        *stride = *(int*)(UINT_PTR)(a + 0x4C);
        return bits;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
}

// the 12x9 grid against the index: mean difference <= 8 over the samples that can be seen (the colour under a fully
// transparent pixel is whatever the decoder left there - the engine's and PIL's differ on button pictures)
static bool SigOk(const HdPic& h, const BYTE* bits, int stride, int w, int ht, const BYTE* al, int as)
{
    if (w != h.w1 || ht != h.h1) return false;
    unsigned diff = 0, used = 0;
    int k = 0;
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 12; i++, k += 3)
        {
            int x = (2 * i + 1) * w / 24, y = (2 * j + 1) * ht / 18;
            if (al && al[(SIZE_T)(ht - 1 - y) * as + x] < 32) continue;
            const BYTE* p = bits + (SIZE_T)(ht - 1 - y) * stride + x * 3;       // bottom-up
            for (int c = 0; c < 3; c++) diff += (unsigned)abs((int)p[c] - (int)h.sig[k + c]);
            used++;
        }
    return diff <= 8u * 3u * used;
}

// 32x24 samples of the surface (colour and alpha) - the engine changes some loaded pictures in place (PRESS START blinks
// by rewriting its button pictures), and a 4x picture is only right while the surface still holds what was checked
static DWORD Fingerprint(const BYTE* bits, int stride, int w, int ht, const BYTE* al, int as)
{
    DWORD x = 2166136261u;
    for (int j = 0; j < 24; j++)
    {
        int y = (2 * j + 1) * ht / 48;
        const BYTE* row = bits + (SIZE_T)(ht - 1 - y) * stride;
        const BYTE* arow = al ? al + (SIZE_T)(ht - 1 - y) * as : NULL;
        for (int i = 0; i < 32; i++)
        {
            int xx = (2 * i + 1) * w / 64;
            DWORD v = row[xx * 3] | (row[xx * 3 + 1] << 8) | (row[xx * 3 + 2] << 16) | ((arow ? arow[xx] : 0) << 24);
            x = (x ^ v) * 16777619u;
        }
    }
    return x;
}

// a surface the engine draws from: if it holds a picture we have at 4x, its BGRA at K (kw x kh); else NULL
const BYTE* HiresAtK(DWORD surf, BYTE* bits, int stride, int w, int ht, int P, int* kw, int* kh, int* alpha)
{
    if (!nPics || !surf) return NULL;
    EnterCriticalSection(&hlk);
    const BYTE* out = NULL;
    Assoc* a = NULL;
    for (int i = 0; i < 1024 && !a; i++) if (assoc[i].surf == surf) a = &assoc[i];
    if (a && a->pic >= 0 && a->state >= 0)
    {
        HdPic& h = pics[a->pic];
        if (a->state == 0 || a->bits != bits)
        {
            a->bits = bits;
            int as = 0;
            const BYTE* al = AlphaOf(surf, w, ht, &as);
            a->state = SigOk(h, bits, stride, w, ht, al, as) ? 1 : -1;
            if (a->state == 1) a->fp = Fingerprint(bits, stride, w, ht, al, as);
            if (a->state < 0 && InterlockedIncrement(&nSigFail) <= 30)
            {
                unsigned diff = 0, used = 0, worst = 0;
                int wk = 0, wx = 0, wy = 0;
                for (int j = 0, k = 0; j < 9 && w == h.w1 && ht == h.h1; j++)
                    for (int i = 0; i < 12; i++, k += 3)
                    {
                        int x = (2 * i + 1) * w / 24, y = (2 * j + 1) * ht / 18;
                        if (al && al[(SIZE_T)(ht - 1 - y) * as + x] < 32) continue;
                        const BYTE* q = bits + (SIZE_T)(ht - 1 - y) * stride + x * 3;
                        unsigned dd = 0;
                        for (int c = 0; c < 3; c++) dd += (unsigned)abs((int)q[c] - (int)h.sig[k + c]);
                        diff += dd;
                        used++;
                        if (dd > worst) { worst = dd; wk = k; wx = x; wy = y; }
                    }
                const BYTE* q = w == h.w1 && ht == h.h1 ? bits + (SIZE_T)(ht - 1 - wy) * stride + wx * 3 : NULL;
                Log("hires: %s - the surface does not look like our picture (sig: %dx%d vs %dx%d, %u samples seen, mean %.1f; worst at "
                    "(%d,%d) engine %02x%02x%02x a %d, ours %02x%02x%02x)", h.name, w, ht, h.w1, h.h1, used, used ? diff / 3.0 / used : 0.0,
                    wx, wy, q ? q[2] : 0, q ? q[1] : 0, q ? q[0] : 0, al ? al[(SIZE_T)(ht - 1 - wy) * as + wx] : -1, h.sig[wk + 2],
                    h.sig[wk + 1], h.sig[wk]);
            }
        }
        else if (a->state == 1)                                  // still what was checked?
        {
            int as = 0;
            const BYTE* al = AlphaOf(surf, w, ht, &as);
            if (Fingerprint(bits, stride, w, ht, al, as) != a->fp)
            {
                nChanged++;
                LeaveCriticalSection(&hlk);
                return NULL;                                     // drawn at 1x until it holds our picture again
            }
        }
        bool ready = h.kpx && h.kP == P && !h.busy && !h.bad;
        if (a->state == 1 && !ready && h.nBands)
        {
            double w0 = NowMs();
            for (int tries = 0; tries < 3 && !ready; tries++)
            {
                if (!StartBands(h, P)) break;
                if (h.busy)
                {
                    HANDLE ev = h.ready;
                    LeaveCriticalSection(&hlk);                      // the workers do not need it; nobody else draws
                    WaitForSingleObject(ev, 10000);
                    EnterCriticalSection(&hlk);
                }
                if (h.busy) break;                                   // (10 s - something is wrong; draw 1x)
                if (h.bad)
                {
                    a->state = -1;                                   // do not try that picture again
                    if (InterlockedIncrement(&nDecodeFail) <= 20) Log("hires: %s - a band could not be made", h.name);
                    break;
                }
                ready = h.kpx && h.kP == P;
            }
            double dw = NowMs() - w0;
            if (dw > 0.5) { nWaits++; msWait += dw; if (dw > msWaitMax) msWaitMax = dw; }
        }
        double t0 = NowMs(), t1 = 0;
        if (a->state == 1 && !ready && !h.nBands && Decode(h) && (t1 = NowMs(), ToK(h, P)))
        {
            double t2 = NowMs();
            msDec += t1 - t0;
            msRes += t2 - t1;
            if (t1 - t0 > msDecMax) msDecMax = t1 - t0;
            if (t2 - t1 > msResMax) msResMax = t2 - t1;
            // the full picture is only needed to resample again for another P (a window size change) - decoding it
            // once more then costs less than keeping 4x everything in a 32-bit process
            VirtualFree(h.px, 0, MEM_RELEASE);
            cacheBytes -= (SIZE_T)h.w1 * h.s * h.h1 * h.s * 4;
            h.px = NULL;
            ready = true;
        }
        if (a->state == 1 && ready)
        {
            h.used = GetTickCount();
            *kw = h.kw;
            *kh = h.kh;
            *alpha = h.alpha;
            out = h.kpx;
            InterlockedIncrement(&nHits);
        }
    }
    LeaveCriticalSection(&hlk);
    return out;
}

void HiresStats(char* buf, int cap)
{
    if (nWorkers)
        sprintf_s(buf, cap, "hires: %ld draws from 4x pictures, %ld made in bands (avg %.0f ms, max %.0f), %ld draws waited (avg %.0f ms, "
                  "max %.0f), %ld failed, %ld sig misses, %ld draws from a changed surface, cache %u MB", nHits, nMade,
                  nMade ? msMake / nMade : 0.0, msMakeMax, nWaits, nWaits ? msWait / nWaits : 0.0, msWaitMax, nDecodeFail + nMadeFail,
                  nSigFail, nChanged, (unsigned)(cacheBytes >> 20));
    else
        sprintf_s(buf, cap, "hires: %ld draws from 4x pictures, %ld decoded (avg %.0f ms, max %.0f; to K avg %.0f, max %.0f), %ld failed, "
                  "%ld sig misses, cache %u MB", nHits, nDecoded, nDecoded ? msDec / nDecoded : 0.0, msDecMax,
                  nDecoded ? msRes / nDecoded : 0.0, msResMax, nDecodeFail, nSigFail, (unsigned)(cacheBytes >> 20));
}
