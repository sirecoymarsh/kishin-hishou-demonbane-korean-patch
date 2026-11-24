from pathlib import Path
import re
p = Path(r"P:\AI\khd-hero-ai\v30\src\dinput8_proxy.cpp"); t = p.read_text(encoding="utf-8")
s = t.index('        if (want && MessageBoxW(GetAncestor(h, GA_ROOT),')
e = t.index('L"모든 콘텐츠 열기", MB_YESNO', s)
bs = chr(92)
new = ('        if (want && MessageBoxW(GetAncestor(h, GA_ROOT),\n'
       '                L"감상실의 CG·DUEL 기체·스토리 차트·극장·피니셔와 DUEL 정보 창의 잠긴 칸을 모두 엽니다.' + bs + 'n"\n'
       '                L"이야기 뒷부분의 내용이 보일 수 있습니다.' + bs + 'n' + bs + 'n"\n'
       '                L"세이브와 진행 기록은 바꾸지 않으므로, 이 칸을 끄면 원래대로 다시 잠깁니다.' + bs + 'n"\n'
       '                L"모든 콘텐츠를 열까요?", ')
t = t[:s] + new + t[e:]
p.write_text(t, encoding="utf-8")
print(t[s:s + 700])
