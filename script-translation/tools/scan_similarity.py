"""Content-baseline scan: compare the current Korean of each unit with the
existing game translation (a different, independent Korean translation).

Low similarity where neighbours are high suggests invented content, a shift,
or a broken old line.  Writes work/scan_similarity.tsv sorted by suspicion.
"""
import difflib
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from load import load_packet, packets

OUT = Path(r"P:\AI\khd-argon-review\work\scan_similarity.tsv")
NORM = re.compile(r"[\s\u3000「」『』（）()…―─!?！？.,、。・·~～\-]")


def norm(s):
    return NORM.sub("", s or "")


def sim(a, b):
    a, b = norm(a), norm(b)
    if not a or not b:
        return None
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()


def main():
    rows = []
    for pid in packets():
        pkt, resp, ko, ko0, rvu = load_packet(pid)
        units = pkt["units"]
        vals = []
        for u in units:
            r = rvu.get(u["id"], {})
            cur = "".join((ko.get(s["id"]) or "") for s in u["segments"] if s["kind"] == "text")
            old = r.get("existing_game_translation")
            vals.append((u, r, cur, old, sim(cur, old)))
        for i, (u, r, cur, old, s) in enumerate(vals):
            if s is None:
                continue
            neigh = [v[4] for v in vals[max(0, i - 3):i] + vals[i + 1:i + 4] if v[4] is not None]
            nmean = sum(neigh) / len(neigh) if neigh else 0
            jp = u["source_text"]
            if len(norm(jp)) >= 12 and s < 0.3:
                rows.append((round(nmean - s, 3), round(s, 3), round(nmean, 3), pid, u["id"],
                             r.get("source_reviewed"), r.get("existing_alignment"),
                             jp, cur, old))
    rows.sort(key=lambda x: -x[0])
    with OUT.open("w", encoding="utf-8") as f:
        f.write("gap\tsim\tneigh\tpid\tuid\treviewed\talign\tjp\tcur\told\n")
        for r in rows:
            f.write("\t".join(str(x).replace("\t", " ").replace("\n", "⏎") for x in r) + "\n")
    print("low-sim units:", len(rows))
    for r in rows[:45]:
        print(r[0], r[1], r[3], r[4], r[5], r[6])
        print("   JP:", r[7][:110].replace("\n", "⏎"))
        print("   KO:", r[8][:110].replace("\n", "⏎"))
        print("   OLD:", (r[9] or "")[:110].replace("\n", "⏎"))


if __name__ == "__main__":
    main()
