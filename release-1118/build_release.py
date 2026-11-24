"""기신비상 한국어 패치 11-18 배포 꾸러미 셋을 조립한다(합본 F:\\AI\\khd-latest-game 은 읽기만).
  본 패치  dist\\KishinHisho_KO_20261118\\        (게임·영상 4편 덮어쓰기·메뉴 그림 덮어쓰기·사전 2권)
  소설     dist\\KishinHisho_KO_20261118_Novels\\ (서재 소설 6권, 설치형 — books.ini 에 합쳐 넣음)
  고화질   dist\\KishinHisho_KO_20261118_HD\\     (hires\\ 4배 그림 + 데몬베인 4배 텍스처)
꾸러미마다 files\\ + manifest.json(khd-release-v2) + Patch.ps1/Install.cmd/Restore.cmd/Check.cmd + 읽어주세요.txt.
  python -I -X utf8 build_release.py [--zip]"""
import configparser
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
G = Path(r"F:\AI\khd-latest-game")
JP_PURE = Path(r"P:\AI\khd-retl\JP_PURE")
JP120 = Path(r"P:\AI\khd-runtime-fix\jp120")
R1030 = Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\데몬베인 시리즈 2\delivery\KishinHisho_KO_DUEL_20261030")
HIRES = Path(r"Y:\AI\khd-4k-art\hires_pack")
TPL = HERE / "template"
DIST = HERE / "dist"
REL = "KishinHisho_KO_20261118"
m1030 = json.loads((R1030 / "manifest.json").read_text(encoding="utf-8-sig"))
v1030 = {f["path"].lower(): f["sha256"].lower() for f in m1030["files"]}
req1030 = {f["path"]: f["sha256"].lower() for f in m1030["required"]}
inv = json.loads((HERE / "inventory.json").read_text(encoding="utf-8"))


def sha(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()


def accepted(rel):
    out = set()
    for base in (JP_PURE, JP120):
        p = base / rel
        if p.is_file():
            out.add(sha(p))
    if rel.lower() in v1030:
        out.add(v1030[rel.lower()])
    return sorted(out)


NOVELS = re.compile(r"^ko-library/(books/novel-\d\.epub|covers/novel-\d\.png)$", re.I)
NOT_CORE = re.compile(r"^(mpeg\.pak|dmpack\.dat|dmpack001\.dat|dmpack600_retex\.dat)$|^autobattle/(adv_test\.txt|khd4k_frame\.raw)$|"
                      r"^ko-library/\.codex_backup/", re.I)
PRESERVE = {"autobattle/autobattle.ini", "khdcam.ini", "khdjump.ini", "ko-library/books.ini"}
GAME_WRITES = re.compile(r"^autobattle/battles/B\d\d/(skill_stats|danger|dodge_stats|stage_seen)\.tsv$", re.I)   # 자동 전투가 고쳐 씀


def default_ini():
    """합본 ini 에서 사람·이 PC 에 묶인 값만 기본으로(고화질 경로 비움 = 게임 폴더 hires, 원래 크기, DUEL 보통)"""
    raw = (G / "autobattle" / "autobattle.ini").read_bytes()
    text = raw.decode("utf-16")
    want = {("autobattle", "uhd_hires"): "", ("autobattle", "uhd_scale"): "1", ("autobattle", "uhd_mode"): "window",
            ("duel", "difficulty"): "1"}
    out, sec = [], None
    for ln in text.split("\r\n"):
        m = re.match(r"^\[(.+)\]$", ln.strip())
        if m:
            sec = m[1].lower()
        else:
            m = re.match(r"^([^=;]+)=(.*)$", ln)
            if m and (sec, m[1].strip().lower()) in want:
                ln = f"{m[1]}={want[(sec, m[1].strip().lower())]}"
            if m and re.match(r"(test_|uhd_dump|uhd_pictrace|uhd_check|kokuin_test)", m[1].strip(), re.I):
                raise SystemExit(f"test key in the merged ini: {ln}")
        out.append(ln)
    c = configparser.ConfigParser(strict=False, interpolation=None)
    c.read_string("\r\n".join(out))
    assert c["autobattle"]["uhd_hires"] == "" and c["autobattle"]["uhd_scale"] == "1" and c["duel"]["difficulty"] == "1"
    return b"\xff\xfe" + "\r\n".join(out).encode("utf-16-le")


def books_ini(ids):
    c = configparser.ConfigParser(strict=False, interpolation=None)
    c.optionxform = str
    c.read_string((G / "ko-library" / "books.ini").read_bytes().decode("utf-16"))
    books = [dict(c[s]) for s in c.sections() if s.startswith("Book")]
    pick = [b for b in books if b["Id"] in ids]
    pick.sort(key=lambda b: ids.index(b["Id"]))
    text = f"[Library]\r\nCount={len(pick)}\r\n"
    for i, b in enumerate(pick):
        text += f"\r\n[Book{i}]\r\nTitle={b['Title']}\r\nPath={b['Path']}\r\nId={b['Id']}\r\n"
    return b"\xff\xfe" + text.encode("utf-16-le"), books


class Pack:
    def __init__(self, name, title):
        self.root = DIST / name
        if self.root.exists():
            raise SystemExit(f"{self.root} exists - not overwriting a built package")
        self.files = []
        self.name, self.title = name, title

    def add(self, rel, src=None, data=None, **extra):
        dest = self.root / "files" / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if data is not None:
            dest.write_bytes(data)
        else:
            shutil.copyfile(src, dest)
            if sha(src) != sha(dest):
                raise SystemExit(f"source changed while copying: {rel}")
        row = dict(path=rel, size=dest.stat().st_size, sha256=sha(dest))
        row.update(extra)
        self.files.append(row)

    def finish(self, required=(), remove=(), books_add=None, known_packs=(), check_packs=False, docs=()):
        man = dict(format="khd-release-v2", release=self.name, title=self.title, date="2026-11-18", files=self.files,
                   required=list(required), remove=list(remove), known_packs=list(known_packs), check_packs=check_packs)
        if books_add:
            man["books_add"] = books_add
        (self.root / "manifest.json").write_text(json.dumps(man, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        shutil.copyfile(TPL / "Patch.ps1", self.root / "Patch.ps1")
        for mode, fn in (("Install", "Install.cmd"), ("Restore", "Restore.cmd"), ("Check", "Check.cmd")):
            (self.root / fn).write_bytes(("@echo off\r\nsetlocal\r\nchcp 65001 >nul\r\npowershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass "
                                          f"-File \"%~dp0Patch.ps1\" -Mode {mode} %*\r\nset \"PATCH_RESULT=%ERRORLEVEL%\"\r\necho.\r\npause\r\n"
                                          "exit /b %PATCH_RESULT%\r\n").encode("ascii"))
        for src, name in docs:
            shutil.copyfile(src, self.root / name)
        sums = []
        for p in sorted(self.root.rglob("*")):
            if p.is_file() and p.name != "SHA256SUMS.txt":
                sums.append(f"{sha(p)}  {p.relative_to(self.root).as_posix()}")
        (self.root / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
        total = sum(r["size"] for r in self.files)
        print(f"{self.name}: {len(self.files)} files, {total / 2**20:.1f} MB")


DIST.mkdir(exist_ok=True)
ONLY = next((a.split("=", 1)[1].split(",") for a in sys.argv if a.startswith("--only=")), ["core", "novels", "hd"])
DICT_IDS = ["codex-magicians", "codex-machines"]
core_books, all_books = books_ini(DICT_IDS)

# ---------------- 본 패치
built = []
if "core" in ONLY:
  core = Pack(REL, "기신비상 데몬베인 한국어 패치 (2026-11-18)")
  for r in inv["rows"]:
      rel = r["rel"]
      if r["status"] != "ship" or NOT_CORE.search(rel) or NOVELS.search(rel):
          continue
      extra = dict(accepted_before=accepted(rel), allow_absent=True)
      if rel in PRESERVE:
          extra["preserve_existing"] = True
      if GAME_WRITES.search(rel):
          extra["game_writes"] = True
      if rel == "autobattle/autobattle.ini":
          core.add(rel, data=default_ini(), ini_merge=True, **extra)
      elif rel == "ko-library/books.ini":
          core.add(rel, data=core_books, **extra)
      else:
          core.add(rel, src=G / rel, **extra)
  core.add("0mpeg.pak", src=HERE / "out" / "0mpeg.pak", allow_absent=True)
  core.add("dmpack004_ko_menu.dat", src=HERE / "out" / "dmpack004_ko_menu.dat", allow_absent=True)
  for n in ("LoaderDll.dll", "LocaleEmulator.dll"):
      core.add(n, src=G / n, allow_absent=True, accepted_before=[req1030[n]])
  core.add("Play.cmd", src=R1030 / "files" / "Play.cmd", allow_absent=True)
  required = [dict(path=n, sha256=req1030[n]) for n in ("0sound.pak", "0voice.pak", "advdata.pak", "cg.pak", "dmpack002.dat",
                                                         "sound.pak", "system.pak", "voice.pak")]
  required += [dict(path=n, sha256="") for n in ("mpeg.pak", "dmpack.dat", "dmpack001.dat")]
  remove = [dict(path="d3d8.dll", sha256=[v1030["d3d8.dll"]], why="(10-29 패치의 dgVoodoo — 이번 판부터 쓰지 않음)"),
            dict(path="dgVoodoo.conf", sha256=[v1030["dgvoodoo.conf"]], why="(dgVoodoo 설정)")]
  core.finish(required=required, remove=remove, known_packs=["dmpack600_retex.dat"], check_packs=True,
              docs=[(TPL / "읽어주세요.txt", "읽어주세요.txt"), (TPL / "THIRD_PARTY.md", "THIRD_PARTY.md"),
                    (TPL / "변경사항.md", "변경사항.md")])
  shutil.copytree(TPL / "licenses", core.root / "licenses")


  built.append(core)
# ---------------- 소설 6권
if "novels" in ONLY:
  nov = Pack(REL + "_Novels", "기신비상 서재 · 데몬베인 소설 6권 (2026-11-18)")
  order = [b["Id"] for b in all_books if b["Id"] not in DICT_IDS]
  for b in all_books:
      if b["Id"] in DICT_IDS:
          continue
      rel = "ko-library/" + b["Path"].replace("\\", "/")
      nov.add(rel, src=G / rel, allow_absent=True)
      cov = rel.replace("books/", "covers/").replace(".epub", ".png")
      nov.add(cov, src=G / cov, allow_absent=True)
  nov.finish(required=[dict(path="ko-library/books.ini", sha256=""), dict(path="KHDLore.dll", sha256="")],
             books_add=[dict(Title=b["Title"], Path=b["Path"], Id=b["Id"]) for b in all_books if b["Id"] not in DICT_IDS],
             docs=[(TPL / "읽어주세요_소설.txt", "읽어주세요.txt")])

  built.append(nov)

# ---------------- 고화질
if "hd" in ONLY:
  hd = Pack(REL + "_HD", "기신비상 고화질 그림 (2026-11-18)")
  paks = sorted({ln.split("\t")[1] for ln in (HIRES / "index.tsv").read_bytes().decode("cp932").splitlines() if ln.strip()})
  hd.add("hires/index.tsv", src=HIRES / "index.tsv", allow_absent=True)
  for p in paks:
      hd.add(f"hires/{p}", src=HIRES / p, allow_absent=True)
  hd.add("dmpack600_retex.dat", src=G / "dmpack600_retex.dat", allow_absent=True, accepted_before=accepted("dmpack600_retex.dat"))
  hd.finish(required=[dict(path="autobattle/khd4k.dll", sha256="")], docs=[(TPL / "읽어주세요_고화질.txt", "읽어주세요.txt")])

  built.append(hd)

if "--zip" in sys.argv:
    stores = {REL: {".pak", ".mpg", ".epub", ".png", ".jpg"}, REL + "_Novels": {".epub", ".png"}, REL + "_HD": {".pak", ".dat"}}
    for pk in built:
        store = stores[pk.name]
        z = DIST / (pk.name + ".zip")
        with zipfile.ZipFile(z, "w", allowZip64=True) as zf:
            for p in sorted(pk.root.rglob("*")):
                if p.is_file():
                    method = zipfile.ZIP_STORED if p.suffix.lower() in store else zipfile.ZIP_DEFLATED
                    zf.write(p, pk.name + "/" + p.relative_to(pk.root).as_posix(), compress_type=method, compresslevel=6)
        (DIST / (pk.name + ".zip.sha256")).write_text(f"{sha(z)}  {z.name}\n", encoding="utf-8")
        print(z.name, f"{z.stat().st_size / 2**20:.1f} MB")
