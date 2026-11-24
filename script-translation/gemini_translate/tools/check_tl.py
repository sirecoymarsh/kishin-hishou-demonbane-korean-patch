"""Check Gemini translations of the 33 untranslated packets (gemini_translate/responses/<packet>.json).

  python -I -X utf8 check_tl.py                  # every response present
  python -I -X utf8 check_tl.py P0062_da0601     # one packet
  python -I -X utf8 check_tl.py --notes          # also write/remove 되돌림\<packet>.txt

Hard = the kit validator's errors + rules the user fixed (terms, traps, dialogue pronouns, Al's 짐, hanja,
undrawable glyphs, repeated contexts, broken engine tags). Soft = ruby particle mismatches and translationese counts
(reviewed by Claude afterwards, not sent back).
"""
import collections
import importlib.util
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from check_proposals import drawable  # noqa: E402  (loads the game font)
import scan_ruby_particles as srp  # noqa: E402

ROOT = Path(r"P:\AI\khd-argon-review\gemini_translate")
PKG = Path(r"P:\AI\khd-argon-review\pkg\KishinHisho_Claude_Review_20261031")
KIT = Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\데몬베인 시리즈 2\delivery\KishinHisho_JP_Gemini_Argon_20261030")
_spec = importlib.util.spec_from_file_location("kitval", KIT / "validate_response.py")
kitval = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kitval)
REPEATS = {r["unit"]: r for r in json.loads(Path(r"P:\AI\khd-argon-review\work\kit_missing\repeats.json").read_text(encoding="utf-8"))
           if r["match"] == "unit"}
BROKEN = {"da9901:T00077", "dh_ex_sound:T00001"}
BROKEN_KO = "[BROKEN_ENGINE_TAG_DO_NOT_REPLACE]"
# (JP in the unit, KO the unit must contain, KO the unit must not contain)
TERMS = [("覇道", "하도", r"하도우|패도"), ("ブラックロッジ", "블랙 로지", r"롯지"), ("紅の少女", "붉은 소녀", r"진홍의 소녀|진홍빛 소녀"),
         ("蠱毒", "고독", r"독벌레"), ("外道", "외도", None), ("瘴気", "독기", r"장기"), ("九朔", r"쿠자쿠|구삭", r"쿠자루|구자쿠"),
         ("アズラッド", "아즈라드", r"아즈랏드"), ("リベル・レギス", "리베르 레기스", r"리벨"), ("偃月刀", "언월도", None),
         ("ダンウィッチ", "더니치", r"던위치"), ("トラペゾヘドロン", "트라페조헤드론", None), ("親父殿", "아버님", None),
         ("母上", "어머님", None), ("魔刃", "마검", r"마인"), ("鋼造", "고조", r"코우조우"), ("ズアウィア", "즈아위아", None),
         ("字祷子", "자도자", None), ("怨敵", "원적", None),  # 怨敵: 사용자 11-01 결정 12
         ("妖蛆の秘密", "해충의 신비", r"요충"),  # 사용자 11-01 결정 7
         ("爆神地", "신의 폭심지", r"폭신지"),  # 사용자 11-01 결정 8
         ("久遠", "영원", r"구원"), ("念話", "텔레파시", r"염화")]  # 사용자 11-01 결정 4·6
TRAPS = [("唐突", "당돌"), ("人影", "인영"), ("芳香", "방향"), ("緋色", "비색"), ("紫電", "자전"), ("防戦", "방전"),
         ("校舎", "교사"), ("全機", "전기"), ("光柱", "광주"), ("受肉", "수육"), ("本命", "본명"), ("鏖殺", "오살"),
         ("劣情", "열정"), ("星気", "성기"), ("大冊", "대책"), ("愚物", "우물"), ("静謐", "정밀"), ("不敵", "불적"),
         ("屍骸", "시해"), ("四散", "사산"), ("無貌", "무모"), ("苛ま", "가책"), ("呪詛", "주저|주소"), ("奔流", "분류"),
         ("脚部", "각부"), ("機影", "기영"), ("敵影", "적영"), ("全霊", "전령"), ("忙殺", "망살"), ("秘め", "비장")]
PRON = re.compile(r"그녀|(?<![가-힣])그(?:는|가|를|의|에게|와|도)(?![가-힣])")
ZIM = re.compile(r"(?<![가-힣])짐(?:은|이|의|을|에게|도|만|이야말로)?(?![가-힣])")
HANJA = re.compile(r"[\u4e00-\u9fff]")
KEEP_HANJA = {"da9903:T00029"}   # \ub974\ub93c\uc5d0 \uc774\ubcf8 \uc8fc\ubb38 \ubc11\uae00\uc790 = \uc6d0\ubb38 \uc911\uad6d\uc5b4(\ub8e8\ube44 = \ubc1c\uc74c, \ud574\uc11d\uc740 \uad04\ud638)
KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa]")  # \u30fb and \u30fc are left out
TL = {"것이다": r"[가-힣] 것이(?:다|었다)", "에 의해": r"에 의(?:해|한)", "동시에": r"(?:함|과|와) 동시에", "로부터": r"로부터",
      "수량+의": r"(?:한|두|세|네|다섯|여섯) (?:개|자루|대|권|명)의 "}
raw_cache = {}


def raw(fname):
    if fname not in raw_cache:
        c = list((PKG / "source_jp_A_cp932").glob(fname + "*"))
        raw_cache[fname] = c[0].read_bytes() if c else b""
    return raw_cache[fname]


def has_break(gap):
    return b"\r" in gap or b"\n" in gap


def load(pid):
    pkt = json.loads((PKG / "packets" / f"{pid}.json").read_text(encoding="utf-8"))
    return pkt


def check(pid):
    pkt = load(pid)
    hard, soft = [], []
    p = ROOT / "responses" / f"{pid}.json"
    try:
        resp = json.loads(p.read_text(encoding="utf-8-sig"))
    except Exception as e:  # noqa: BLE001
        return {"pid": pid, "hard": [f"JSON을 읽을 수 없음: {e}"], "soft": [], "tl": {}}
    v = kitval.validate(pkt, resp)
    hard += [f"구조: {e}" for e in v.get("errors", [])]
    ko = {r.get("id"): r.get("ko") for r in resp.get("records", []) if isinstance(r, dict)}
    tl = collections.Counter()
    for u in pkt["units"]:
        segs = u["segments"]
        sp = u.get("speaker_jp") or "(지문)"
        ujp = "".join(s["jp"] for s in segs)
        uko = "".join(ko.get(s["id"]) or "" for s in segs)
        for s in segs:
            k = ko.get(s["id"])
            if not isinstance(k, str):
                continue
            if s["id"] in BROKEN:
                if k != BROKEN_KO:
                    hard.append(f"{s['id']}: 깨진 엔진 명령 칸은 ko를 {BROKEN_KO} 로 둔다")
                continue
            bad = sorted({c for c in k if drawable(c)})
            if bad:
                hard.append(f"{s['id']}: 게임이 못 그리는 글자 {bad}")
            # only the garbled laugh may keep noise kanji, and the R'lyeh chant its original Chinese (user 11-01)
            hz = [c for c in HANJA.findall(k) if not ((sp == "ズアウィア" or s["id"] in KEEP_HANJA) and c in s["jp"])]
            if hz:
                hard.append(f"{s['id']}: 한자를 씀 {sorted(set(hz))} (한자 괄호·한자 표기 금지)")
            kn = [c for c in KANA.findall(k) if not (sp == "ズアウィア" and c in s["jp"])]
            if kn:
                hard.append(f"{s['id']}: 일본어 가나가 남음 {sorted(set(kn))} (ッ는 늘임소리·부호로)")
            if re.search(r"[！？]", k) and not re.search(r"[Ａ-Ｚａ-ｚ]", k):  # full-width Latin chants keep theirs
                soft.append(f"{s['id']}: 전각 ！？ (교정본은 반각)")
            if s["kind"] != "ruby_reading":
                for name, rx in TL.items():
                    tl[name] += len(re.findall(rx, k))
        if sp != "(지문)" and PRON.search(uko):
            hard.append(f"{u['id']} [{sp}]: 대사에 그/그녀 — {PRON.search(uko).group(0)!r}")
        if sp.startswith("アル") and "妾" in ujp and ZIM.search(uko):
            hard.append(f"{u['id']} [{sp}]: 알의 妾를 「짐」으로 씀(「이 몸」)")
        for jp, need, ban in TERMS:
            if jp in ujp:
                if not re.search(need, uko):
                    hard.append(f"{u['id']}: 원문 {jp} — 확정 표기 「{need}」가 없음")
                if ban and re.search(ban, uko):
                    hard.append(f"{u['id']}: 원문 {jp} — 쓰지 않기로 한 표기 「{re.search(ban, uko).group(0)}」")
        for jp, trap in TRAPS:
            if jp in ujp and re.search(trap, uko):
                hard.append(f"{u['id']}: 한자음 함정 {jp}→「{re.search(trap, uko).group(0)}」")
        if not any(s["id"] in BROKEN for s in segs):
            for a, b in ("「」", "『』"):
                if ujp.count(a) != uko.count(a) or ujp.count(b) != uko.count(b):
                    hard.append(f"{u['id']}: 괄호 {a}{b} 수가 원문과 다름(원문 {ujp.count(a)}·{ujp.count(b)}, 번역 {uko.count(a)}·{uko.count(b)})")
        rep = REPEATS.get(u["id"])
        if rep:
            mine = [ko.get(s["id"]) for s in segs]
            if len(mine) == len(rep["ko_segments"]):
                if mine != rep["ko_segments"]:
                    hard.append(f"{u['id']}: 반복 문장 표와 다름 — 표: {''.join(x or '' for x in rep['ko_segments'])[:40]!r}")
            else:
                soft.append(f"{u['id']}: 반복 문장인데 칸 수가 달라 표와 대조 못 함")
        rb = raw(u["file"])
        for i, s in enumerate(segs):  # a break the raw gap (outside every cell) already holds, written into the cell again
            k = ko.get(s["id"])
            if not isinstance(k, str) or s["id"] in BROKEN:
                continue
            nxt = rb[s["source_byte_end"]:segs[i + 1]["source_byte_start"]] if i + 1 < len(segs) else b""
            if i > 1 and segs[i - 1]["kind"] == "ruby_reading":  # a ruby base: the break sits before its reading
                prv = rb[segs[i - 2]["source_byte_end"]:segs[i - 1]["source_byte_start"]]
            else:
                prv = rb[segs[i - 1]["source_byte_end"]:s["source_byte_start"]] if i > 0 else b""
            if re.search(r"\n[\n　 ]*$", k) and not s["jp"].endswith("\n") and has_break(nxt):
                hard.append(f"{s['id']}: 칸 끝 줄바꿈 — 원문은 칸 밖(틈)에서 이미 줄을 바꾼다(게임에서 빈 줄·들여쓰기 두 배)")
            if k.startswith("\n") and not s["jp"].startswith("\n") and has_break(prv):
                hard.append(f"{s['id']}: 칸 앞 줄바꿈 — 원문은 칸 밖(틈)에서 이미 줄을 바꾼다(게임에서 빈 줄·들여쓰기 두 배)")
            if s["kind"] == "ruby_reading" and re.fullmatch(r"[・･　 ]+", s["jp"]) and re.search(r"[가-힣A-Za-z0-9]", k):
                hard.append(f"{s['id']}: 강조점 루비(원문 읽기가 점뿐)를 글자로 씀 {k!r} — 점으로(밑글자가 두 번 보임)")
            elif s["kind"] == "ruby_reading" and i + 1 < len(segs) and k.strip() and k.strip() == (ko.get(segs[i + 1]["id"]) or "").strip():
                hard.append(f"{s['id']}: 루비 읽기가 밑글자와 같음 {k!r} — 생략(omit_redundant)하거나 다른 층을 살린다")
        for i, s in enumerate(segs):  # ruby particle pairing
            if s["kind"] != "ruby_reading" or i + 2 >= len(segs):
                continue
            base, after = segs[i + 1], segs[i + 2]
            kb, kr, ka = ko.get(base["id"]) or "", ko.get(s["id"]) or "", ko.get(after["id"]) or ""
            if not kr or after["kind"] == "ruby_reading" or not (re.search(r"[가-힣]", kb) and re.search(r"[가-힣]", kr)):
                continue
            pair, form = srp.match_particle(ka)
            if pair:
                fb, fr = srp.form_for(pair, kb), srp.form_for(pair, kr)
                if fb and fr and fb != fr:
                    soft.append(f"{after['id']}: 루비 조사 어긋남 — 본문 「{kb[-8:]}」/읽기 「{kr[-8:]}」 + 「{form}」")
    return {"pid": pid, "hard": hard, "soft": soft, "tl": dict(tl)}


def check_side():
    p = ROOT / "responses" / "da0202_missing6.json"
    src = json.loads((ROOT / "in_da0202_missing6.json").read_text(encoding="utf-8"))
    try:
        got = json.loads(p.read_text(encoding="utf-8-sig"))
    except Exception as e:  # noqa: BLE001
        return {"pid": "da0202_missing6", "hard": [f"JSON을 읽을 수 없음: {e}"], "soft": [], "tl": {}}
    hard = []
    lines = got.get("lines", [])
    if len(lines) != len(src["lines"]):
        hard.append(f"줄 수가 {len(lines)} (6이어야 함)")
    for a, b in zip(src["lines"], lines):
        k = b.get("ko") or ""
        if not k.strip():
            hard.append(f"{a['src_lines']}: ko가 비었음")
        if a["jp"].count("\n") != k.count("\n"):
            hard.append(f"{a['src_lines']}: 줄바꿈 수가 원문과 다름")
        if a["speaker"] != "(지문)" and PRON.search(k):
            hard.append(f"{a['src_lines']}: 대사에 그/그녀")
        bad = sorted({c for c in k if drawable(c)})
        if bad:
            hard.append(f"{a['src_lines']}: 게임이 못 그리는 글자 {bad}")
    return {"pid": "da0202_missing6", "hard": hard, "soft": [], "tl": {}}


def note(r):
    p = ROOT / "되돌림" / f"{r['pid']}.txt"
    if not r["hard"]:
        if p.exists():
            p.unlink()
        return
    lines = [f"{r['pid']} 되돌림 — 아래가 규칙에 걸렸다. 걸린 곳을 고쳐 responses\\{r['pid']}.json 을 다시 써라(나머지는 그대로).", ""]
    lines += [f"- {h}" for h in r["hard"][:60]]
    if len(r["hard"]) > 60:
        lines.append(f"- … 그 밖 {len(r['hard']) - 60}건")
    p.write_text("\n".join(lines) + "\n", encoding="utf-8")


def all_targets():
    rs = {p.stem for p in (PKG / "responses").glob("*.json")}
    return [p.stem for p in sorted((PKG / "packets").glob("*.json")) if p.stem not in rs]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    pids = args or [p for p in all_targets() if (ROOT / "responses" / f"{p}.json").exists()]
    res = [check(p) for p in pids]
    if not args and (ROOT / "responses" / "da0202_missing6.json").exists():
        res.append(check_side())
    tot = collections.Counter()
    for r in res:
        if "--notes" in sys.argv:
            note(r)
        tot["묶음"] += 1
        tot["걸림"] += len(r["hard"])
        tot["살핌"] += len(r["soft"])
        tot.update({f"tl:{k}": v for k, v in r["tl"].items()})
        print(f"{r['pid']}: 걸림 {len(r['hard'])}, 살펴볼 것 {len(r['soft'])}, 직역투 {r['tl']}")
        for h in r["hard"][:12]:
            print("   [걸림]", h)
        for s in r["soft"][:6]:
            print("   [살핌]", s)
    print("합계:", dict(tot))


if __name__ == "__main__":
    main()
