"""Check Gemini polish outputs (out/<packet>.json) against the inputs; write 되돌림 notes for hard failures.

  python -I -X utf8 check_out.py                 # check every out file, print summary
  python -I -X utf8 check_out.py P0002_da0101    # one packet, with every edit listed
  python -I -X utf8 check_out.py --notes         # also write/remove 되돌림\<packet>.txt
"""
import collections
import difflib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-argon-review\tools")
from check_proposals import drawable  # noqa: E402  (loads the game font)

ROOT = Path(r"P:\AI\khd-argon-review\gemini_polish")
PROTECT = ["하도", "고조", "블랙 로지", "오거", "류가", "붉은 소녀", "아즈라드", "고독", "외도", "독기", "마검", "다리 실드",
           "더니치", "바깥의 신", "공주님", "쿠자쿠", "리베르 레기스", "언월도", "데몬베인", "아이온", "귀계신", "마도서",
           "사령비법", "네크로노미콘", "아컴", "미스캐토닉", "즈아위아", "아자토스", "나이알라토텝", "클록워크 팬텀", "틱톡맨",
           "마기우스", "레무리아", "무한나선", "이 몸", "그대", "대디", "레이디", "달링", "예스", "마스터", "로보", "말입니다",
           "쿠로 쨩", "옛 인", "엘더"]
TRAPS = ["인영", "장기", "방향", "비색", "자전", "성기", "대책", "각부", "불적", "분류", "마인", "기영", "전령", "당돌", "정밀",
         "주저", "망살"]
INTENS = ["정말", "진짜", "너무", "완전히", "굉장히", "매우", "엄청", "마구", "무척", "몹시", "대단히", "아주", "한껏", "잔뜩",
          "끔찍이", "미친 듯이", "격렬하게", "거칠게", "완벽하게", "철저히", "그야말로", "도무지"]
TIC_SPEAKERS = {"ドクター・ウェスト", "エルザ", "ストーン", "チアキ", "アル", "ハヅキ", "シュリュズベリイ", "アナザーブラッド"}
PRON = re.compile(r"그녀|(?<![가-힣])그(?:는|가|를|의|에게|와|도)(?![가-힣])")
HANJA = re.compile(r"[\u4e00-\u9fff]")


def load_in(pid):
    doc = json.loads((ROOT / "in" / f"{pid}.json").read_text(encoding="utf-8"))
    cells = {}
    for u in doc["units"]:
        for c in u["cells"]:
            cells[c["sid"]] = dict(c, speaker=u["speaker"])
    return doc, cells


def check(pid, verbose=False):
    doc, cells = load_in(pid)
    path = ROOT / "out" / f"{pid}.json"
    hard, soft = [], []
    try:
        out = json.loads(path.read_text(encoding="utf-8-sig"))
    except Exception as e:  # noqa: BLE001
        return {"pid": pid, "hard": [f"JSON을 읽을 수 없음: {e}"], "soft": [], "edits": 0, "cells": 0, "keep": 1.0}
    if out.get("packet") != pid:
        hard.append(f"packet 칸이 {out.get('packet')!r} (파일 이름은 {pid})")
    edits = out.get("edits")
    if not isinstance(edits, list):
        return {"pid": pid, "hard": hard + ["edits 목록이 없음"], "soft": [], "edits": 0, "cells": 0, "keep": 1.0}
    extra = set(out) - {"packet", "edits"}
    if extra:
        soft.append(f"쓰지 말라고 한 필드: {sorted(extra)}")
    cur = {sid: c["ko"] for sid, c in cells.items()}
    touched = set()
    for i, e in enumerate(edits):
        sid, old, new = e.get("sid"), e.get("old"), e.get("new")
        where = f"#{i + 1} {sid}"
        c = cells.get(sid)
        if c is None:
            hard.append(f"{where}: 이 묶음에 없는 칸 ID")
            continue
        if c.get("lock"):
            hard.append(f"{where}: 잠긴 칸({c['lock']})")
            continue
        if not isinstance(old, str) or not isinstance(new, str) or not old:
            hard.append(f"{where}: old/new가 비었거나 글자가 아님")
            continue
        ko = cur[sid]
        n = ko.count(old)
        if n != 1:
            hard.append(f"{where}: old {old!r}가 현재 칸에 {n}번 나옴(정확히 1번이어야 함). 현재 칸: {ko!r}")
            continue
        if old == new:
            hard.append(f"{where}: old와 new가 같음")
            continue
        if old.strip() == ko.strip() and len(ko) > 25:
            soft.append(f"{where}: 칸 전체를 old로 씀")
        edge = c.get("ruby_edge", "")
        res0 = ko.replace(old, new)
        hw = lambda s: re.findall(r"[가-힣]+", s)  # noqa: E731
        if ("앞" in edge and hw(ko)[:1] != hw(res0)[:1]) or ("뒤" in edge and hw(ko)[-1:] != hw(res0)[-1:]):
            hard.append(f"{where}: ruby_edge 쪽 끝 낱말을 바꿈(루비 읽기와 조사 짝이 깨짐)")
            continue
        for ch in ("\n", "\u3000", "「", "」", "『", "』", "〈", "〉"):
            if old.count(ch) != new.count(ch):
                hard.append(f"{where}: {repr(ch)} 개수가 바뀜")
                break
        else:
            res = ko.replace(old, new)
            bad = sorted({ch for ch in new if drawable(ch)})
            if bad:
                hard.append(f"{where}: 게임이 못 그리는 글자 {bad}")
                continue
            if len(HANJA.findall(new)) > len(HANJA.findall(old)):
                hard.append(f"{where}: 한자를 넣음")
                continue
            if c["speaker"] != "(지문)" and len(PRON.findall(new)) > len(PRON.findall(old)):
                hard.append(f"{where}: 대사에 그/그녀를 넣음")
                continue
            lost = [t for t in PROTECT if old.count(t) > new.count(t)]
            if lost:
                hard.append(f"{where}: 확정 용어·말버릇을 지움 {lost}")
                continue
            trap = [t for t in TRAPS if new.count(t) > old.count(t)]
            if trap:
                hard.append(f"{where}: 한자음 함정을 새로 씀 {trap}")
                continue
            if c["speaker"] == "ドクター・ウェスト" and "것이" in old and "것이" not in new:
                hard.append(f"{where}: 웨스트 말버릇 「것이다」를 지움")
                continue
            more = [t for t in INTENS if new.count(t) > old.count(t)]
            if more:
                soft.append(f"{where}: 강조어 추가 {more} — {old!r} → {new!r}")
            for ch in "!?…―":
                if old.count(ch) != new.count(ch):
                    soft.append(f"{where}: {ch} 개수가 바뀜")
                    break
            if len(new) > len(old) * 1.6 + 6:
                soft.append(f"{where}: 크게 늘어남 {len(old)}→{len(new)}자")
            if c["speaker"] in TIC_SPEAKERS:
                soft.append(f"{where}: 말버릇 인물({c['speaker']}) 대사를 고침 — {old!r} → {new!r}")
            cur[sid] = res
            touched.add(sid)
            if verbose:
                print(f"  {sid} [{c['speaker']}] {old!r} → {new!r}  ({e.get('why', '')})")
    before = "".join(c["ko"] for c in cells.values() if isinstance(c["ko"], str))
    after = "".join(v for v in cur.values() if isinstance(v, str))
    keep = difflib.SequenceMatcher(None, before, after, autojunk=False).ratio() if before else 1.0
    return {"pid": pid, "hard": hard, "soft": soft, "edits": len(edits), "cells": len(touched), "keep": keep,
            "editable": sum(1 for c in cells.values() if not c.get("lock"))}


def note(r):
    p = ROOT / "되돌림" / f"{r['pid']}.txt"
    if not r["hard"]:
        if p.exists():
            p.unlink()
        return
    lines = [f"{r['pid']} 되돌림 — 아래 편집이 규칙에 걸렸다. 걸린 편집만 고치거나 빼고, 나머지는 그대로 두어 out\\{r['pid']}.json 을 다시 써라.", ""]
    lines += [f"- {h}" for h in r["hard"]]
    p.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    notes = "--notes" in sys.argv
    pids = args or sorted(p.stem for p in (ROOT / "out").glob("*.json"))
    tot = collections.Counter()
    for pid in pids:
        r = check(pid, verbose=bool(args))
        if notes:
            note(r)
        tot["packets"] += 1
        tot["edits"] += r["edits"]
        tot["cells"] += r["cells"]
        tot["hard"] += len(r["hard"])
        print(f"{pid}: 편집 {r['edits']}, 고친 칸 {r['cells']}/{r.get('editable', '?')}, 글자 보존 {r['keep']:.3f}, 걸림 {len(r['hard'])}, 살펴볼 것 {len(r['soft'])}")
        for h in r["hard"]:
            print("   [걸림]", h)
        for s in r["soft"]:
            print("   [살핌]", s)
    print("합계:", dict(tot))


if __name__ == "__main__":
    main()
