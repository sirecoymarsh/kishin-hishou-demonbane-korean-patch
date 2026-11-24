"""배포용 영상 덧씌우기(11-18): 합본 mpeg.pak 에서 일본판 1.00 mpeg.pak(83a06b06, P:\\AI\\khd-retl\\JP_PURE)과 다른 항목만
0mpeg.pak 하나로(같은 이름이면 이름순으로 앞 묶음이 이김 — 0mpeg.pak 이 mpeg.pak 을 덮는다; 엔진 1.20 업데이트의 0cg·0sound 와 같은 길).
  python -I -X utf8 mpeg_overlay.py   -> out\\0mpeg.pak + mpeg_overlay.json"""
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
import kishinhisho_pak_fixed as P  # noqa: E402
import pak_writer  # noqa: E402

HERE = Path(__file__).resolve().parent
NOW = Path(r"F:\AI\khd-latest-game\mpeg.pak")
ORIG = Path(r"P:\AI\khd-retl\JP_PURE\mpeg.pak")
OUT = HERE / "out" / "0mpeg.pak"


def entries(path):
    a = P.parse_archive(path)
    return a, {e.name.lower(): e for e in a.entries}


an, now = entries(NOW)
ao, orig = entries(ORIG)
print("now", len(now), "orig", len(orig))
print("only in orig:", sorted(set(orig) - set(now)))
pick = []
for k, e in now.items():
    plain = P.decode_stored(e, P.read_stored(an, e))
    if k in orig:
        o = orig[k]
        if plain == P.decode_stored(o, P.read_stored(ao, o)):
            continue
    pick.append((e.name, plain))
    print("overlay:", e.name, len(plain), "new" if k not in orig else "changed")
pak_writer.write_pak(OUT, pick)
ab, back = entries(OUT)
assert sorted(back) == sorted(n.lower() for n, _ in pick)
rows = []
for name, plain in pick:
    e = back[name.lower()]
    got = P.decode_stored(e, P.read_stored(ab, e))
    assert got == plain, name
    rows.append(dict(name=name, size=len(plain), sha256=hashlib.sha256(plain).hexdigest(), compressed=bool(e.compressed)))
h = hashlib.sha256(OUT.read_bytes()).hexdigest()
(HERE / "mpeg_overlay.json").write_text(json.dumps(dict(out=str(OUT), sha256=h, entries=rows), ensure_ascii=False, indent=1), encoding="utf-8")
print("ok", OUT, OUT.stat().st_size, h[:8])
