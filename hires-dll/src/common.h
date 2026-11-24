// khd4k.dll - 4K work for the Win11 copy of 機神飛翔デモンベイン (2026-10-15, Claude). Shared helpers and settings.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>

#pragma intrinsic(_ReturnAddress)

// ---------------------------------------------------------------- settings (autobattle.ini [autobattle])
// uhd        0 off (the proxy does not load us), 1 = 4K display, 2 = trace only
// uhd_mode   full = borderless over the whole monitor (no display mode change), window = a big window
// uhd_scale  window mode: the client is 800x600 times this (default 3)
// uhd_trace  1 = also write the call trace (autobattle\khd4k_trace.txt)
// uhd_offscreen 1 = put the window far off screen (background tests: a big window behind others would cover the
//                desktop and take its clicks), and never let it take the user's foreground: made hidden and shown
//                without activation (WS_EX_NOACTIVATE), the engine's SetForegroundWindow ignored, its ShowWindow and
//                SetWindowPos without activation (user 10-16: 「백그라운드에서 소리 끄고 실험해줘 자꾸 화면 가리면 내가
//                작업이 힘들어」)
// uhd_probe  1 = find who reads the dialogue layer (hardware breakpoints; development only; turns uhd_text off)
// uhd_text   1 = stage B: the dialogue at 4x (default), 0 = stage A only (the frame scaled up)
// uhd_battle 1 = fights drawn at the image rectangle's resolution (default), 0 = drawn at 800x600 and scaled up
// uhd_capture first-last/step[/k][@x,y,w,h][;...] (up to 4 ranges): test only - fights get a lockable back buffer and
//             those frames go to autobattle\cap, native ones k x k averaged (default 4 = 720x540; 1 = every pixel,
//             2880x2160, to look at textures); @x,y,w,h = only that rectangle, every pixel; with it the fights also log
//             the textures they make
// uhd_capfrom 0 = capture frames count from the fight's device (default), 1 = from its first 3D draw, the arena's first
//             frame (the loading screen before it lasts as long as the agent waits to press a key, so runs only line up
//             counted from there)
// uhd_envfix  test only - fights' environment-mapped draws (stage 1, camera-space reflection vector): 0 as the module
//             draws them, 1 = the generated coordinates taken as two (texture transform COUNT2, identity), 2 = the
//             reflection texture alone (what the lookup gives)
// uhd_pictrace 1 = stage C groundwork (11-07): log every picture load (0x4d290: name -> surface) and every DrawLayer
//             (image, its colour/alpha surfaces, opacity, key, the 9 arguments, the loaded name of the colour surface)
//             to khd4k_log.txt - to see whether loaded surfaces reach the frame directly
// uhd_stagec 1 = stage C (11-07): pictures drawn at the output scale from 4x files (needs uhd_text=1, the stage B frame)
// uhd_hires  the folder with index.tsv and the 4x pictures (default <game>\hires)
struct Settings
{
    int uhd, full, scale, trace, offscreen, probe, text, battle, envfix, pictrace, stagec;
    int dumpfo;                                        // uhd_dumpfo N (test): every N-th present the output frame -> cap\fo_*.bmp (12 at most)
    int check;                                         // uhd_check 1 (test): every present, FO against S1 per block (log 'check:')
    wchar_t hiresDir[MAX_PATH];
    int capFirst;                                      // non-zero: some frames are captured (uhd_capture)
    int capN, capA[4], capB[4], capS[4], capK[4];      // the ranges: first, last, step, averaging
    int capX[4], capY[4], capW[4], capH[4];            // ... and a rectangle (capW 0 = the whole frame)
    int capFrom;                                       // uhd_capfrom
};
extern Settings cfg;

extern HMODULE sys;            // system.dll
extern DWORD sysBase, sysEnd;
extern wchar_t abDir[MAX_PATH];

void Log(const char* fmt, ...);
DWORD Rva(void* ret);          // system.dll RVA of a return address (0x8xxxxxxx = outside system.dll)
bool Budget(const char* fn, DWORD rva, LONG full = 12);   // log the first `full` calls of (fn, caller) in full
bool Patch(void** slot, void* hook, void** orig);
bool HookImport(const char* dll, const char* func, void* hook, void** orig);
bool HookModuleImport(HMODULE mod, const char* dll, const char* func, void* hook, void** orig);
bool HookModuleImportIfAny(HMODULE mod, const char* dll, const char* func, void* hook);   // silent when not imported
bool InlineHook(DWORD rva, int n, void* hook, BYTE** tramp);

extern BYTE* volatile frameBits;  // display.cpp: the engine's frame DIB (bottom-up) at the last full present
extern int frameStride, frameH;

void InstallTrace();           // trace.cpp
void InstallDisplay();         // display.cpp
void InstallProbe();           // probe.cpp
void InstallUhd();             // uhd.cpp
// hires.cpp (stage C)
void HiresInit(const wchar_t* dir);
void HiresNormalise(const char* in, char* out, int cap);
void HiresOnLoad(DWORD surf, const char* name);
const BYTE* HiresAtK(DWORD surf, BYTE* bits, int stride, int w, int ht, int P, int* kw, int* kh, int* alpha);
void HiresStats(char* buf, int cap);
void HiresSetP(int P);                  // the output's px per 5-px block (bands start at load time)
void InstallBattle();          // battle.cpp
void InstallMovie();           // movie.cpp
bool UhdPresent(HDC d, int x, int y);  // uhd.cpp: draw the output frame 1:1; false = not ready
int UhdSetOutput(int imgH);             // uhd.cpp: output image height (multiple of 120) -> px per 5-px block
void UhdRepaint();
HWND UhdMainWindow();          // display.cpp: the engine's main window (NULL before it is made)
WNDPROC UhdFrontProc(WNDPROC requested);   // display.cpp: the procedure to set instead (ours, in front of the engine's)
void UhdQuietWindows(HMODULE m);           // display.cpp: uhd_offscreen - m's window calls never activate the game
