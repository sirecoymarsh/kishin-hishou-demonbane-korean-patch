"""배포 폴더의 글·실행 파일에서 이 PC 경로·계정 이름이 박혀 있는지 훑는다(바이트, UTF-16 둘 다)."""
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
root = Path(sys.argv[1])
words = ["P:\\AI", "F:\\AI", "Y:\\AI", "C:\\Users", "P:/AI", "F:/AI", "Y:/AI", "CadriacLord", "direabi", "claude-work",
         "khd-latest-game", "khd-fullrun", "Codex\\projects"]
pats = []
for w in words:
    pats.append((w, re.compile(re.escape(w.encode("utf-8")), re.I)))
    pats.append((w + " (utf16)", re.compile(re.escape(w.encode("utf-16-le")), re.I)))
skip = {".pak", ".epub", ".png", ".jpg", ".otf", ".mpg", ".dat"}
hits = {}
for p in sorted(root.rglob("*")):
    if not p.is_file() or p.suffix.lower() in skip:
        continue
    b = p.read_bytes()
    for w, rx in pats:
        for m in rx.finditer(b):
            ctx = b[max(0, m.start() - 40):m.end() + 60]
            if "utf16" in w:
                ctx = ctx.decode("utf-16-le", "replace")
            else:
                ctx = ctx.decode("utf-8", "replace")
            hits.setdefault(str(p.relative_to(root)), []).append((w, ctx.replace("\n", " ").replace("\r", " ")))
for k, v in hits.items():
    print(k, len(v))
    for w, c in v[:3]:
        print("   ", w, "|", c[:140])
print("files with hits:", len(hits))
