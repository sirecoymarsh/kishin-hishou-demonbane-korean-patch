"""KHDLore.dll 빌드(코덱스 tools\build.py 의 compile_dll 과 같은 명령 — w64devkit-x86 g++, 정적, 타임스탬프 없음 → 같은 소스면 같은 바이트)
  python -I -X utf8 build.py [출력 이름]"""
import hashlib, os, subprocess, sys
from pathlib import Path
A = Path(__file__).resolve().parent
BIN = Path(r"P:\AI\Codex\projects\11-eyes-crossover-ko\research\tools\w64devkit-x86-2.9.1\w64devkit\bin")
SDK = Path(r"P:\AI\Codex\projects\migrated-from-c\onedrive-chatgpt\월광의 카르네바레\localization\integration\revisions\library-20261103\deps\webview2\build\native\include")
os.environ["TEMP"] = os.environ["TMP"] = str(A / "tmp")
os.environ["PATH"] = str(BIN) + ";" + os.environ["PATH"]
out = A / "build" / (sys.argv[1] if len(sys.argv) > 1 else "KHDLore.dll")
cmd = [str(BIN / "g++.exe"), "-std=c++17", "-D_WIN32_WINNT=0x0601", "-O2", "-shared", "-s", "-static", "-Wl,--kill-at", "-Wl,--no-insert-timestamp", "-Wl,--image-base=0x6e540000",
       "-I" + str(SDK), str(A / "src" / "lore_host.cpp"), str(A / "src" / "library_webview.cpp"), "-o", str(out),
       "-luser32", "-lgdi32", "-lole32", "-luuid", "-lshlwapi"]
p = subprocess.run(cmd, capture_output=True)
(A / "build" / "compile.log").write_bytes(p.stdout + p.stderr)
if p.returncode:
    sys.exit((p.stdout + p.stderr).decode(errors="replace")[-3000:])
print(out, hashlib.sha256(out.read_bytes()).hexdigest()[:8], out.stat().st_size)
