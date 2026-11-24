#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstring>

extern "C" void LibraryCreate(HWND,const wchar_t*);
extern "C" void LibraryResize();
extern "C" void LibraryVisible(int);
extern "C" void LibraryClose();
extern "C" void LibraryPick(int);
static HINSTANCE instance;
static HWND gameWindow;
static WNDPROC previousProc;
static HHOOK installHook;
static constexpr UINT INSTALL_BRIDGE=WM_APP+0x59f;
static std::wstring gameRoot;
static bool quiet, full;
static bool inReader, readerDone, closeGame;
static RECT restoreRect;
static LONG restoreStyle;

extern "C" void LoreLog(const char* message){
    if(gameRoot.empty())return;
    HANDLE f=CreateFileW((gameRoot+L"\\ko-library\\native.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    char line[1024];int n=snprintf(line,sizeof(line),"%llu pid=%lu tid=%lu %s\r\n",GetTickCount64(),GetCurrentProcessId(),GetCurrentThreadId(),message);
    DWORD done;WriteFile(f,line,n,&done,nullptr);CloseHandle(f);
}
static void toggleFull(HWND h){
    if(!full){
        GetWindowRect(h,&restoreRect);restoreStyle=GetWindowLongW(h,GWL_STYLE);
        MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);
        RECT r=monitor.rcMonitor;
        int width=r.right-r.left,height=r.bottom-r.top;
        if(quiet){r.left=-32000;r.top=-32000;}
        // WS_VISIBLE must survive the style change. The old host cleared it,
        // hiding the entire reader while its disabled game owner stayed alive.
        LONG style=(restoreStyle&~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX))|WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN;
        full=true;
        SetWindowLongW(h,GWL_STYLE,style);
        SetWindowPos(h,nullptr,r.left,r.top,width,height,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        LibraryResize();LoreLog("embedded fullscreen visible");
    }else{
        full=false;
        SetWindowLongW(h,GWL_STYLE,restoreStyle);
        SetWindowPos(h,nullptr,restoreRect.left,restoreRect.top,restoreRect.right-restoreRect.left,restoreRect.bottom-restoreRect.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        LibraryResize();LoreLog("embedded windowed visible");
    }
}
static void openReader(HWND parent){
    if(inReader)return;
    LoreLog("LORE enter embedded in game window");
    RECT gameRect;GetWindowRect(parent,&gameRect);
    LONG gameStyle=GetWindowLongW(parent,GWL_STYLE);
    LONG gameExStyle=GetWindowLongW(parent,GWL_EXSTYLE);
    inReader=true;readerDone=false;closeGame=false;full=false;
    SetPropW(parent,L"KHD.Lore.Active",(HANDLE)1);
    SetWindowLongW(parent,GWL_STYLE,gameStyle|WS_CLIPCHILDREN);
    // The reused WebView controller creates its child directly inside the
    // game's client area. There is no second top-level reader window.
    LibraryCreate(parent,gameRoot.c_str());
    MSG message;bool quit=false;int quitCode=0;
    while(!readerDone&&IsWindow(parent)){
        int result=GetMessageW(&message,nullptr,0,0);
        if(result<=0){quit=result==0;quitCode=(int)message.wParam;break;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    LibraryClose();
    if(IsWindow(parent)){
        // Restore the engine's exact surface before returning to its suspended
        // menu command. Reader resizes must not reset its Direct3D device.
        SetWindowLongW(parent,GWL_STYLE,gameStyle);
        SetWindowLongW(parent,GWL_EXSTYLE,gameExStyle);
        SetWindowPos(parent,nullptr,gameRect.left,gameRect.top,gameRect.right-gameRect.left,gameRect.bottom-gameRect.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
        RemovePropW(parent,L"KHD.Lore.Active");
        if(!quiet&&GetForegroundWindow()==parent)SetFocus(parent);
        InvalidateRect(parent,nullptr,FALSE);
    }
    inReader=false;full=false;
    LoreLog("LORE return to engine caller from embedded reader");
    if(closeGame&&IsWindow(parent))PostMessageW(parent,WM_CLOSE,0,0);
    if(quit)PostQuitMessage(quitCode);
}
static LRESULT CALLBACK gameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(inReader){
        switch(m){
        case WM_APP+0x580:if(w==3)toggleFull(h);else readerDone=true;return 0;
        case WM_APP+0x581:LibraryPick((int)w);return 0;
        case WM_SIZE:if(w!=SIZE_MINIMIZED)LibraryResize();return 0;
        case WM_MOVE:case WM_MOVING:LibraryResize();return 0;
        case WM_WINDOWPOSCHANGED:{LRESULT result=DefWindowProcA(h,m,w,l);LibraryResize();return result;}
        case WM_DPICHANGED:{auto r=(const RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);LibraryResize();return 0;}
        case WM_SHOWWINDOW:LibraryVisible((int)w);return 0;
        case WM_SETFOCUS:if(!quiet)LibraryVisible(TRUE);return 0;
        case WM_MOUSEACTIVATE:if(quiet)return MA_NOACTIVATE;break;
        case WM_KEYDOWN:if(w==VK_F11&&!(l&(1L<<30)))toggleFull(h);return 0;
        case WM_SYSKEYDOWN:if(w==VK_RETURN&&!(l&(1L<<30))){toggleFull(h);return 0;}break;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);HBRUSH b=CreateSolidBrush(RGB(9,13,22));FillRect(dc,&r,b);DeleteObject(b);EndPaint(h,&ps);return 0;}
        case WM_CLOSE:closeGame=true;readerDone=true;return 0;
        case WM_DESTROY:readerDone=true;break;
        }
        // While the menu caller is suspended, the WebView owns input, painting
        // and size messages. Do not route reader state to game rendering hooks.
        return DefWindowProcA(h,m,w,l);
    }
    if(m==0x410&&w==2&&l){
        MEMORY_BASIC_INFORMATION region={};
        if(VirtualQuery((void*)l,&region,sizeof(region))&&region.State==MEM_COMMIT&&!(region.Protect&(PAGE_NOACCESS|PAGE_GUARD))){
            const char* url=(const char*)l;
            static const char target[]="http://khd-lore.invalid/library";
            if((const char*)region.BaseAddress+region.RegionSize-url>=sizeof(target)&&!memcmp(url,target,sizeof(target))){
                openReader(h);return 1;
            }
        }
    }
    return CallWindowProcA(previousProc,h,m,w,l);
}
static BOOL CALLBACK findGame(HWND h,LPARAM){
    DWORD pid;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;
    char cls[128];GetClassNameA(h,cls,sizeof(cls));
    if(!strcmp(cls,"OCGameApplicationWindow")){gameWindow=h;return FALSE;}return TRUE;
}
static LRESULT CALLBACK installOnUIThread(int code,WPARAM w,LPARAM l){
    if(code>=0){
        auto message=(const CWPSTRUCT*)l;
        if(message->hwnd==gameWindow&&message->message==INSTALL_BRIDGE){
            // Serialize publication of the previous procedure with dispatch on
            // the window's owner thread; a worker-thread swap has a null race.
            previousProc=(WNDPROC)SetWindowLongPtrA(gameWindow,GWLP_WNDPROC,(LONG_PTR)gameProc);
            if(previousProc){SetPropW(gameWindow,L"KHD.Lore.Attached",(HANDLE)1);LoreLog("game window URL bridge attached on UI thread");}
            UnhookWindowsHookEx(installHook);installHook=nullptr;
        }
    }
    return CallNextHookEx(nullptr,code,w,l);
}
static DWORD WINAPI attach(LPVOID){
    // 11-09 Claude: heap, not a 64 KiB stack local (the kind that overflowed the movie threads in DllMain)
    std::wstring path(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,&path[0],32768);path.resize(n);gameRoot=path;gameRoot.resize(gameRoot.find_last_of(L"\\/"));
    LoreLog("worker started");
    quiet=GetEnvironmentVariableW(L"KHD_LORE_TEST",nullptr,0)>0;
    for(int i=0;i<600&&!gameWindow;i++){EnumWindows(findGame,0);if(!gameWindow)Sleep(50);}
    if(!gameWindow){LoreLog("no game window; hook not installed");return 0;}
    LoreLog("own game window found");
    // Existing native patches stay in the WndProc chain, no game code addresses.
    DWORD thread=GetWindowThreadProcessId(gameWindow,nullptr);
    installHook=SetWindowsHookExW(WH_CALLWNDPROC,installOnUIThread,instance,thread);
    if(installHook){
        // WH_CALLWNDPROC observes sent messages. Use a bounded synchronous
        // dispatch so installation actually runs on the owning UI thread.
        DWORD_PTR result=0;
        if(!SendMessageTimeoutW(gameWindow,INSTALL_BRIDGE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,10000,&result))LoreLog("UI-thread installation dispatch timed out");
    }else LoreLog("UI-thread bridge installation failed");
    return 0;
}
extern "C" __declspec(dllexport) void LoreBootstrap(){}
BOOL WINAPI DllMain(HINSTANCE inst,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        // DllMain also runs on the movie decoder's small-stack threads. A
        // 32768-wchar local here reserved 64 KiB before the reason check and
        // overflowed those threads. Keep path/string/log work in attach().
        // The static runtime needs thread notifications; do not suppress them.
        instance=inst;
        HANDLE t=CreateThread(nullptr,0,attach,nullptr,0,nullptr);
        if(t)CloseHandle(t);
    }
    return TRUE;
}
