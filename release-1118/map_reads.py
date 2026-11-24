import re, sys
from pathlib import Path
from collections import Counter
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import kishinhisho_pak_fixed as P
g = Path(sys.argv[1])
log = (g / "autobattle" / "proxy_log.txt").read_bytes().decode("cp932", "replace").splitlines()
allreads = [ln for ln in log if "MPEGTRACE read" in ln]
arcs = {"0mpeg.pak": P.parse_archive(g / "0mpeg.pak"), "mpeg.pak": P.parse_archive(g / "mpeg.pak")}
for pid in sorted({ln.split()[3] for ln in allreads}):
    reads = [ln for ln in allreads if ln.split()[3] == pid]
    c = Counter(); first = {}; span = {}
    for ln in reads:
        m = re.search(r"read (\S+) (\d+) bytes at (\d+)", ln)
        pak, at = m[1].split(chr(92))[-1], int(m[3])
        a = arcs[pak]
        hit = "table/other"
        for e in a.entries:
            s = a.data_offset + e.offset
            if s <= at < s + e.size:
                hit = e.name
                lo, hi = span.get((pak, hit), (at - s, at - s))
                span[(pak, hit)] = (min(lo, at - s), max(hi, at - s))
        c[(pak, hit)] += 1
        first.setdefault((pak, hit), ln[11:19])
    print("pid", pid, "reads", len(reads))
    for k, v in c.items():
        print("  ", v, k, "first", first[k], "entry bytes", span.get(k))
