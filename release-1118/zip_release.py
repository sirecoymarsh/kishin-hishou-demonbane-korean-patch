"""dist\\ 의 꾸러미 폴더 셋을 압축(이미 압축된 그림·영상·묶음은 저장만) + .sha256 + 압축본을 다시 열어 SHA256SUMS 대조."""
import hashlib
import sys
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
DIST = Path(r"Y:\AI\claude-work\khd-release-1118\dist")
STORE = {"KishinHisho_KO_20261118": {".pak", ".mpg", ".epub", ".png", ".jpg"},
         "KishinHisho_KO_20261118_Novels": {".epub", ".png"},
         "KishinHisho_KO_20261118_HD": {".pak", ".dat", ".tsv"}}


def sha_file(p):
    d = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            d.update(b)
    return d.hexdigest()


ONLY = [a for a in sys.argv[1:] if not a.startswith('-')]
for name, store in STORE.items():
    if ONLY and name not in ONLY:
        continue
    root = DIST / name
    z = DIST / (name + ".zip")
    tmp = z.with_suffix(".zip.writing")
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as zf:
        for p in sorted(root.rglob("*")):
            if p.is_file():
                method = zipfile.ZIP_STORED if p.suffix.lower() in store else zipfile.ZIP_DEFLATED
                zf.write(p, name + "/" + p.relative_to(root).as_posix(), compress_type=method, compresslevel=6)
    tmp.replace(z)
    sums = {ln.split("  ", 1)[1]: ln.split("  ", 1)[0] for ln in (root / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines() if ln}
    bad = 0
    with zipfile.ZipFile(z) as zf:
        for rel, h in sums.items():
            d = hashlib.sha256()
            with zf.open(name + "/" + rel) as f:
                for b in iter(lambda: f.read(1 << 22), b""):
                    d.update(b)
            bad += d.hexdigest() != h
    zh = sha_file(z)
    (DIST / (name + ".zip.sha256")).write_text(f"{zh}  {z.name}\n", encoding="utf-8")
    print(f"{z.name}: {z.stat().st_size / 2**20:.1f} MB, {len(sums)} files checked, mismatches {bad}, sha256 {zh[:8]}")
