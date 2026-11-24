"""영상 묶음: 합본 mpeg.pak 과 일본판 원본(267,981,498B)을 항목 단위로 대조(읽기만)"""
import importlib.util
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
spec = importlib.util.spec_from_file_location("kpak", r"P:\AI\khd-runtime-fix\tools\kishinhisho_pak_fixed.py")
K = importlib.util.module_from_spec(spec)
sys.modules["kpak"] = K
spec.loader.exec_module(K)
ORIG = Path(r"P:\AI\Codex\projects\kishin-hishou-win11-investigation\runtime\jp120_menu\mpeg.pak")
CUR = Path(r"F:\AI\khd-latest-game\mpeg.pak")
c = K.compare_archives(ORIG, CUR)
print({k: (v if not isinstance(v, (list, dict)) else (len(v) if len(v) > 12 else v)) for k, v in c.items()})
