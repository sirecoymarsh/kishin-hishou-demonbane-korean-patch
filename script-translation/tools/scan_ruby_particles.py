"""Find ruby groups whose following particle only fits the base, not the
reading (or the reverse), e.g. base 압박감 / reading 프레셔 + next cell 이:
read with the reading it becomes 프레셔이.

Fix convention (package voice notes, Codex P0005): put the particle that
fits each side into the base cell and the reading cell, and remove it from
the next cell.  Writes work/ruby_particle_candidates.json as edit proposals
(cat 루비조사) for review; nothing is applied here.

  python -I -X utf8 scan_ruby_particles.py [responses_dir]
"""
import json
import re
import sys
from pathlib import Path

PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
CUR = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"P:\AI\khd-argon-review\out\responses")
OUT = Path(r"P:\AI\khd-argon-review\work\ruby_particle_candidates.json")

# (consonant-final form, vowel-final form); longest first when matching
COPULA_TAILS = ["라고", "라는", "란", "라서", "라도", "라면", "라", "야말로", "야", "여", "며", "나", "다",
                "지만", "지", "랑", "든", "고", "므로", "니까", "면", "냐", "니", "네", "군", "구나", "기에"]
PAIRS = [("이었", "였"), ("이에요", "예요")] + [("이" + t, t) for t in COPULA_TAILS] + [
    ("으로", "로"), ("이여", "여"), ("은", "는"), ("을", "를"), ("과", "와"), ("이", "가"), ("아", "야")]


def batchim(ch):
    """0 = no final consonant, 8 = ㄹ, other int = final consonant, None = not Hangul."""
    if not ("가" <= ch <= "힣"):
        return None
    return (ord(ch) - 0xAC00) % 28


def last_hangul(s):
    s = s.rstrip(" 　")
    return s[-1] if s and "가" <= s[-1] <= "힣" else None


def form_for(pair, word):
    cons, vow = pair
    b = batchim(last_hangul(word))
    if b is None:
        return None
    if pair == ("으로", "로"):
        return vow if b in (0, 8) else cons
    return vow if b == 0 else cons


def match_particle(after):
    """Return (pair, matched_text) if `after` starts with a batchim-dependent particle."""
    for cons, vow in PAIRS:
        for form in (cons, vow):
            if after.startswith(form):
                rest = after[len(form):]
                # the particle must end the word: next char is not Hangul (space, punct, newline, end)
                # except copula tails that themselves end in a verb ending are already full forms
                if rest and "가" <= rest[0] <= "힣":
                    continue
                return (cons, vow), form
    return None, None


def main():
    cur = {}
    for f in CUR.glob("*.json"):
        for r in json.loads(f.read_text(encoding="utf-8"))["records"]:
            cur[r["id"]] = r.get("ko")
    out = []
    for p in sorted((PKG / "packets").glob("*.json")):
        pkt = json.loads(p.read_text(encoding="utf-8"))
        for u in pkt["units"]:
            segs = u["segments"]
            for i in range(len(segs) - 2):
                r, b, a = segs[i], segs[i + 1], segs[i + 2]
                if r["kind"] != "ruby_reading" or b["kind"] != "text" or a["kind"] != "text":
                    continue
                if b.get("source_line") != a.get("source_line"):
                    continue
                reading, base, after = cur.get(r["id"]) or "", cur.get(b["id"]) or "", cur.get(a["id"]) or ""
                if not reading.strip() or not base or not after or reading == base:
                    continue
                lr, lb = last_hangul(reading), last_hangul(base)
                if lr is None or lb is None:
                    continue
                pair, found = match_particle(after)
                if not pair:
                    continue
                fr, fb = form_for(pair, reading), form_for(pair, base)
                if fr == fb:
                    continue
                out.append({
                    "unit": u["id"], "speaker": u.get("speaker_jp"), "jp": u["source_context_with_tags"],
                    "reading_id": r["id"], "base_id": b["id"], "after_id": a["id"],
                    "reading": reading, "base": base, "after": after, "particle": found,
                    "edits": [
                        {"sid": r["id"], "old": reading, "new": reading.rstrip() + fr, "cat": "루비조사",
                         "reason": f"읽기로 읽으면 '{reading}{found}'가 됨. 조사를 읽기·본문 칸에 각각 넣음"},
                        {"sid": b["id"], "old": base, "new": base.rstrip() + fb, "cat": "루비조사",
                         "reason": "위와 짝"},
                        {"sid": a["id"], "old": after, "new": after[len(found):], "cat": "루비조사",
                         "reason": "조사를 앞 두 칸으로 옮김"},
                    ],
                })
    OUT.write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    print("candidates:", len(out))
    for c in out:
        print(f"{c['unit']} | {c['reading']}/{c['base']} + {c['after'][:16]!r} → "
              f"{c['edits'][0]['new']}/{c['edits'][1]['new']} + {c['edits'][2]['new'][:14]!r}")


if __name__ == "__main__":
    main()
