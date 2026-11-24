"""Check a review agent's proposal file before hand-in.

  python -I -X utf8 check_proposals.py P:\\AI\\khd-argon-review\\work\\agents\\a1.json

For every edit: the record exists; "old" occurs exactly once in the current
Korean (out/responses, applied in file order so several edits to one record
chain correctly); the result contains only characters the game can draw
(Hangul in the CP932 Han carrier map with a real glyph, other characters
encodable in CP932 and not a carrier kanji, no compatibility jamo).
Exit code 1 on any error.
"""
import json
import sys
from pathlib import Path

from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont

CUR = Path(r"P:\AI\khd-argon-review\out\responses")
RETL = Path(r"P:\AI\khd-retl")

_map = json.loads((RETL / "cp932_han_carrier_map_ext.json").read_text(encoding="utf-8"))
_rows = _map["mappings"] if isinstance(_map, dict) else _map
CAR = {r["target"]: r["carrier"] for r in _rows}
CARRIERS = set(CAR.values())
_font = TTFont(RETL / "KHDKorean-CP932Han-Ext.otf")
_cmap = _font.getBestCmap()
_gs = _font.getGlyphSet()
_glyph_cache = {}


def drawable(ch):
    """Return None if the game can draw ch, else a reason string."""
    if ch in "\n":
        return None
    if "가" <= ch <= "힣":
        if ch not in CAR:
            return "캐리어 표에 없는 음절"
        cp = ord(CAR[ch])
        if cp not in _glyph_cache:
            g = _cmap.get(cp)
            ok = False
            if g is not None:
                pen = BoundsPen(_gs)
                _gs[g].draw(pen)
                ok = pen.bounds is not None
            _glyph_cache[cp] = ok
        return None if _glyph_cache[cp] else "글꼴에 글립 없음"
    if "\u3130" <= ch <= "\u318f" or "\u1100" <= ch <= "\u11ff":
        return "한글 자모는 못 그림"
    try:
        ch.encode("cp932")
    except UnicodeEncodeError:
        return "CP932에 없는 문자"
    if ch in CARRIERS:
        return "캐리어로 쓰이는 한자(한글로 바뀌어 보임)"
    return None


def main():
    path = Path(sys.argv[1])
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    edits = data["edits"] if isinstance(data, dict) else data
    cur = {}
    for f in CUR.glob("*.json"):
        for r in json.loads(f.read_text(encoding="utf-8"))["records"]:
            cur[r["id"]] = r.get("ko")
    errors = []
    for i, e in enumerate(edits):
        sid = e.get("sid")
        where = f"#{i} {sid}"
        if sid not in cur:
            errors.append(f"{where}: 없는 칸 ID")
            continue
        ko = cur[sid]
        if not isinstance(ko, str):
            errors.append(f"{where}: 이 칸은 원문 바이트 보존 칸이라 고칠 수 없음")
            continue
        for key in ("new", "cat", "reason"):
            if key not in e:
                errors.append(f"{where}: '{key}' 없음")
        if "new" not in e:
            continue
        if e.get("op") == "prefix":
            if ko.startswith(e["new"]):
                errors.append(f"{where}: 이미 그 문자로 시작함")
                continue
            new = e["new"] + ko
        else:
            old = e.get("old")
            if not old:
                errors.append(f"{where}: 'old' 없음")
                continue
            n = ko.count(old)
            if n != 1:
                errors.append(f"{where}: old가 현재 칸에 {n}번 나옴 (정확히 1번이어야 함). 현재 칸: {ko!r}")
                continue
            if old == e["new"]:
                errors.append(f"{where}: old와 new가 같음")
                continue
            new = ko.replace(old, e["new"])
        bad = sorted({(c, drawable(c)) for c in new if drawable(c)})
        if bad:
            errors.append(f"{where}: 그릴 수 없는 글자 " + ", ".join(f"{c!r}({why})" for c, why in bad))
            continue
        cur[sid] = new
    print(f"edits: {len(edits)}  errors: {len(errors)}")
    for x in errors:
        print("  ", x)
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
