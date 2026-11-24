"""모든 변경을 합친 사본(11-03 사용자 「모든 변경사항을 최신화한 사본을 만들어볼까?」)의 두 PAK.

script.pak  = 아르곤 재번역(F:\\AI\\khd-argon-game, 8c8979b2) + GPT 컨티뉴 대기(GPT tools\\build_continue_wait.py 와 같은 변환:
              「#コンティニュー」 표지 안 음성 태그 뒤에 그 음성 길이만큼 WAIT — 번역 글은 안 건드림). GPT 는 옛 번역(11f011da)에
              걸었다(14대본·57음성) — 같은 수가 나와야 한다.
0replay.pak = v12 사본 것(GPT DUEL 명령표 미리보기·초상 가운데 + 프리 파이트 표지) + 아르곤의 스토리 차트(dh_ex_story.nps),
              프리 파이트 표지는 아르곤 대본의 freebattle.nps 앞에 다시 붙임(옛 대본에 붙인 것이 아르곤 것을 덮지 않게).
둘 다 나머지 항목은 바이트 그대로(대조).

  python -I -X utf8 build_merged_paks.py   → out\\script.pak, out\\0replay.pak, out\\continue-manifest.json
"""
from __future__ import annotations

import hashlib
import json
import math
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import kishinhisho_pak_fixed as P  # noqa: E402
import pak_writer  # noqa: E402
from make_story_subs import ogg_seconds  # noqa: E402

ARGON = Path(r"F:\AI\khd-argon-game")
V12 = Path(r"F:\AI\khd-v12-game")
ROOT = Path(r"P:\AI\khd-hero-ai\merge")
OUT = ROOT / "out"
FREE = "script\\freebattle.nps"
STORY = "script\\dh_ex_story.nps"
MARK = b'<SYSTEM MENU="#200007E2">'


def entries(path: Path) -> dict[str, bytes]:
    a = P.parse_archive(path)
    return {e.name: P.decode_stored(e, P.read_stored(a, e)) for e in a.entries}


def build_script() -> dict:
    base = ARGON / "script.pak"
    a = P.parse_archive(base)
    voices = []
    for n in ("0voice.pak", "voice.pak"):
        p = P.parse_archive(ARGON / n)
        voices.append((p, {e.name.lower(): e for e in p.entries}))
    stage = ROOT / "stage" / "continue"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    rows, replacements = [], {}
    for e in a.entries:                                    # GPT build_continue_wait.py 와 같은 변환
        if not e.name.lower().endswith(".nps"):
            continue
        t = P.decode_stored(e, P.read_stored(a, e)).decode("cp932")

        def branch(m):
            def voice(v):
                key = "voice\\" + v[2].lstrip("\\").lower() + ".ogg"
                hit = next(((p, d[key]) for p, d in voices if key in d), None)
                seconds = 0.0
                if hit:
                    p, ve = hit
                    seconds = ogg_seconds(P.decode_stored(ve, P.read_stored(p, ve)))
                    assert 0 < seconds < 120, (key, seconds)
                ticks = math.ceil(seconds * 10) + 1
                rows.append({"script": e.name, "marker": m[1], "voice": key, "seconds": seconds, "wait_100ms": ticks,
                             "old_tail": v[3], "audio_found": bool(hit)})
                return v[1] + '\r<WAIT TIME="' + str(ticks) + '" KEYSKIP="FALSE" SYNC="TRUE">\r'
            body, count = re.subn(r'(<VOICE\b[^>]*SRC="([^"]+)"[^>]*>)(\s*(?:<k>\s*)?(?:<WAIT\b[^>]*>\s*)?)', voice, m[2], flags=re.I)
            assert count <= 1, (e.name, m[1], count)
            return m[1] + body
        changed = re.sub(r'(<MARKER NAME="#コンティニュー[^"]+">)(.*?)(?=<MARKER|\Z)', branch, t, flags=re.S)
        if changed != t:
            data = changed.encode("cp932")
            dest = stage.joinpath(*e.name.split("\\"))
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            replacements[e.name] = data
    out = OUT / "script.pak"
    P.build_archive(base, stage, out, level=6)
    original = {e.name: P.decode_stored(e, P.read_stored(a, e)) for e in a.entries}
    got = entries(out)
    assert set(got) == set(original)
    for n, b in got.items():
        assert b == replacements.get(n, original[n]), n
    gpt = json.loads(Path(r"P:\AI\Codex\projects\khd-hard-v11-20261102\out\continue-manifest.json").read_text(encoding="utf-8"))
    same_rows = [(r["script"], r["voice"], r["wait_100ms"]) for r in rows] == \
                [(r["script"], r["voice"], r["wait_100ms"]) for r in gpt["voices"]]
    manifest = {"base": "argon script.pak " + hashlib.sha256(base.read_bytes()).hexdigest()[:16],
                "changed_scripts": sorted(replacements), "voices": rows, "same_as_gpt_on_old_translation": same_rows,
                "sha256": hashlib.sha256(out.read_bytes()).hexdigest()}
    (OUT / "continue-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"script.pak: {len(replacements)} scripts, {len(rows)} continue voices (GPT on the old: {len(gpt['changed_scripts'])}, "
          f"{len(gpt['voices'])}; same scripts/voices/waits: {same_rows}) -> {manifest['sha256'][:16]}")
    return manifest


def build_replay() -> str:
    mine = entries(V12 / "0replay.pak")
    argon = entries(ARGON / "0replay.pak")
    src = entries(ARGON / "script.pak")[FREE]
    assert b"SYSTEM MENU" not in src
    nl = b"\r\n" if b"\r\n" in src else b"\n"
    merged = dict(mine)
    merged[STORY] = argon[STORY]
    merged[FREE] = MARK + nl + src
    out = OUT / "0replay.pak"
    pak_writer.write_pak(out, list(merged.items()))
    got = entries(out)
    assert got == merged
    h = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f"0replay.pak: {len(merged)} entries = v12's + Argon {STORY} + marker on Argon's freebattle.nps ({len(src)} bytes) -> {h[:16]}")
    return h


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    OUT.mkdir(parents=True, exist_ok=True)
    build_script()
    build_replay()
