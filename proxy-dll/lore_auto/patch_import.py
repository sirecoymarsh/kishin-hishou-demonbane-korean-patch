"""11-19 사용자 「들여오기 기능은 어디에 있어?」→「만들어 줘」: 서고 「책 들여오기」 — 고른 EPUB(여러 개)을 ko-library\\books 로 복사(books.ini 는 그대로라
패치를 다시 풀어도 남음; 자동 목록·자동 표지가 찾아 보여 줌). patch_auto.py 다음에 건다.
 - LibraryPick(2) = 들여오기(LibraryImport). 화면 메시지 "import" → PostMessage(host, LIB_PICK, 2) — lore_host.cpp 는 그대로.
 - 같은 이름의 같은 내용이면 건너뜀, 다른 내용이면 「이름 (2).epub」. 끝나면 mapbooks + 다시 불러오기, 알림은 다시 불러온 화면의 catalog 뒤에 보냄.
  python -I -X utf8 patch_import.py"""
from pathlib import Path

W = Path(__file__).resolve().parent / "src" / "library_webview.cpp"
s = W.read_text(encoding="utf-8")
if "LibraryImport" in s:
    raise SystemExit("already patched")

old = "static std::vector<Book> books;"
assert s.count(old) == 1
s = s.replace(old, old + "\nstatic std::wstring pendingNotice;   // 11-19: shown after the reload (catalog)")

old = "+L\"}\";}post(s+L\"]}\");"
assert s.count(old) == 1, s.count(old)
s = s.replace(old, old + "if(!pendingNotice.empty()){post(L\"{\\\"type\\\":\\\"notice\\\",\\\"message\\\":\"+quote(pendingNotice)+L\"}\");pendingNotice.clear();}")

imp = r'''// 11-19 Claude: 「책 들여오기」 - the chosen EPUB files are copied into ko-library\books (books.ini untouched: a re-extracted patch keeps
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
'''
old = 'extern "C" void LibraryPick(int folder){\n'
assert s.count(old) == 1
s = s.replace(old, imp + old + "    if(folder==2){LibraryImport();return;}   // 11-19 「책 들여오기」\n")

old = 'else if(!wcscmp(msg,L"epub"))PostMessageW(host,LIB_PICK,0,0);'
assert s.count(old) == 1
s = s.replace(old, old + 'else if(!wcscmp(msg,L"import"))PostMessageW(host,LIB_PICK,2,0);')
W.write_text(s, encoding="utf-8")
print("patched", W)
