// khd4k.dll probe (uhd_probe=1): who reads the dialogue layer the engine stamps its glyphs into?
// system.dll+0x5ca10 = DrawChar(layer, hdc, x, y, pChar, color) (stdcall, ret 0x18) stamps one glyph (shadow 010101 at
// +2,+2 first, then the colour) into the surface at layer+0xB4 (colour 24-bit + a separate alpha plane). GDI locks DIB
// memory (no guard pages), so after a stamp we put a hardware data breakpoint (DR0, read/write, 1 byte) on a pixel inside
// that glyph in every thread; the handler logs each instruction that touches it and the system.dll return addresses on
// the stack (the compositor and its callers).
#include "common.h"
#include <tlhelp32.h>

static BYTE* tramp5ca10;
static volatile LONG stamps;
static HANDLE hwEvent, hwDone;
static volatile DWORD hwAddr;
static volatile LONG hits;
static int frameWatchX, frameWatchY;

struct Reader { DWORD eip; LONG n; };
static Reader readers[64];
static LONG nReaders;

// a white pixel of the frame with the text shadow (near black) 2 px down-right of it = a glyph pixel on screen;
// DR1 = write breakpoint on it in every thread: the next writer is whoever composites text into the frame
static void SetDr(int slot, DWORD addr, DWORD rw)
{
    int set = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof te };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1)
        {
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c))
            {
                if (slot == 1) c.Dr1 = addr; else c.Dr0 = addr;
                DWORD en = 1u << (slot * 2), ctl = (rw & 3u) << (16 + slot * 4);
                c.Dr7 = (c.Dr7 & ~((3u << (slot * 2)) | (0xFu << (16 + slot * 4)))) | en | ctl;
                if (SetThreadContext(th, &c)) set++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    Log("DR%d on %08lx (%s) set in %d threads", slot, addr, rw == 1 ? "write" : "read/write", set);
}

static void ArmFrameWatch()
{
    BYTE* f = frameBits;
    int st = frameStride, fh = frameH;
    if (!f || !st || fh <= 0) { Log("frame not known yet"); return; }
    {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\khd4k_frame.raw", abDir);                // bottom-up BGR rows, stride in the log
        HANDLE fo = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fo != INVALID_HANDLE_VALUE)
        {
            DWORD wr;
            WriteFile(fo, f, (DWORD)(st * fh), &wr, NULL);
            CloseHandle(fo);
            Log("frame dumped: stride %d height %d", st, fh);
        }
    }
    for (int y = fh - 3; y > fh / 3; y--)
        for (int x = 40; x < 760; x++)
        {
            BYTE* p = f + (fh - 1 - y) * st + x * 3;
            BYTE* q = f + (fh - 1 - (y + 2)) * st + (x + 2) * 3;
            if (p[0] >= 0xE8 && p[1] >= 0xE8 && p[2] >= 0xE8 && q[0] < 0x40 && q[1] < 0x40 && q[2] < 0x40)
            {
                frameWatchX = x;
                frameWatchY = y;
                SetDr(1, (DWORD)(UINT_PTR)p, 1);
                Log("watching frame pixel (%d,%d) for writes", x, y);
                return;
            }
        }
    Log("no text pixel found in the frame");
}

static DWORD WINAPI HwHelper(LPVOID)
{
    for (;;)
    {
        WaitForSingleObject(hwEvent, INFINITE);
        DWORD addr = hwAddr;
        int set = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = { sizeof te };
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!t) continue;
            if (SuspendThread(t) != (DWORD)-1)
            {
                CONTEXT c = {};
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(t, &c))
                {
                    c.Dr0 = addr;
                    c.Dr7 = (c.Dr7 & ~0x000F0003u) | 0x1u | (0x3u << 16);      // L0, RW0 = read/write, LEN0 = 1 byte
                    if (SetThreadContext(t, &c)) set++;
                }
                ResumeThread(t);
            }
            CloseHandle(t);
        }
        CloseHandle(snap);
        Log("hardware breakpoint on %08lx set in %d threads", addr, set);
        SetEvent(hwDone);
        Sleep(1500);
        ArmFrameWatch();
    }
}

static bool LooksLikeReturn(DWORD v)
{
    if (v < sysBase + 0x1000 || v >= sysEnd) return false;
    const BYTE* p = (const BYTE*)(UINT_PTR)v;
    if (p[-5] == 0xE8) return true;                                  // call rel32
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;        // call [disp32] / [reg+disp32]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;        // call [reg+disp8]
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;        // call reg / [reg]
    return false;
}

static LONG CALLBACK HwHandler(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    if (!(c->Dr6 & 3)) return EXCEPTION_CONTINUE_SEARCH;
    const char* what = (c->Dr6 & 2) ? "FRAME pixel write" : "glyph pixel touch";
    c->Dr6 = 0;
    DWORD eip = c->Eip;                                              // the instruction after the access
    LONG total = InterlockedIncrement(&hits);
    if (total > 400) c->Dr7 &= ~0xFu;                                // enough
    for (LONG i = 0; i < nReaders; i++)
        if (readers[i].eip == eip) { InterlockedIncrement(&readers[i].n); return EXCEPTION_CONTINUE_EXECUTION; }
    if (nReaders >= 64) return EXCEPTION_CONTINUE_EXECUTION;
    readers[nReaders].eip = eip;
    readers[nReaders].n = 1;
    nReaders++;
    char chain[300];
    int len = 0, found = 0;
    DWORD* sp = (DWORD*)(UINT_PTR)c->Esp;
    for (int i = 0; i < 600 && found < 10; i++)
    {
        DWORD v;
        __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (LooksLikeReturn(v))
        {
            len += _snprintf_s(chain + len, sizeof(chain) - len, _TRUNCATE, " %08lx", v - sysBase);
            found++;
        }
    }
    chain[len] = 0;
    Log("%s: after %08lx (%s) thread %lu; stack returns:%s", what, Rva((void*)(UINT_PTR)eip),
        (eip >= sysBase && eip < sysEnd) ? "system.dll" : "elsewhere", GetCurrentThreadId(), chain);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void __stdcall Post5ca10(DWORD* a, DWORD ret)
{
    BYTE* layer = (BYTE*)(UINT_PTR)a[0];
    BYTE* surf = layer + 0xB4;
    BITMAPINFOHEADER* bi = *(BITMAPINFOHEADER**)(surf + 4);
    BYTE* bits = *(BYTE**)(surf + 0xC);
    int stride = *(int*)(surf + 0x4C);
    int h = bi ? (bi->biHeight < 0 ? -bi->biHeight : bi->biHeight) : 0;
    LONG n = InterlockedIncrement(&stamps);
    if (n <= 6)
        Log("DrawChar #%ld layer %p at (%ld,%ld) color %06lx -> %lu; surface %dx%d bits %p stride %d", n, layer, (long)a[2], (long)a[3],
            a[5], ret, bi ? bi->biWidth : 0, h, bits, stride);
    if (n == 8 && bits && h)            // the 4th glyph (white pass): watch a white pixel of it, before anything reads it
    {
        int w = bi->biWidth;
        DWORD pick = 0;
        for (int y = (int)a[3]; y < (int)a[3] + 24 && !pick; y++)
            for (int x = (int)a[2]; x < (int)a[2] + (int)ret && !pick; x++)
            {
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                BYTE* p = bits + (h - y - 1) * stride + x * 3;        // bottom-up DIB, as the engine's 0x11c0
                if (p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF) pick = (DWORD)(UINT_PTR)p;
            }
        if (pick)
        {
            hwAddr = pick;
            SetEvent(hwEvent);
            WaitForSingleObject(hwDone, 2000);                         // the helper suspends us too while it sets DR0
        }
        else Log("no white pixel in glyph #4");
    }
}

__declspec(naked) static void Hook5ca10()
{
    __asm {
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        push dword ptr [esp + 0x18]
        call dword ptr [tramp5ca10]
        push eax
        push eax
        lea edx, [esp + 0xC]
        push edx
        call Post5ca10
        pop eax
        ret 0x18
    }
}

// ---------------------------------------------------------------- DrawLayer (0x56450): ecx + 9 args, ret 0x24
static BYTE* tramp56450;
static volatile LONG layerCalls;

static bool SurfaceDims(DWORD p, int* w, int* h, int* bpp)
{
    __try
    {
        if (p < 0x10000) return false;
        BITMAPINFOHEADER* bi = *(BITMAPINFOHEADER**)(UINT_PTR)(p + 4);
        if ((UINT_PTR)bi < 0x10000 || bi->biSize != 40) return false;
        *w = bi->biWidth;
        *h = bi->biHeight;
        *bpp = bi->biBitCount;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void __stdcall Pre56450(DWORD self, DWORD ret, DWORD* a)
{
    if (stamps == 0) return;                                          // only after the first glyph
    LONG n = InterlockedIncrement(&layerCalls);
    if (n > 60) return;
    char line[700];
    DWORD dstBits = 0;
    __try { dstBits = *(DWORD*)(UINT_PTR)(a[0] + 0xC); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    int len = _snprintf_s(line, _TRUNCATE, "DrawLayer #%ld caller %08lx this %08lx dst %08lx bits %08lx%s args", n, Rva((void*)(UINT_PTR)ret), self,
                          a[0], dstBits, (BYTE*)(UINT_PTR)dstBits == frameBits ? " (PRESENTED)" : "");
    for (int i = 0; i < 9; i++)
    {
        int w, h, bpp;
        if (SurfaceDims(a[i], &w, &h, &bpp))
            len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " [%d]S%dx%d/%d", i, w, h, bpp);
        else
            len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " [%d]%ld", i, (long)a[i]);
    }
    int w, h, bpp;
    if (SurfaceDims(self, &w, &h, &bpp)) len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " | this S%dx%d", w, h);
    __try
    {
        for (int k = 0; k < 0x140; k += 4)                           // surfaces held by the image object
        {
            DWORD q = *(DWORD*)(UINT_PTR)(self + k);
            if (SurfaceDims(q, &w, &h, &bpp))
                len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " this+%x=%08lx:S%dx%d/%d", k, q, w, h, bpp);
            else if (SurfaceDims(self + k, &w, &h, &bpp))
                len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " this+%x:embedded S%dx%d/%d", k, w, h, bpp);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("%s", line);
}

__declspec(naked) static void Hook56450()
{
    __asm {
        push ecx
        lea eax, [esp + 8]
        push eax
        push dword ptr [esp + 8]
        push ecx
        call Pre56450
        pop ecx
        jmp dword ptr [tramp56450]
    }
}

// ---------------------------------------------------------------- CopyRect (0x52480): ecx flag, eax first row, edx rows,
// stack (src, dst, x, width) - ret 0x10
static BYTE* tramp52480;
static volatile LONG copyCalls;

static void __stdcall Pre52480(DWORD eax_, DWORD ecx_, DWORD edx_, DWORD ret, DWORD* a)
{
    LONG n = InterlockedIncrement(&copyCalls);
    if (n > 40 || stamps == 0) return;
    int w1 = 0, h1 = 0, b1 = 0, w2 = 0, h2 = 0, b2 = 0;
    SurfaceDims(a[0], &w1, &h1, &b1);
    SurfaceDims(a[1], &w2, &h2, &b2);
    Log("CopyRect #%ld caller %08lx flag %lu rows %ld+%ld src %08lx S%dx%d/%d dst %08lx S%dx%d/%d x %ld w %ld", n, Rva((void*)(UINT_PTR)ret),
        ecx_, (long)eax_, (long)edx_, a[0], w1, h1, b1, a[1], w2, h2, b2, (long)a[2], (long)a[3]);
}

__declspec(naked) static void Hook52480()
{
    __asm {
        pushad
        lea ebx, [esp + 36]          // stack args (pushad 32 + return address 4)
        push ebx
        push dword ptr [esp + 36]    // the return address
        push edx
        push ecx
        push eax
        call Pre52480
        popad
        jmp dword ptr [tramp52480]
    }
}

void InstallProbe()
{
    const BYTE expectCopy[5] = { 0x83, 0xEC, 0x08, 0x85, 0xC9 };              // sub esp,8; test ecx,ecx
    if (memcmp((BYTE*)sys + 0x52480, expectCopy, 5) == 0 && InlineHook(0x52480, 5, (void*)Hook52480, &tramp52480))
        Log("probe: CopyRect (0x52480) logged");
    else Log("probe: CopyRect NOT hooked - unexpected bytes");
    const BYTE expectLayer[7] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x10 };   // mov eax,[esp+4]; sub esp,0x10
    if (memcmp((BYTE*)sys + 0x56450, expectLayer, 7) == 0 && InlineHook(0x56450, 7, (void*)Hook56450, &tramp56450))
        Log("probe: DrawLayer (0x56450) logged");
    else Log("probe: DrawLayer NOT hooked - unexpected bytes");
    const BYTE expect[7] = { 0x6A, 0xFF, 0x68, 0xBC, 0xFE, 0x0B, 0x10 };      // push -1; push 0x100bfebc
    if (memcmp((BYTE*)sys + 0x5ca10, expect, 7) != 0 || !InlineHook(0x5ca10, 7, (void*)Hook5ca10, &tramp5ca10))
    {
        Log("probe: DrawChar (0x5ca10) NOT hooked - unexpected bytes");
        return;
    }
    hwEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    hwDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, HwHelper, NULL, 0, NULL));
    AddVectoredExceptionHandler(1, HwHandler);
    Log("probe: DrawChar hooked; a hardware breakpoint goes on a glyph pixel after the 8th stamp");
}
