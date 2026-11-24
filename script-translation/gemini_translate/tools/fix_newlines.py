"""Line breaks Gemini doubled: the source keeps the break and indent between two cells in the raw gap (outside every
cell, e.g. "<k>\\r　" or "\\r　　　<r text=..."), and Gemini wrote the same break into the cell as well, so the game
would show an empty line and a doubled indent. A trailing (or leading) newline run is stripped only when the cell's
JP has none there and the raw gap on that side already holds a line break; anything else is listed for hand review.
A few hand fixes ride along (a break Gemini moved into the middle of a line, two dialogue breaks it dropped, two
spacing slips). Every change goes to fix_log.jsonl.

  python -I -X utf8 fix_newlines.py --dry   # list only
  python -I -X utf8 fix_newlines.py
"""
import glob
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
DRY = "--dry" in sys.argv
TAIL = re.compile(r"\n[\n\u3000 ]*$")
HEAD = re.compile(r"^\n[\n\u3000 ]*")
HAND = {  # sid: (old, new, why)
    "da1320:T00028": (".\n           \u3000나의 전우여.", ". 나의 전우여.", "원문은 「よ。我が戦友よ。」가 한 줄 — 넣은 줄바꿈 뺌"),
    "da1314:T00063": ("격파하는 자 ", "격파하는 자", "서술격 조사 「이니」(루비 밑글자 칸)를 띄어 썼음"),
    "da1401:T00010": (" 말 할 처지는", " 말할 처지는", "띄어쓰기(말하다)"),
    # the break and 17 indents sit in the raw gap before the ruby reading, so the gap check below cannot see them
    "da1316:T00124": ("\n" + "　" * 17 + "공허", "공허", "루비 앞 원문 틈의 줄바꿈·들여쓰기를 칸 앞에 겹쳐 씀 — 칸 쪽을 뺌"),
    # the source breaks the line before よりも (in the gap), so 보다도 moves up into both ruby cells
    "da1314:R00078": ("레플리카", "레플리카보다도", "원문 줄바꿈이 「よりも」 앞 틈에 있어 조사를 루비 두 칸으로 옮김"),
    "da1314:T00276": ("모조품", "모조품보다도", "원문 줄바꿈이 「よりも」 앞 틈에 있어 조사를 루비 두 칸으로 옮김"),
    "da1314:T00277": ("보다도 조악한\n　", "조악한 ", "원문 틈에 이미 줄바꿈이 있어 칸 끝 줄바꿈 대신 낱말 띄어쓰기"),
    "da9901:T00071": ("좋은 아침입니다아! 좋은 아침이로군요오오!", "좋은 아침입니다아!\n\u3000좋은 아침이로군요오오!",
                      "원문의 대사 줄바꿈(\\n　) 되살림"),
}

raw_cache = {}


def raw(fname):
    if fname not in raw_cache:
        c = glob.glob(str(PKG / "source_jp_A_cp932" / (fname + "*")))
        raw_cache[fname] = open(c[0], "rb").read() if c else b""
    return raw_cache[fname]


log = None if DRY else open(LOG, "a", encoding="utf-8")
review = []
total = 0
for f in sorted((ROOT / "responses").glob("P*.json")):
    pid = f.stem
    pkt = json.loads((PKG / "packets" / f"{pid}.json").read_text(encoding="utf-8"))
    resp = json.loads(f.read_text(encoding="utf-8"))
    rec = {r["id"]: r for r in resp["records"]}
    n = 0

    def setko(sid, new, why):
        global n, total
        before = rec[sid]["ko"]
        if new == before:
            return
        print(f"  {sid}: {before!r:.60} -> {new!r:.60}  ({why})")
        if not DRY:
            log.write(json.dumps({"packet": pid, "id": sid, "field": "ko", "before": before, "after": new, "why": why,
                                  "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")
            rec[sid]["ko"] = new
        n += 1
        total += 1

    for u in pkt["units"]:
        segs = u["segments"]
        rb = raw(u["file"])
        for i, s in enumerate(segs):
            r = rec.get(s["id"])
            if r is None or not isinstance(r.get("ko"), str) or r["ko"].startswith("[BROKEN"):
                continue
            k = r["ko"]
            if s["id"] in HAND:
                old, new, why = HAND[s["id"]]
                if k.count(old) == 1:
                    k = k.replace(old, new)
                    setko(s["id"], k, why)
                else:
                    review.append(f"{s['id']}: 손 고침 old 가 칸에 {k.count(old)}번")
            m = TAIL.search(k)
            if m and not s["jp"].endswith("\n"):
                gap = rb[s["source_byte_end"]:segs[i + 1]["source_byte_start"]] if i + 1 < len(segs) else b""
                if b"\r" in gap or b"\n" in gap:
                    k = k[:m.start()]
                    setko(s["id"], k, "원문 틈에 이미 있는 줄바꿈·들여쓰기를 칸 끝에 겹쳐 씀 — 칸 쪽을 뺌")
                else:
                    review.append(f"{s['id']}: 칸 끝 줄바꿈인데 원문 틈에 줄바꿈 없음 {k[-30:]!r} / 틈 {gap[:30]!r}")
            m = HEAD.match(k)
            if m and not s["jp"].startswith("\n"):
                gap = rb[segs[i - 1]["source_byte_end"]:s["source_byte_start"]] if i > 0 else b""
                if b"\r" in gap or b"\n" in gap:
                    k = k[m.end():]
                    setko(s["id"], k, "원문 틈에 이미 있는 줄바꿈·들여쓰기를 칸 앞에 겹쳐 씀 — 칸 쪽을 뺌")
                else:
                    review.append(f"{s['id']}: 칸 앞 줄바꿈인데 원문 틈에 줄바꿈 없음 {k[:30]!r} / 틈 {gap[-30:]!r}")
    if n and not DRY:
        f.write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
    if n:
        print(pid, "고친 칸", n)
print("고친 칸 합계:", total, "(시험 실행)" if DRY else "")
print("손으로 볼 것:", len(review))
for x in review:
    print("  ", x)
