"""Claude's last fixes on the Gemini translations: missing word spaces at cell joins (hand-judged list) and ruby
particle pairing (particle that fits each side goes into the reading cell and the base cell; the next cell loses it —
skipped when the next cell would be left blank). Every change goes to fix_log.jsonl.

  python -I -X utf8 fix_spaces_ruby.py
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
import scan_ruby_particles as srp  # noqa: E402

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
LOG = ROOT / "fix_log.jsonl"
BY = "Claude Opus 5.5 (claude-opus-5-5)"
# right-hand cell of each join that needs a word space (judged one by one from join_candidates.json)
SPACE = {"da0603:T00027", "da0603:T00028", "da0603:T00029", "da0605:T00015", "da1313:T00048", "da1313:T00050",
         "da1313:T00071", "da1313:T00072", "da1313:T00075", "da1313:T00076", "da1313:T00116", "da1313:T00197",
         "da1314:T00046", "da1314:T00050", "da1314:T00247", "da1314:T00248", "da1314:T00255", "da1314:T00257",
         "da1314:T00276", "da1314:T00281", "da1314:T00293", "da1315:T00070", "da1315:T00117", "da1316:T00030",
         "da1316:T00054", "da1317:T00008", "da1317:T00126", "da1317:T00157", "da1320:T00098", "da1320:T00101",
         "da9901:T00020", "da9901:T00036", "da9901:T00049", "da9902:T00008", "da9902:T00037", "da9902:T00044",
         "da9902:T00045", "da9902:T00051", "da9902:T00057", "da9902:T00076", "da9903:T00007", "da9903:T00008",
         "da9906ed:T00008"}
EXTRA = {"da9901:T00064": ("28호", "28호 ", "모델 이름 앞뒤 띄어쓰기(루비 밑글자에 공백을 넣지 않으려 앞 칸에)"),
         "da1317:T00008": ("아닌것인가", "아닌 것인가", "칸 안 띄어쓰기")}

log = open(LOG, "a", encoding="utf-8")
left_over = []


def write_log(pid, sid, before, after, why):
    log.write(json.dumps({"packet": pid, "id": sid, "field": "ko", "before": before, "after": after, "why": why,
                          "by": BY, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}, ensure_ascii=False) + "\n")


for f in sorted((ROOT / "responses").glob("P*.json")):
    pid = f.stem
    pkt = json.loads((PKG / "packets" / f"{pid}.json").read_text(encoding="utf-8"))
    resp = json.loads(f.read_text(encoding="utf-8"))
    rec = {r["id"]: r for r in resp["records"]}
    n = 0

    def setko(sid, new, why):
        global n
        before = rec[sid]["ko"]
        if new != before:
            write_log(pid, sid, before, new, why)
            rec[sid]["ko"] = new
            n += 1

    for sid, (old, new, why) in EXTRA.items():
        if sid in rec and isinstance(rec[sid].get("ko"), str) and rec[sid]["ko"].count(old) == 1 and new not in rec[sid]["ko"]:
            setko(sid, rec[sid]["ko"].replace(old, new), why)
    for u in pkt["units"]:
        segs = u["segments"]
        texts = [s for s in segs if s["kind"] == "text"]
        ruby_base = {segs[i + 1]["id"] for i, s in enumerate(segs) if s["kind"] == "ruby_reading" and i + 1 < len(segs)}
        for a, b in zip(texts, texts[1:]):
            if b["id"] not in SPACE:
                continue
            ka, kb = rec[a["id"]]["ko"], rec[b["id"]]["ko"]
            if ka.endswith((" ", "\u3000")) or kb.startswith((" ", "\u3000")):
                continue
            if b["id"] in ruby_base and a["id"] not in ruby_base:
                setko(a["id"], ka + " ", "낱말 띄어쓰기(루비 밑글자 앞 칸 끝에)")
            else:
                setko(b["id"], " " + kb, "낱말 띄어쓰기(칸 경계에서 붙어 보임)")
        for i, s in enumerate(segs):  # ruby particle pairing
            if s["kind"] != "ruby_reading" or i + 2 >= len(segs):
                continue
            base, after = segs[i + 1], segs[i + 2]
            if after["kind"] == "ruby_reading":
                continue
            kr, kb, ka = rec[s["id"]].get("ko") or "", rec[base["id"]].get("ko") or "", rec[after["id"]].get("ko") or ""
            if not kr or not (re.search(r"[가-힣]", kb) and re.search(r"[가-힣]", kr)):
                continue
            pair, form = srp.match_particle(ka)
            if not pair:
                continue
            fb, fr = srp.form_for(pair, kb), srp.form_for(pair, kr)
            if not fb or not fr or fb == fr:
                continue
            rest = ka[len(form):]
            if not rest.strip():
                left_over.append(f"{after['id']}: 뒤 칸이 비게 되어 건너뜀 — 본문 「{kb}」/읽기 「{kr}」 + 「{ka}」")
                continue
            setko(s["id"], kr + fr, "루비 조사 짝(읽기 칸에 읽기에 맞는 조사)")
            setko(base["id"], kb + fb, "루비 조사 짝(본문 칸에 본문에 맞는 조사)")
            setko(after["id"], rest, "루비 조사 짝(조사를 앞 두 칸으로 옮김)")
    if n:
        f.write_text(json.dumps(resp, ensure_ascii=False, indent=2), encoding="utf-8")
        print(pid, "고친 칸", n)
print("건너뛴 루비 조사:", len(left_over))
for x in left_over:
    print("  ", x)
