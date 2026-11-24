from pathlib import Path
p = Path(r"C:\Users\CadriacLord\.claude\projects\P--AI\memory\MEMORY.md")
t = p.read_text(encoding="utf-8")
bs = chr(92)
line = "- [기신비상 11-18 배포 꾸러미](khd-release-20261118.md) — 본 패치·소설 6권(설치형)·고화질 셋, 정본 Y:" + bs + "AI" + bs + "claude-work" + bs + "khd-release-1118" + bs + "진행.md; **시험 뒤 창 위치 레지스트리 확인**\n"
if "khd-release-20261118" not in t:
    i = t.find("- [기신비상 합본 사본")
    assert i >= 0
    t = t[:i] + line + t[i:]
    p.write_text(t, encoding="utf-8")
print("ok")
