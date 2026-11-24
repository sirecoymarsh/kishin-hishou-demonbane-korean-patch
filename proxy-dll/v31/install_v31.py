"""v31 설치: 대리 DLL(스토리 어려움 원령 주탄 절반) + 스토리 어려움 묶음(긴 시전 가드) — 합본·시험 사본, 앞 파일은 백업"""
import hashlib, shutil, sys
from pathlib import Path
H = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()[:8]
SRC = {"dinput8.dll": Path(r"P:\AI\khd-hero-ai\v31\out\dinput8.dll"),
       r"autobattle\hard\dmpack900_hard.dat": Path(r"P:\AI\khd-hero-ai\v31\out\dmpack900_hard.dat")}
for game, bk in ((r"F:\AI\khd-latest-game", r"P:\AI\khd-hero-ai\backup\khd-latest-game_331cec40"),
                 (r"F:\AI\khd-fullrun-game", r"P:\AI\khd-hero-ai\backup\khd-fullrun-game_before_v31")):
    g, b = Path(game), Path(bk)
    for rel, src in SRC.items():
        dst = g / rel
        keep = b / rel
        keep.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists() and not keep.exists():
            shutil.copy2(dst, keep)
        was = H(dst) if dst.exists() else "-"
        shutil.copy2(src, dst)
        print(f"{game} {rel}: {was} -> {H(dst)}")
    stale = g / "dmpack900_hard.dat"
    if stale.exists():
        print(f"  (game folder has a {H(stale)} dmpack900_hard.dat - the proxy swaps it at the next fight)")
