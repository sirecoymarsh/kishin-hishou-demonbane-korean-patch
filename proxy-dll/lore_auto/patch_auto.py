"""11-19 사용자 「다른 관련 도서도 더 제작해 넣고 싶은데 표지를 자동으로 읽진 않는다고?」→「그렇게 바꿔」: 서고에 EPUB 파일만 넣으면 뜨게.
lore_auto\\src(= 설치본 f1e4810c 와 바이트가 같게 빌드되는 소스)에:
 1) loadbooks — books.ini 의 책 뒤에 ko-library\\books\\*.epub 가운데 목록에 없는 것을 이름순으로(StrCmpLogicalW). 제목은 파일 이름(화면이 EPUB 의
    dc:title 로 바꿈), Id 는 "file-" + 파일 이름의 해시(가상 호스트 이름에 들어가므로 한글·빈칸도 안전, 폴더를 옮겨도 읽던 위치가 남음).
 2) savebook — 「책 더하기」 칸 번호를 목록 전체(이제 폴더에서 찾은 책 포함)가 아니라 books.ini 의 Count 로.
 3) 목록(catalog)에 EPUB 파일 책의 가상 주소 url — 화면(library.js)이 그 주소로 표지·제목을 읽는다.
  python -I -X utf8 patch_auto.py"""
from pathlib import Path

W = Path(__file__).resolve().parent / "src" / "library_webview.cpp"
s = W.read_text(encoding="utf-8")

old1 = "if(*p){wchar_t id[128];GetPrivateProfileStringW(section,L\"Id\",L\"\",id,128,ini.c_str());std::wstring path=p;if(PathIsRelativeW(p))path=root+L\"\\\\\"+path;books.push_back({path,*t?t:titleof(p),id});}}\n}"
assert s.count(old1) == 1, "loadbooks tail"
new1 = old1[:-2] + """
    // 11-19 Claude: every EPUB file in ko-library\\books is on the shelf too (after the books.ini ones, by name) - a new book is one file
    std::vector<std::wstring> found;WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((root+L"\\\\books\\\\*.epub").c_str(),&d);
    if(f!=INVALID_HANDLE_VALUE){do{if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!_wcsicmp(PathFindExtensionW(d.cFileName),L".epub"))found.push_back(d.cFileName);}while(FindNextFileW(f,&d));FindClose(f);}
    std::sort(found.begin(),found.end(),[](const std::wstring &a,const std::wstring &b){return StrCmpLogicalW(a.c_str(),b.c_str())<0;});
    for(auto &name:found){
        if(books.size()>=256)break;
        std::wstring path=root+L"\\\\books\\\\"+name;bool listed=false;
        for(auto &b:books)if(!_wcsicmp(b.path.c_str(),path.c_str())){listed=true;break;}
        if(!listed)books.push_back({path,name.substr(0,name.size()-5),L"file-"+bookid(name)});
    }
}"""
s = s.replace(old1, new1)

# bookid is defined after loadbooks - declare it first
old_decl = "static void loadbooks(){"
assert s.count(old_decl) == 1
s = s.replace(old_decl, "static std::wstring bookid(const std::wstring &p);\nstatic void loadbooks(){")

old2 = "wchar_t section[32],count[32];swprintf(section,32,L\"Book%u\",(unsigned)books.size());swprintf(count,32,L\"%u\",(unsigned)books.size()+1);"
assert s.count(old2) == 1, "savebook"
new2 = ("unsigned own=(unsigned)GetPrivateProfileIntW(L\"Library\",L\"Count\",0,ini.c_str());"     # 11-19: books.ini's own count
        "wchar_t section[32],count[32];swprintf(section,32,L\"Book%u\",own);swprintf(count,32,L\"%u\",own+1);")
s = s.replace(old2, new2)

old3 = "+L\",\\\"available\\\":\"+(exists(b.path)?L\"true\":L\"false\")+L\"}\";}post(s+L\"]}\");"
assert s.count(old3) == 1, "catalog"
new3 = ("+L\",\\\"available\\\":\"+(exists(b.path)?L\"true\":L\"false\")"
        "+L\",\\\"url\\\":\"+quote(!directory(b.path)&&isepub(b.path)?L\"https://\"+domain(b)+L\"/\"+urlpart(titleof(b.path)):L\"\")+L\"}\";}post(s+L\"]}\");")
s = s.replace(old3, new3)
W.write_text(s, encoding="utf-8")
print("patched", W)
