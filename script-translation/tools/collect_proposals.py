"""Collect agent proposal files into one numbered review sheet.

  python -I -X utf8 collect_proposals.py            # summary + writes work/proposals_all.json
  python -I -X utf8 collect_proposals.py show CAT [start] [count]
        CAT = all | 오역 | 누락 | 미번역 | 한자어 | 글자 | 용어 | 대명사 | 말투 | 번역투 | 띄어쓰기 | 일관성 | 기타 | B | A

Each proposal gets a stable number (agent order, then file order).  The
sheet shows segment JP, the current Korean, old→new and the reason, so a
decision can be made without opening the packet.
"""
import json
import sys
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review")
AG = ROOT / "work" / "agents"
PKG = ROOT / "pkg" / "KishinHisho_Claude_Review_20261031"
CUR = ROOT / "out" / "responses"


def jload(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


def build():
    seg = {}
    for p in (PKG / "packets").glob("*.json"):
        for u in jload(p)["units"]:
            for s in u["segments"]:
                seg[s["id"]] = (u, s)
    cur = {}
    for p in CUR.glob("*.json"):
        for r in jload(p)["records"]:
            cur[r["id"]] = r.get("ko")
    rows = []
    for f in sorted(AG.glob("a[0-9]*.json")):
        d = jload(f)
        for i, e in enumerate(d.get("edits", [])):
            u, s = seg.get(e["sid"], ({}, {}))
            rows.append({
                "n": len(rows) + 1, "agent": f.stem, "i": i, **e,
                "unit": u.get("id"), "speaker": u.get("speaker_jp"),
                "jp_seg": s.get("jp"), "jp_unit": u.get("source_text"),
                "ko_cur": cur.get(e["sid"]),
            })
    (ROOT / "work" / "proposals_all.json").write_text(
        json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")
    return rows


def short(s, n=160):
    s = (s or "").replace("\n", "⏎")
    return s if len(s) <= n else s[:n] + "…"


def show(rows, cat, start, count):
    sel = [r for r in rows if cat == "all" or r.get("cat") == cat or r.get("grade") == cat]
    for r in sel[start:start + count]:
        print(f"#{r['n']} [{r['agent']} {r.get('grade')} {r.get('cat')}] {r['sid']} {r['speaker'] or '지문'}")
        jp = r["jp_seg"] if len(r.get("jp_unit") or "") > 220 else r["jp_unit"]
        print("  JP:", short(jp, 220))
        print("  KO:", short(r["ko_cur"], 220))
        if r.get("op") == "prefix":
            print(f"  →  앞에 {r['new']!r} 붙임")
        else:
            print(f"  →  「{short(r.get('old'), 120)}」 ⇒ 「{short(r.get('new'), 120)}」")
        print("  ∵", short(r.get("reason"), 200))


def main():
    rows = build()
    if len(sys.argv) > 1 and sys.argv[1] == "show":
        cat = sys.argv[2] if len(sys.argv) > 2 else "all"
        start = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        count = int(sys.argv[4]) if len(sys.argv) > 4 else 40
        show(rows, cat, start, count)
        return
    import collections
    print("proposals:", len(rows))
    print("by agent:", dict(collections.Counter(r["agent"] for r in rows)))
    print("by cat:", dict(collections.Counter(r.get("cat") for r in rows)))
    print("by grade:", dict(collections.Counter(r.get("grade") for r in rows)))
    for f in sorted(AG.glob("a[0-9]*.json")):
        d = jload(f)
        print(f.stem, "reviewed_units:", len(d.get("reviewed_units", [])), "questions:", len(d.get("questions", [])))


if __name__ == "__main__":
    main()
