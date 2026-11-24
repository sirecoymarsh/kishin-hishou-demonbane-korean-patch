"""Show the two da0120ed lines the user reported (11-05) as the engine will lay them out, from a script.pak."""
import importlib.util
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
spec = importlib.util.spec_from_file_location("lc", str(Path(__file__).with_name("layout_check.py")))
sys.argv, args = sys.argv[:1], sys.argv[1:]
L = importlib.util.module_from_spec(spec)
spec.loader.exec_module(L)

for pak in args:
    a = L.K.parse_archive(Path(pak))
    for e in a.entries:
        if not e.name.lower().endswith("da0120ed.nps"):
            continue
        t = L.K.decode_stored(e, L.K.read_stored(a, e)).decode("cp932")
        print("==", pak)
        for key, box in (("20ed01202", 5), ("20ed01812", 12)):
            i = t.find(key)
            j = t.find("\r\r", i)
            for line in t[i:j].split("\r")[1:]:
                vis = L.visible(line)
                if not vis.strip():
                    continue
                x, rows, cur = 0, [], ""
                for c in vis:
                    d = L.px(c)
                    if x + d >= L.BOXW[box] and x > 0:
                        rows.append(cur)
                        cur, x = "", 0
                    cur += c
                    x += d
                rows.append(cur)
                for r in rows:
                    lead = len(r) - len(r.lstrip("　"))
                    print(f"   [{lead:2d} spaces] {L.ko(r.lstrip(chr(0x3000)))}")
