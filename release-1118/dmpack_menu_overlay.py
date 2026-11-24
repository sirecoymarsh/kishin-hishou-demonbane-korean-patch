"""배포용 메뉴 그림 덧씌우기(11-18): 합본의 dmpack.dat·dmpack001.dat 에서 원본(.orig_*)과 내용이 다른 항목만 뽑아
작은 GUPK 묶음 하나로. 사용자 원본 dmpack.dat·dmpack001.dat 은 그대로 두고 이 묶음이 덮는다(뒤 번호가 이김).
이름 고르기: 합본에서 그 항목을 가진 더 뒤 묶음(dmpack002·003·5xx·600)이 있으면 그쪽이 이기므로 덧씌우기에서도 빼야
같은 결과 — 확인해서 알린다.
  python -I -X utf8 dmpack_menu_overlay.py   -> out\\dmpack004_ko_menu.dat + menu_overlay.json"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

TOOLS = Path(r"P:\AI\khd-runtime-fix\tools")
src = (TOOLS / "gupk_sounds.py").read_text(encoding="utf-8").split("\nargs = sys.argv[1:]")[0]
gs = {}
_argv, sys.argv = sys.argv, [sys.argv[0], r"F:\AI\khd-latest-game\demonbane.dll"]
exec(compile(src, "gupk_sounds.py", "exec"), gs)
sys.argv = _argv
G = Path(r"F:\AI\khd-latest-game")
HERE = Path(__file__).resolve().parent
OUT = HERE / "out" / "dmpack004_ko_menu.dat"
WORK = HERE / "work_menu"
TABLE = gs["read_pe_va"](str(G / "demonbane.dll"), gs["LOOKUP_TABLE_VA"], 6 * 256)


def load(path):
    """name -> (name, key, data); data = the unpacked bytes, or ('raw', stored bytes) where unpacking fails
    (size-0 reserved entries of the original)"""
    d = {}
    with open(path, "rb") as f:
        for idx, name, key, unpacked, off, size in gs["entries"](str(path), TABLE):
            try:
                data = gs["extract"](str(path), TABLE, key, unpacked, off, size)
            except Exception:
                f.seek(off)
                data = ("raw", unpacked, f.read(size))
            d[name.lower()] = (name, key, data)
    return d


changed = {}
for pack, orig in (("dmpack.dat", "dmpack.dat.orig_FDD3D1A8"), ("dmpack001.dat", "dmpack001.dat.orig_0B479A4D")):
    now, was = load(G / pack), load(G / orig)
    added = sorted(set(now) - set(was))
    removed = sorted(set(was) - set(now))
    diff = sorted(k for k in now if k in was and now[k][2] != was[k][2])
    print(f"{pack}: {len(now)} entries, changed {len(diff)}, added {len(added)}, removed {len(removed)}")
    for k in diff + added:
        changed[k] = (pack,) + now[k]
    if removed:
        print("  removed (cannot be expressed by an overlay):", removed)
# what the game ends up reading: dmpack001 beats dmpack.dat; anything later beats both
later = sorted(p.name for p in G.glob("dmpack*.dat") if p.name.lower() not in ("dmpack.dat", "dmpack001.dat"))
shadow = {}
for p in later:
    names = {n.lower() for _i, n, *_r in gs["entries"](str(G / p), TABLE)}
    for k in changed:
        if k in names:
            shadow.setdefault(k, []).append(p)
eff = {}
for k, (pack, name, key, data) in changed.items():
    if pack == "dmpack.dat" and k in load(G / "dmpack001.dat"):
        print("  (dmpack.dat change is under dmpack001's own entry):", name)
        continue
    eff[k] = (pack, name, key, data)
print("overlay entries:", len(eff), "| shadowed by later packs:", shadow)
WORK.mkdir(exist_ok=True)
OUT.parent.mkdir(exist_ok=True)
specs = []
rec = []
for k, (pack, name, key, data) in sorted(eff.items()):
    p = WORK / name.replace("\\", "__").replace("/", "__")
    p.write_bytes(data)
    # 11-18 밤 고침: 엔진은 항목 이름을 역슬래시로 갖고 그대로 _stricmp 로 찾는다(data\menu\x.png). gupk_sounds.entries 가 읽을 때
    # \ 를 / 로 바꿔 보여 주므로 그 이름을 그대로 쓰면 묶음 안 이름이 data/menu/x.png 가 되어 엔진이 못 찾고 dmpack001 의
    # 일본어 그림을 썼다(packtest\packtest.exe 로 엔진의 묶음 읽기를 돌려 확인). 쓸 때는 원래대로 역슬래시로.
    specs.append(f"{name.replace('/', chr(92))}={p}:{key}")
    rec.append(dict(name=name, from_pack=pack, key=key, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
r = subprocess.run([sys.executable, "-I", str(TOOLS / "gupk_build.py"), str(G / "demonbane.dll"), str(OUT)] + specs,
                   capture_output=True, text=True)
print(r.stdout.strip() or r.stderr.strip())
if r.returncode:
    raise SystemExit(r.returncode)
back = load(OUT)
assert sorted(back) == sorted(eff), "overlay names differ"
for k, (pack, name, key, data) in eff.items():
    assert back[k][2] == data and back[k][1] == key, k
(HERE / "menu_overlay.json").write_text(json.dumps(dict(out=str(OUT), sha256=hashlib.sha256(OUT.read_bytes()).hexdigest(),
                                                       entries=rec, shadowed=shadow), ensure_ascii=False, indent=1), encoding="utf-8")
print("ok", OUT, OUT.stat().st_size, "bytes")
for x in rec:
    print("  ", x["from_pack"], x["name"], x["size"])
