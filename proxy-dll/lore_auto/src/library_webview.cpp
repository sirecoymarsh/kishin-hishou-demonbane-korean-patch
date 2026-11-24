#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <cwctype>
#include <cstdio>
#include "WebView2.h"
#include "library_uuids.h"

// The existing native menu owns this surface. Book folders remain read-only.
extern "C" void LoreLog(const char*);
static HWND host;
static unsigned generation;
static ULONGLONG mapping;
static std::wstring root, ini, activeMapping;
static ICoreWebView2Controller *controller;
static ICoreWebView2 *view;
static ICoreWebView2_3 *view3;
static bool comOwned;
static const wchar_t *origin=L"https://library.demonbane.test/";
struct Book {std::wstring path,title,id;};
static std::vector<Book> books;
static std::wstring pendingNotice;   // 11-19: shown after the reload (catalog)
static std::vector<std::wstring> domains;
enum { LIB_ACTION=WM_APP+0x580, LIB_PICK=WM_APP+0x581 };

template<class I, class... A> class Callback final:public I {
    ULONG refs=1; std::function<HRESULT(A...)> fn;
public:
    explicit Callback(std::function<HRESULT(A...)> f):fn(f){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==__uuidof(I)){*out=static_cast<I*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override{return fn(a...);}
};
static std::wstring quote(const std::wstring &s){
    std::wstring o=L"\"";for(wchar_t c:s){if(c==L'"'||c==L'\\'){o+=L'\\';o+=c;}else if(c<32){wchar_t b[8];swprintf(b,8,L"\\u%04x",c);o+=b;}else o+=c;}return o+L"\"";
}
static std::wstring urlpart(const std::wstring &s){
    int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string b(n,0);WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,&b[0],n,nullptr,nullptr);
    std::wstring o;for(int i=0;i<n-1;i++){unsigned char c=b[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')o+=(wchar_t)c;else if(c=='\\'||c=='/')o+=L'/';else{wchar_t x[4];swprintf(x,4,L"%%%02X",c);o+=x;}}return o;
}
static bool directory(const std::wstring &p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY);}
static bool exists(const std::wstring &p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
static bool isepub(const std::wstring &p){return !_wcsicmp(PathFindExtensionW(p.c_str()),L".epub")||exists(p+L"\\META-INF\\container.xml");}
static std::wstring titleof(std::wstring p){while(!p.empty()&&(p.back()==L'\\'||p.back()==L'/'))p.pop_back();return PathFindFileNameW(p.c_str());}
static void post(const std::wstring &s){if(view)view->PostWebMessageAsJson(s.c_str());}
static void error(const wchar_t *message){post(L"{\"type\":\"error\",\"message\":"+quote(message)+L"}");}
static std::wstring bookid(const std::wstring &p);
static void loadbooks(){
    books.clear();int n=GetPrivateProfileIntW(L"Library",L"Count",0,ini.c_str());
    for(int i=0;i<n&&i<256;i++){wchar_t section[32],p[4096],t[512];swprintf(section,32,L"Book%d",i);GetPrivateProfileStringW(section,L"Path",L"",p,4096,ini.c_str());GetPrivateProfileStringW(section,L"Title",L"",t,512,ini.c_str());if(*p){wchar_t id[128];GetPrivateProfileStringW(section,L"Id",L"",id,128,ini.c_str());std::wstring path=p;if(PathIsRelativeW(p))path=root+L"\\"+path;books.push_back({path,*t?t:titleof(p),id});}}
    // 11-19 Claude: every EPUB file in ko-library\books is on the shelf too (after the books.ini ones, by name) - a new book is one file
    unsigned added=0;std::vector<std::wstring> found;WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((root+L"\\books\\*.epub").c_str(),&d);
    if(f!=INVALID_HANDLE_VALUE){do{if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!_wcsicmp(PathFindExtensionW(d.cFileName),L".epub"))found.push_back(d.cFileName);}while(FindNextFileW(f,&d));FindClose(f);}
    std::sort(found.begin(),found.end(),[](const std::wstring &a,const std::wstring &b){return StrCmpLogicalW(a.c_str(),b.c_str())<0;});
    for(auto &name:found){
        if(books.size()>=256)break;
        std::wstring path=root+L"\\books\\"+name;bool listed=false;
        for(auto &b:books)if(!_wcsicmp(b.path.c_str(),path.c_str())){listed=true;break;}
        if(!listed){books.push_back({path,name.substr(0,name.size()-5),L"file-"+bookid(name)});++added;}
    }
    char m[96];snprintf(m,sizeof(m),"shelf: %u books, %u found only in the books folder",(unsigned)books.size(),added);LoreLog(m);
}
static bool savebook(const Book &b){
    unsigned own=(unsigned)GetPrivateProfileIntW(L"Library",L"Count",0,ini.c_str());wchar_t section[32],count[32];swprintf(section,32,L"Book%u",own);swprintf(count,32,L"%u",own+1);
    return WritePrivateProfileStringW(section,L"Path",b.path.c_str(),ini.c_str())&&WritePrivateProfileStringW(section,L"Title",b.title.c_str(),ini.c_str())&&WritePrivateProfileStringW(L"Library",L"Count",count,ini.c_str());
}
static std::wstring bookid(const std::wstring &p){unsigned long long h=14695981039346656037ull;for(wchar_t c:p){h^=(unsigned)towlower(c);h*=1099511628211ull;}wchar_t b[32];swprintf(b,32,L"%016llx",h);return b;}
static std::wstring domain(const Book &b){return L"book-"+(b.id.empty()?bookid(b.path):b.id)+L"-"+std::to_wstring(mapping)+L".demonbane.test";}
static void mapbooks(){
    // WebView2 resolves virtual hosts when a document is created. Register all
    // sources before navigation, and reload after additions or refresh.
    loadbooks();for(auto &d:domains)view3->ClearVirtualHostNameToFolderMapping(d.c_str());domains.clear();mapping=GetTickCount64();
    for(auto &b:books){if(!exists(b.path))continue;std::wstring folder=b.path;if(!directory(folder)){size_t n=folder.find_last_of(L"\\/");if(n==std::wstring::npos)continue;folder.resize(n);}auto d=domain(b);if(SUCCEEDED(view3->SetVirtualHostNameToFolderMapping(d.c_str(),folder.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW)))domains.push_back(d);}
}
static void catalog(){
    loadbooks();std::wstring s=L"{\"type\":\"catalog\",\"books\":[";
    for(size_t i=0;i<books.size();i++){if(i)s+=L",";auto &b=books[i];s+=L"{\"index\":"+std::to_wstring(i)+L",\"id\":"+quote((b.id.empty()?bookid(b.path):b.id))+L",\"title\":"+quote(b.title)+L",\"path\":"+quote(b.path)+L",\"kind\":"+quote(isepub(b.path)?L"epub":L"comic")+L",\"available\":"+(exists(b.path)?L"true":L"false")+L",\"url\":"+quote(!directory(b.path)&&isepub(b.path)?L"https://"+domain(b)+L"/"+urlpart(titleof(b.path)):L"")+L"}";}post(s+L"]}");if(!pendingNotice.empty()){post(L"{\"type\":\"notice\",\"message\":"+quote(pendingNotice)+L"}");pendingNotice.clear();}
}
static bool imagefile(const std::wstring &p){const wchar_t *e=PathFindExtensionW(p.c_str());return !_wcsicmp(e,L".png")||!_wcsicmp(e,L".jpg")||!_wcsicmp(e,L".jpeg")||!_wcsicmp(e,L".webp")||!_wcsicmp(e,L".gif")||!_wcsicmp(e,L".bmp")||!_wcsicmp(e,L".avif");}
static void images(const std::wstring &base,const std::wstring &relative,std::vector<std::wstring> &out,int depth=0){
    if(depth>8||out.size()>20000)return;WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((base+L"\\"+relative+L"*").c_str(),&d);if(f==INVALID_HANDLE_VALUE)return;
    do{if(d.cFileName[0]==L'.'||(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;std::wstring p=relative+d.cFileName;if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)images(base,p+L"\\",out,depth+1);else if(imagefile(p))out.push_back(p);}while(FindNextFileW(f,&d));FindClose(f);
}
static void openbook(int i){
    char event[80];snprintf(event,sizeof(event),"open book %d",i);LoreLog(event);
    if(!view3||i<0||i>=(int)books.size())return;auto &b=books[i];if(!exists(b.path)){error(L"책 폴더를 찾을 수 없습니다. 폴더의 위치를 확인해 주세요.");return;}
    bool dir=directory(b.path),epub=isepub(b.path);std::vector<std::wstring> pages;
    if(!epub){images(b.path,L"",pages);std::sort(pages.begin(),pages.end(),[](auto &a,auto &b){return StrCmpLogicalW(a.c_str(),b.c_str())<0;});if(pages.empty()){error(L"이 폴더에 읽을 수 있는 이미지가 없습니다. EPUB은 META-INF 폴더가 들어 있는 최상위 폴더를 선택해 주세요.");return;}}
    activeMapping=domain(b);
    if(std::find(domains.begin(),domains.end(),activeMapping)==domains.end()){error(L"책 폴더를 연결하지 못했습니다. 폴더 경로와 새로고침을 확인해 주세요.");return;}
    std::wstring base=L"https://"+activeMapping+L"/",url=base+(dir?L"":urlpart(titleof(b.path)));
    std::wstring s=L"{\"type\":\"opened\",\"index\":"+std::to_wstring(i)+L",\"id\":"+quote((b.id.empty()?bookid(b.path):b.id))+L",\"title\":"+quote(b.title)+L",\"kind\":"+quote(epub?L"epub":L"comic")+L",\"url\":"+quote(url)+L",\"packed\":"+(dir?L"false":L"true")+L",\"pages\":[";
    for(size_t k=0;k<pages.size();k++){if(k)s+=L",";s+=quote(base+urlpart(pages[k]));}post(s+L"]}");
}
// 11-19 Claude: 「책 들여오기」 - the chosen EPUB files are copied into ko-library\books (books.ini untouched: a re-extracted patch keeps
// them; the shelf lists that folder and reads each book's own cover and title). Same name and same bytes: skipped; same name, other
// bytes: "name (2).epub".
static bool samefile(const std::wstring &a,const std::wstring &b){
    WIN32_FILE_ATTRIBUTE_DATA x,y;if(!GetFileAttributesExW(a.c_str(),GetFileExInfoStandard,&x)||!GetFileAttributesExW(b.c_str(),GetFileExInfoStandard,&y))return false;
    if(x.nFileSizeLow!=y.nFileSizeLow||x.nFileSizeHigh!=y.nFileSizeHigh)return false;
    HANDLE f=CreateFileW(a.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr),g=CreateFileW(b.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    bool same=f!=INVALID_HANDLE_VALUE&&g!=INVALID_HANDLE_VALUE;std::vector<char> p(1<<16),q(1<<16);
    while(same){DWORD m=0,n=0;if(!ReadFile(f,p.data(),(DWORD)p.size(),&m,nullptr)||!ReadFile(g,q.data(),(DWORD)q.size(),&n,nullptr)||m!=n){same=false;break;}if(!m)break;if(memcmp(p.data(),q.data(),m))same=false;}
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);if(g!=INVALID_HANDLE_VALUE)CloseHandle(g);return same;
}
static void LibraryImport(){
    IFileOpenDialog *dialog=nullptr;HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));if(FAILED(hr)){error(L"파일 선택 창을 열지 못했습니다.");return;}
    DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_FILEMUSTEXIST|FOS_ALLOWMULTISELECT);dialog->SetTitle(L"서고에 들여올 EPUB 파일 선택 (여러 개 가능)");
    COMDLG_FILTERSPEC filter={L"EPUB 책",L"*.epub"};dialog->SetFileTypes(1,&filter);
    if(SUCCEEDED(dialog->Show(GetAncestor(host,GA_ROOT)))){IShellItemArray *items=nullptr;
        if(SUCCEEDED(dialog->GetResults(&items))){
            DWORD n=0;items->GetCount(&n);std::wstring dir=root+L"\\books";CreateDirectoryW(dir.c_str(),nullptr);unsigned copied=0,failed=0,already=0;
            for(DWORD i=0;i<n;i++){IShellItem *item=nullptr;PWSTR path=nullptr;
                if(SUCCEEDED(items->GetItemAt(i,&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){
                    std::wstring name=PathFindFileNameW(path),ext=PathFindExtensionW(path),stem=name.substr(0,name.size()-ext.size()),target=dir+L"\\"+name;
                    if(!_wcsicmp(path,target.c_str())||(exists(target)&&samefile(path,target)))already++;
                    else{for(int k=2;exists(target)&&k<1000;k++)target=dir+L"\\"+stem+L" ("+std::to_wstring(k)+L")"+ext;
                        if(CopyFileW(path,target.c_str(),TRUE))copied++;else failed++;}
                    CoTaskMemFree(path);}
                if(item)item->Release();}
            items->Release();
            char m[112];snprintf(m,sizeof(m),"import: %u copied, %u failed, %u already in the folder",copied,failed,already);LoreLog(m);
            if(failed)pendingNotice=L"책 "+std::to_wstring(failed)+L"권을 서고 폴더(ko-library\\books)에 복사하지 못했습니다. 게임 폴더에 쓸 수 있는지 확인해 주세요.";
            else if(copied)pendingNotice=L"책 "+std::to_wstring(copied)+L"권을 서고에 들여왔습니다."+(already?L" (이미 있던 "+std::to_wstring(already)+L"권은 건너뜀)":L"");
            else if(already)pendingNotice=L"고른 책은 이미 서고에 있습니다.";
            mapbooks();view->Reload();}}
    dialog->Release();
}
extern "C" void LibraryPick(int folder){
    if(folder==2){LibraryImport();return;}   // 11-19 「책 들여오기」
    IFileOpenDialog *dialog=nullptr;HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));if(FAILED(hr)){error(L"파일 선택 창을 열지 못했습니다.");return;}
    DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST));dialog->SetTitle(folder?L"만화 이미지 폴더 또는 압축을 푼 EPUB 폴더":L"읽을 EPUB 파일 선택");
    COMDLG_FILTERSPEC filter={L"EPUB 소설",L"*.epub"};if(!folder)dialog->SetFileTypes(1,&filter);
    if(SUCCEEDED(dialog->Show(GetAncestor(host,GA_ROOT)))){IShellItem *item=nullptr;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){
        auto found=std::find_if(books.begin(),books.end(),[&](auto &b){return !_wcsicmp(b.path.c_str(),path);});
        if(found==books.end()){if(books.size()>=256)error(L"책은 최대 256개까지 등록할 수 있습니다.");else if(!savebook({path,titleof(path)}))error(L"책 목록을 저장하지 못했습니다.");}mapbooks();view->Reload();CoTaskMemFree(path);
    }if(item)item->Release();}dialog->Release();
}
extern "C" void LibraryResize(){if(controller&&host){RECT r;GetClientRect(host,&r);if(r.right>0&&r.bottom>0)controller->put_Bounds(r);controller->NotifyParentWindowPositionChanged();}}
extern "C" void LibraryVisible(int yes){if(controller){controller->put_IsVisible(yes!=0);if(yes&&!GetEnvironmentVariableW(L"KHD_LORE_TEST",nullptr,0))controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);}}
extern "C" void LibraryClose(){
    ++generation;host=nullptr;if(view3){view3->Release();view3=nullptr;}if(view){view->Release();view=nullptr;}if(controller){controller->Close();controller->Release();controller=nullptr;}if(comOwned){CoUninitialize();comOwned=false;}
}
// 11-09 Claude: the reading records (WebView2 profile) go to <game>\ko-library\profile; a game installed where it may not write
// (Program Files) made the environment fail ("도서관을 열지 못했습니다") - then they go to %LOCALAPPDATA%\KHD-LORE\profile.
// Heap buffers only: this runs on the game's UI thread, but the 32768-wchar locals were the kind that broke the movie threads.
static bool writablefolder(const std::wstring &dir){
    CreateDirectoryW(dir.c_str(),nullptr);
    std::wstring probe=dir+L"\\.write-probe";
    HANDLE f=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    CloseHandle(f);return true;
}
static std::wstring envvalue(const wchar_t *name){
    DWORD n=GetEnvironmentVariableW(name,nullptr,0);if(!n)return L"";
    std::vector<wchar_t> b(n+1);DWORD m=GetEnvironmentVariableW(name,b.data(),n+1);return m&&m<=n?std::wstring(b.data(),m):L"";
}
static std::wstring profilefolder(){
    std::wstring forced=envvalue(L"KHD_LORE_PROFILE");if(!forced.empty())return forced;
    std::wstring own=root+L"\\profile";if(writablefolder(own))return own;
    std::wstring local=envvalue(L"LOCALAPPDATA");
    if(!local.empty()){std::wstring base=local+L"\\KHD-LORE";CreateDirectoryW(base.c_str(),nullptr);std::wstring alt=base+L"\\profile";
        if(writablefolder(alt)){LoreLog("profile: the game folder is not writable - reading records in LOCALAPPDATA\\KHD-LORE");return alt;}}
    return own;
}
static void nativeerror(HRESULT hr){if(!host)return;wchar_t b[256];swprintf(b,256,L"도서관을 열지 못했습니다.\nWebView2 Runtime을 확인해 주세요.\n오류: 0x%08lX",(unsigned long)hr);char code[80];snprintf(code,sizeof(code),"WebView error %08lx",(unsigned long)hr);LoreLog(code);if(GetEnvironmentVariableW(L"KHD_LORE_TEST",nullptr,0))SetWindowTextW(host,b);else MessageBoxW(host,b,L"서고",MB_OK|MB_ICONERROR);}
extern "C" void LibraryCreate(HWND h,const wchar_t *game){
    host=h;unsigned g=++generation;root=std::wstring(game)+L"\\ko-library";ini=root+L"\\books.ini";
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);comOwned=SUCCEEDED(hr);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){nativeerror(hr);return;}
    // Explicit P: project profile; no default AppData or system temp cache.
    HMODULE loader=LoadLibraryW((root+L"\\WebView2Loader.dll").c_str());if(!loader){nativeerror(HRESULT_FROM_WIN32(GetLastError()));return;}
    using CreateFn=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create=(CreateFn)GetProcAddress(loader,"CreateCoreWebView2EnvironmentWithOptions");if(!create){nativeerror(E_NOINTERFACE);return;}
    auto envcb=new Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([g](HRESULT result,ICoreWebView2Environment *env)->HRESULT{
        if(g!=generation||!host)return S_OK;if(FAILED(result)||!env){nativeerror(result);return S_OK;}
        auto cb=new Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>([g](HRESULT result,ICoreWebView2Controller *c)->HRESULT{
            if(g!=generation||!host){if(c)c->Close();return S_OK;}if(FAILED(result)||!c){nativeerror(result);return S_OK;}
            controller=c;c->AddRef();c->get_CoreWebView2(&view);if(!view||FAILED(view->QueryInterface(__uuidof(ICoreWebView2_3),(void**)&view3))){nativeerror(E_NOINTERFACE);return S_OK;}
            ICoreWebView2Settings *settings=nullptr;view->get_Settings(&settings);if(settings){settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsBuiltInErrorPageEnabled(FALSE);settings->Release();}
            view3->SetVirtualHostNameToFolderMapping(L"library.demonbane.test",root.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);mapbooks();
            EventRegistrationToken token;
            auto messages=new Callback<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>([](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs *e)->HRESULT{
                LPWSTR src=nullptr,msg=nullptr;e->get_Source(&src);e->TryGetWebMessageAsString(&msg);
                if(src&&msg&&!wcsncmp(src,origin,wcslen(origin))){if(!wcscmp(msg,L"catalog"))catalog();else if(!wcscmp(msg,L"refresh")){mapbooks();view->Reload();}else if(!wcsncmp(msg,L"open:",5)){wchar_t *end;long i=wcstol(msg+5,&end,10);if(!*end)openbook((int)i);}else if(!wcscmp(msg,L"folder"))PostMessageW(host,LIB_PICK,1,0);else if(!wcscmp(msg,L"epub"))PostMessageW(host,LIB_PICK,0,0);else if(!wcscmp(msg,L"import"))PostMessageW(host,LIB_PICK,2,0);else if(!wcscmp(msg,L"back"))PostMessageW(host,LIB_ACTION,0,0);else if(!wcscmp(msg,L"theater"))PostMessageW(host,LIB_ACTION,2,0);else if(!wcscmp(msg,L"fullscreen"))PostMessageW(host,LIB_ACTION,3,0);}
                CoTaskMemFree(src);CoTaskMemFree(msg);return S_OK;});view->add_WebMessageReceived(messages,&token);messages->Release();
            auto nav=new Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs *e)->HRESULT{LPWSTR u=nullptr;e->get_Uri(&u);if(!u||wcsncmp(u,origin,wcslen(origin)))e->put_Cancel(TRUE);CoTaskMemFree(u);return S_OK;});view->add_NavigationStarting(nav,&token);nav->Release();
            auto popup=new Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs *e)->HRESULT{e->put_Handled(TRUE);return S_OK;});view->add_NewWindowRequested(popup,&token);popup->Release();
            LibraryResize();LibraryVisible(IsWindowVisible(host));view->Navigate((std::wstring(origin)+L"index.html").c_str());return S_OK;
        });HRESULT hr=env->CreateCoreWebView2Controller(host,cb);cb->Release();if(FAILED(hr))nativeerror(hr);return S_OK;
    });std::wstring userdata=profilefolder();hr=create(nullptr,userdata.c_str(),nullptr,envcb);envcb->Release();if(FAILED(hr))nativeerror(hr);
}
