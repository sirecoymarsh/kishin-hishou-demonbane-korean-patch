"""11-19 사용자 「패치는 실행파일보다는 압축해서 바로 대체하는 형식으로 만드는게 좋겠어」「윈도우에서 서명이 없다고 …」 →
설치기(Patch.ps1·*.cmd) 없이 게임 폴더에 풀어 덮어쓰는 zip 셋. zip 안 경로 = 게임 폴더 기준(맨 위 폴더 없음).
  본 패치  dist_zip\\KishinHisho_KO_20261118.zip
  소설     dist_zip\\KishinHisho_KO_20261118_Novels.zip   (ko-library\\books\\novel-*.epub + covers\\novel-*.png 만)
  고화질   dist_zip\\KishinHisho_KO_20261118_HD.zip       (hires\\ + dmpack600_retex.dat)
설치기가 하던 일 가운데 덮어쓰기로 못 하는 것은 이렇게:
  - 소설 목록 — 본 패치의 ko-library\\books.ini 에 8권(소설 6 + 사전 2)을 다 적고, 서재(library.js b7875669)는 파일이 있는 책만
    보인다(사용자 11-19 「로어에 사용자가 직접 Epub을 선택해 추가하면」 → 표지·제목이 붙고 순서가 상관없는 이쪽으로).
  - 설정 값 지키기·dgVoodoo 치우기·원본 백업 — 안내문(다시 풀면 ini 기본값, d3d8.dll·dgVoodoo.conf 지우기, 덮어쓰는 원래 파일 다섯).
목록·기본 설정·책 목록 함수는 build_release.py 앞부분을 그대로 쓴다(합본 F:\\AI\\khd-latest-game 은 읽기만).
  python -I -X utf8 build_zip_release.py [--only=core,novels,hd]
"""
import hashlib
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
_src = (HERE / "build_release.py").read_text(encoding="utf-8").split("\nclass Pack")[0]
_g = {"__file__": str(HERE / "build_release.py"), "__name__": "build_release_head"}
exec(compile(_src, "build_release.py(head)", "exec"), _g)
G, R1030, HIRES, REL, inv, sha = _g["G"], _g["R1030"], _g["HIRES"], _g["REL"], _g["inv"], _g["sha"]
NOVELS, NOT_CORE, default_ini, books_ini = _g["NOVELS"], _g["NOT_CORE"], _g["default_ini"], _g["books_ini"]
TPL = HERE / "template"
TPLZ = HERE / "template_zip"
OUT = HERE / "dist_zip"
DOCS = "한국어패치 안내"
OVERWRITES_120 = {"system.dll", "demonbane.dll", "script.pak", "0cg.pak", "0advdata.pak"}   # 안내문과 맞춰 둠(아래에서 다시 셈)
ONLY = next((a.split("=", 1)[1].split(",") for a in sys.argv if a.startswith("--only=")), ["core", "novels", "hd"])


class Zip:
    def __init__(self, name, store):
        self.name, self.store, self.items = name, store, {}

    def add(self, arc, src=None, data=None):
        arc = arc.replace("\\", "/")
        assert arc not in self.items, arc
        self.items[arc] = data if data is not None else Path(src)
        if data is None and not Path(src).is_file():
            raise SystemExit(f"missing source for {arc}: {src}")

    def write(self):
        OUT.mkdir(exist_ok=True)
        sums = []
        for arc, src in sorted(self.items.items()):
            if not arc.startswith(DOCS + "/SHA256SUMS"):
                sums.append(f"{hashlib.sha256(src).hexdigest() if isinstance(src, bytes) else sha(src)}  {arc}")
        sums_name = f"{DOCS}/SHA256SUMS{'' if self.name == REL else self.name[len(REL):]}.txt"
        self.items[sums_name] = ("\n".join(sums) + "\n").encode("utf-8")
        z = OUT / (self.name + ".zip")
        tmp = z.with_suffix(".zip.writing")
        now = time.localtime()[:6]
        with zipfile.ZipFile(tmp, "w", allowZip64=True) as zf:
            for arc, src in sorted(self.items.items()):
                method = zipfile.ZIP_STORED if Path(arc).suffix.lower() in self.store else zipfile.ZIP_DEFLATED
                if isinstance(src, bytes):
                    zi = zipfile.ZipInfo(arc, date_time=now)
                    zi.compress_type = method
                    zi.external_attr = 0o644 << 16
                    zf.writestr(zi, src, compress_type=method, compresslevel=6)
                else:
                    zf.write(src, arc, compress_type=method, compresslevel=6)
        tmp.replace(z)
        bad = 0
        with zipfile.ZipFile(z) as zf:                       # 다시 열어 SHA256SUMS 와 대조
            names = set(zf.namelist())
            for ln in sums:
                h, arc = ln.split("  ", 1)
                d = hashlib.sha256()
                with zf.open(arc) as f:
                    for b in iter(lambda: f.read(1 << 22), b""):
                        d.update(b)
                bad += d.hexdigest() != h
            assert names == set(self.items), "zip names differ"
        zh = sha(z)
        (OUT / (z.name + ".sha256")).write_text(f"{zh}  {z.name}\n", encoding="utf-8")
        total = sum(len(s) if isinstance(s, bytes) else s.stat().st_size for s in self.items.values())
        print(f"{z.name}: {len(self.items)} entries, {total / 2**20:.1f} MB raw, {z.stat().st_size / 2**20:.1f} MB zip, "
              f"checked {len(sums)}, mismatches {bad}, sha256 {zh[:8]}")
        if bad:
            raise SystemExit("zip read-back mismatch")


def cam_default():
    """khdcam.ini 의 [state](카메라가 쓰는 「마지막 시점」, remember=1 이면 다음 실행에도 씀)는 합본에서 마지막으로 본 시점이 들어 있다 →
    배포판은 원래 카메라(preset1 = 0 0 1)로"""
    text = (G / "khdcam.ini").read_bytes().decode("utf-16")
    out, sec = [], None
    for ln in text.split("\r\n"):
        s = ln.strip()
        if s.startswith("[") and s.endswith("]"):
            sec = s[1:-1].lower()
        elif sec == "state" and "=" in s and not s.startswith(";"):
            k = s.split("=", 1)[0].strip().lower()
            ln = {"yaw": "yaw=0.0", "pitch": "pitch=0.0", "zoom": "zoom=1.000"}.get(k, ln)
        out.append(ln)
    new = "\r\n".join(out)
    assert "yaw=0.0\r\npitch=0.0\r\nzoom=1.000" in new, "khdcam.ini [state] not found"
    return b"\xff\xfe" + new.encode("utf-16-le")


def docs(z: Zip, readme: str):
    z.add(Path(readme).name, src=TPLZ / readme)


if "core" in ONLY:
    core = Zip(REL, {".pak", ".mpg", ".epub", ".png", ".jpg"})
    ids = None
    for r in inv["rows"]:
        rel = r["rel"]
        if r["status"] != "ship" or NOT_CORE.search(rel) or NOVELS.search(rel):
            continue
        if rel == "autobattle/autobattle.ini":
            core.add(rel, data=default_ini())
        elif rel == "ko-library/books.ini":
            _two, all_books = books_ini([])
            ids = [b["Id"] for b in all_books]
            core.add(rel, data=books_ini(ids)[0])                 # 8권 다 — 서재는 파일이 있는 책만 보임
        elif rel == "khdcam.ini":
            core.add(rel, data=cam_default())
        else:
            core.add(rel, src=G / rel)
    assert ids and len(ids) == 8, ids
    core.add("0mpeg.pak", src=HERE / "out" / "0mpeg.pak")
    core.add("dmpack004_ko_menu.dat", src=HERE / "out" / "dmpack004_ko_menu.dat")
    for n in ("LoaderDll.dll", "LocaleEmulator.dll"):
        core.add(n, src=G / n)
    core.add("Play.cmd", src=R1030 / "files" / "Play.cmd")
    docs(core, "한국어패치_읽어주세요.txt")
    core.add(f"{DOCS}/변경사항.md", src=TPL / "변경사항.md")
    core.add(f"{DOCS}/THIRD_PARTY.md", src=TPL / "THIRD_PARTY.md")
    for p in sorted((TPL / "licenses").iterdir()):
        core.add(f"{DOCS}/licenses/{p.name}", src=p)
    # 덮어쓰는 원래 1.20 파일(안내문에 적은 다섯과 같아야 함)
    base = Path(r"Y:\AI\claude-work\khd-release-1118\testbase\기신비상 깨끗한 판")
    over = {p.relative_to(base).as_posix().lower() for p in base.rglob("*") if p.is_file()} & {a.lower() for a in core.items}
    assert over == OVERWRITES_120, over
    lib = core.items["ko-library/library.js"]
    assert "b=>b.available&&(" in lib.read_text(encoding="utf-8"), "library.js does not hide missing books"
    core.write()

if "novels" in ONLY:
    nov = Zip(REL + "_Novels", {".epub", ".png"})
    for r in inv["rows"]:
        if r["status"] == "ship" and NOVELS.search(r["rel"]):
            nov.add(r["rel"], src=G / r["rel"])
    assert sum(a.endswith(".epub") for a in nov.items) == 6 and sum(a.endswith(".png") for a in nov.items) == 6, sorted(nov.items)
    docs(nov, "한국어패치_소설_읽어주세요.txt")
    nov.write()

if "hd" in ONLY:
    hd = Zip(REL + "_HD", {".pak", ".dat", ".tsv"})
    paks = sorted({ln.split("\t")[1] for ln in (HIRES / "index.tsv").read_bytes().decode("cp932").splitlines() if ln.strip()})
    hd.add("hires/index.tsv", src=HIRES / "index.tsv")
    for p in paks:
        hd.add(f"hires/{p}", src=HIRES / p)
    hd.add("dmpack600_retex.dat", src=G / "dmpack600_retex.dat")
    docs(hd, "한국어패치_고화질_읽어주세요.txt")
    hd.write()
