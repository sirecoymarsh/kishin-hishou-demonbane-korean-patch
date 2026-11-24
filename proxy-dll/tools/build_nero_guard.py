"""DUEL 균형 모드: 네임리스 원(네로) 등·치마의 늘 막기 풀기(11-02) — 클록워크 팬텀 갑옷 풀기(10-19 build_duel.phantom_armor)와 같은 방법.

사용자 11-02 「네임리스 원이 지상 로봇 공격하게 하는건 성공했는데 지상 유닛들이 네임리스 원이랑 클락 웍 팬텀 공격하게 하는거
실패했네. 예전에 어떻게 해결했지?」→ 같은 고침을 네로에게(「적용해 줘」).

네로 정의의 머리 판정 `collision collid_back|collid_skirt collid_root SEPARATER&GUARD(100)` 은 늘 100% 막기라 땅 기체의 공격
(대개 아래쪽 = 치마)이 다 막혔다. 그 두 줄만 SEPARATER&HIT 로. 판정 고침은 균형 모드에서만(사용자 10-20):
  - 보통 장면(058~897)에 네로가 있으면 균형 모드 사본(+1000)에서 네로 정의를 …B 사본으로 — 506 에 이미 사본이 있으면 그 사본 위에
    (팬텀 갑옷·사이크라노쉬 조준 등 기존 고침 유지), 없으면 새 사본 + 표 줄.
  - 균형 모드에서만 쓰는 장면(1000~1999 사본·네로 지상 7300~7355·아이온 9083/9109 등)에만 나오는 네로 정의는 그 자리에서 고침.
묶음 dmpack516_neroguard.dat(번호 > 506·510·512 → 그 사본들을 덮음, < 520 → 520 이 그 위에 각인 666 을 얹어 다시 빌드).

  python -I -X utf8 build_nero_guard.py [--dry]
  → P:\\AI\\khd-hero-ai\\neroguard\\out\\dmpack516_neroguard.dat, neroguard\\duel_balance_scenes.tsv(지금 시험 사본 표 + 새 줄)
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import dm_lib as D  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")
ROOT = Path(r"P:\AI\khd-hero-ai\neroguard")
OUT = ROOT / "out" / "dmpack516_neroguard.dat"
WORK = ROOT / "pack-work"
TSV_IN = GAME / "autobattle" / "duel_balance_scenes.tsv"
TSV_OUT = ROOT / "duel_balance_scenes.tsv"
AUTO_TSV = Path(r"P:\AI\Codex\projects\khd_replay_20261015\build\duel_auto.tsv")
SKIP = (OUT.name, "dmpack520_kokuin666.dat", "dmpack521_kokuin666_test.dat")
PACKS = tuple(p.name for p in sorted(GAME.glob("dmpack*.dat")) if p.name not in SKIP)
D._EFF = D.effective(PACKS, GAME)
OFFSET = 1000
# 대리 DLL nero_ground_scope.h(v10) 의 NERO_GROUND 경로: 이 원래 장면들은 균형 모드에서 「불러온 장면 == 원래(또는 506 사본)」일 때만
# 네로 지상 장면 7300~7355(NG_LIVE 정의 — 그 자리에서 고침)로 간다. 여기에 새 +1000 사본을 걸면 불러온 번호가 바뀌어 지상 네로 대신
# 맨 네로가 나온다(11-02 A/B 첫 판: 262 → 1262·70 → 1070 로 가 지상 경로가 빠짐) — 이 장면들은 건드리지 않는다.
NERO_GROUND_SOURCES = {70, 86, 118, 134, 150, 166, 182, 198, 202, 204, 206, 208, 209, 210, 211, 212, 213, 215, 216, 217,
                       262, 278, 294, 326, 358, 423, 425, 427, 490, 506, 538, 554, 570, 586, 602, 618, 622, 624, 626, 628,
                       629, 630, 631, 632, 633, 635, 636, 637, 682, 698, 714, 746, 778, 843, 845, 847}
NG_SCOPE = Path(r"P:\AI\khd-hero-ai\v10\src\nero_ground_scope.h")
# mm_qm_scope.h: 로드 비야키↔네로 경로(5923~5926)도 「source==loaded」일 때만 — 마찬가지로 건드리지 않는다
MM_QM_SOURCES = {522, 627, 102, 207}
MMQM_SCOPE = Path(r"P:\AI\khd-hero-ai\v10\src\mm_qm_scope.h")
ROUTED = NERO_GROUND_SOURCES | MM_QM_SOURCES
GUARD = re.compile(r"(?m)^([ \t]*collision[ \t]+collid_(?:back|skirt)\d*[ \t]+collid_root[ \t]+SEPARATER&)GUARD\(100\)")
SUFFIX = "B"


def balance_only(s: int) -> bool:
    """균형 모드에서만 불리는 장면: 506 사본(+1000)·네로 지상(510)·로드 비야키-네로(508)·아이온 네이티브(512)·주인공 AI(513·514)"""
    return 1000 <= s < 2000 or 7300 <= s <= 7355 or 5923 <= s <= 5926 or 9000 <= s <= 9999


def first_robot_blocks(cmn: str) -> list[tuple[str, int, int]]:
    """depth-0 material(robot, SLOT) 블록마다 첫 것만(엔진이 쓰는 것) → (SLOT, 시작, 끝) — 원래 줄 끝 그대로"""
    out, seen, depth, cur = [], set(), 0, None
    pos = 0
    for line in cmn.splitlines(keepends=True):
        s = line.split("//", 1)[0]
        m = re.match(r"\s*material\s*\(\s*robot\s*,\s*(\w+)", s)
        if depth == 0 and m:
            cur = (m[1], pos)
        depth += s.count("{") - s.count("}")
        pos += len(line)
        if cur is not None and depth == 0 and "}" in s:
            if cur[0] not in seen:
                seen.add(cur[0])
                out.append((cur[0], cur[1], pos))
            cur = None
    return out


KEY_LINE = re.compile(r"(?mi)^([ \t]*(def_file|file)[ \t]+)(\S+)([^\r\n]*)(\r?\n)")


def block_def(block: str) -> re.Match | None:
    """그 블록이 실제로 쓰는 정의 줄: def_file 이 있으면 그것, 없으면 file(모든 파일의 기본 이름 — 그 줄은 그대로 두어야 한다;
    11-02 첫 빌드가 file 줄을 def_file+dmo_file 로 쪼개 waz·kcf 등의 기본 이름을 지울 뻔했다)"""
    lines = [m for m in KEY_LINE.finditer(block) if not m[1].lstrip().startswith("//")]
    d = [m for m in lines if m[2].lower() == "def_file"]
    f = [m for m in lines if m[2].lower() == "file"]
    return d[0] if d else (f[0] if f else None)


def qm_defs(cmn: str) -> list[str]:
    names = []
    for _slot, a, b in first_robot_blocks(cmn):
        m = block_def(cmn[a:b])
        if m and m[3].lower().startswith("data/character/qm/"):
            names.append(m[3].lower())
    return names


def main():
    dry = "--dry" in sys.argv
    if NG_SCOPE.exists():                         # 위 목록이 대리 DLL 표와 같은지(다른 판이 표를 바꿨으면 멈춤)
        ng = {int(m[1]) for m in re.finditer(r"case (\d+):return loaded==", NG_SCOPE.read_text(encoding="utf-8"))}
        assert ng == NERO_GROUND_SOURCES, sorted(ng ^ NERO_GROUND_SOURCES)
    if MMQM_SCOPE.exists():
        mq = {int(m[1]) for m in re.finditer(r"case (\d+): return 59", MMQM_SCOPE.read_text(encoding="utf-8"))}
        assert mq == MM_QM_SOURCES, sorted(mq ^ MM_QM_SOURCES)
    scenes = {}
    for n in D._EFF:
        m = re.match(r"data/scene/scene(\d+)/scene\1\.cmn$", n)
        if m:
            s = int(m[1])
            if 58 <= s < 2000 or balance_only(s):
                scenes[s] = D.read(n).decode("cp932")
    uses: dict[str, set[int]] = {}
    for s, cmn in scenes.items():
        for d in qm_defs(cmn):
            uses.setdefault(d, set()).add(s)
    in_place = sorted(d for d, ss in uses.items() if all(balance_only(s) for s in ss))
    need_copy = sorted(d for d, ss in uses.items() if not all(balance_only(s) for s in ss))
    files: dict[str, bytes] = {}
    report = dict(in_place={}, copies={}, scenes={}, rows_added=[], rows_updated=[])

    def fixed(name: str) -> tuple[str, int]:
        t = D.read(name + ".def").decode("cp932")
        return GUARD.subn(r"\1HIT", t)

    for d in in_place:                            # 균형 모드 전용 장면에만 나오는 네로 정의 — 그 자리에서
        t, n = fixed(d)
        if n:
            files[d + ".def"] = t.encode("cp932")
        report["in_place"][d] = n
    for d in need_copy:                           # 보통 장면에도 나오는 정의 — …B 사본
        t, n = fixed(d)
        assert n == 2, (d, n)
        b = d + SUFFIX.lower()
        assert b + ".def" not in D._EFF, b
        files[b + ".def"] = t.encode("cp932")
        report["copies"][d] = b
    auto = {}
    for ln in AUTO_TSV.read_text(encoding="utf-8").splitlines():
        if ln.startswith("#") or not ln.strip():
            continue
        manual, au, pilot, _ai, fight = ln.split("\t")
        auto[int(manual)] = (pilot, fight)
        auto[int(au)] = (pilot, fight)
    table = TSV_IN.read_text(encoding="utf-8").splitlines()
    have = {int(l.split("\t")[0]): i for i, l in enumerate(table) if l and not l.startswith("#")}
    used_b: set[str] = set()
    for s in sorted(x for x in scenes if 58 <= x < 1000 and x not in ROUTED):
        cmn = scenes[s]
        ds = [d for d in qm_defs(cmn) if d in report["copies"]]
        if not ds:
            continue
        c = s + OFFSET
        base = scenes.get(c)
        src = f"data/scene/scene{(c if base else s):03d}/scene{(c if base else s):03d}"
        text = base if base else cmn
        pieces, last = [], 0
        for _slot, a, b in first_robot_blocks(text):
            block = text[a:b]
            m = block_def(block)
            if m and m[3].lower() in report["copies"]:
                used_b.add(m[3].lower())
                if m[2].lower() == "def_file":        # 정의 줄의 이름만 …B 로
                    block = block[:m.start(3)] + m[3] + SUFFIX + block[m.end(3):]
                else:                                 # file(기본 이름)만 있으면 그 줄은 두고 아래에 def_file …B 를 더한다
                    indent = re.match(r"[ \t]*", m[1])[0]
                    block = block[:m.end()] + f"{indent}def_file\t{m[3]}{SUFFIX}{m[5]}" + block[m.end():]
            pieces.append(text[last:a] + block)
            last = b
        pieces.append(text[last:])
        new = "".join(pieces)
        assert new != text, s
        cname = f"data/scene/scene{c:03d}/scene{c:03d}"
        files[cname + ".cmn"] = new.encode("cp932")
        if not base:                              # 새 사본: scn·se 도
            scn = D.read(src + ".scn").decode("cp932")
            files[cname + ".scn"] = (f"// DUEL 11-02: balance-mode copy of scene{s:03d} (nero_guard)\r\n" + scn).encode("cp932")
            files[cname + ".se"] = D.read(src + ".se")
            pilot, fight = auto.get(s, ("?", "?"))
            table.append(f"{s}\t{c}\t{pilot}\t{fight}\tnero_guard")
            report["rows_added"].append(s)
        else:
            i = have[s]
            parts = table[i].split("\t")
            if "nero_guard" not in parts[-1]:
                parts[-1] += "+nero_guard"
                table[i] = "\t".join(parts)
            report["rows_updated"].append(s)
        report["scenes"][s] = dict(copy=c, on_506=bool(base), defs=ds)
    for d in [d for d in report["copies"] if d not in used_b]:   # 사본 장면이 실제로 안 쓰는 …B 는 넣지 않는다
        del files[report["copies"][d] + ".def"]
        del report["copies"][d]
    for name in files:                            # 원래(이야기) 묶음 이름은 건드리지 않는다
        assert Path(D._EFF[name][1]).name not in ("dmpack.dat", "dmpack001.dat", "dmpack002.dat", "dmpack003.dat") \
            if name in D._EFF else True, name
    print(f"네로 정의: 그 자리 {len(in_place)}(고친 {sum(1 for v in report['in_place'].values() if v)}), …B 사본 {len(need_copy)}; "
          f"장면 사본 새로 {len(report['rows_added'])}·506 사본 위 {len(report['rows_updated'])}; 파일 {len(files)}")
    ROOT.mkdir(parents=True, exist_ok=True)
    (ROOT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    if dry:
        return
    TSV_OUT.write_text("\n".join(table) + "\n", encoding="utf-8")
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    specs = []
    for name, data in files.items():
        p = WORK / name.replace("/", "__")
        p.write_bytes(data)
        specs.append(f"{name.replace('/', chr(92))}={p}")
    listing = WORK / "_specs.txt"
    listing.write_text("\n".join(specs) + "\n", encoding="utf-8")
    r = subprocess.run([sys.executable, "-I", str(D.TOOLS / "gupk_build.py"), str(GAME / "demonbane.dll"), str(OUT),
                        "@" + str(listing)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit(r.returncode)
    print(OUT, hashlib.sha256(OUT.read_bytes()).hexdigest())


main()
