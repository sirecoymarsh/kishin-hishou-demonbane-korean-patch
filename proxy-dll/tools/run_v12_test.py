"""v12 시험 사본(F:\\AI\\khd-v12-game — GPT v11 런타임 사본 + 대리 DLL v12)으로 run_duel_test 를 돌린다(11-03 자동 전투 UI·
DUEL 판 중간 넘기기). 창은 화면 밖·소리 끔이 기본(--onscreen --sound 등은 run_duel_test 그대로 넘김).

  python -I -X utf8 run_v12_test.py GM:CM [--auto] --keys=handover --handover=7000 [--limit=45] [--balance] [--difficulty=1]

--handover=N: autobattle.ini handover_test_ms=N(그 뒤 일시정지를 열 때마다 1P 를 넘기고 창을 닫음, 시험 전용).
끝나면 autobattle.ini 는 바이트 그대로 돌아간다(run_duel_test 가 한 번, 이 도구가 handover 줄까지 한 번 더).
"""
import ctypes
import sys
from pathlib import Path

G = Path(next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--game=")), r"F:\AI\khd-v12-game"))   # 11-03 합친 사본도
sys.argv = [a for a in sys.argv if not a.startswith("--game=")]
HANDOVER = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--handover=")), None)
# --pause-capture=P:\...\x.bmp: 일시정지 창이 열리고 60프레임 뒤 그 화면(GPT pause_menu.h 의 pause_capture, P: 경로만) + pause_trace
CAPTURE = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--pause-capture=")), None)
# --ini=키=값 (여러 번): 그 판 동안만 autobattle.ini [autobattle] 에 더함(시험용 스위치)
EXTRA = [a.split("=", 1)[1].split("=", 1) for a in sys.argv if a.startswith("--ini=")]
sys.argv = [a for a in sys.argv if not a.startswith(("--handover=", "--pause-capture=", "--ini="))]
# 11-05 23:0x: the tests of 21:41-22:37 ran without the Ambrosius DUEL hook (HdTick, hm_duel_ai) that the merged copy has on
# (its autobattle.ini hm_duel_ai=1; the v12 ini has no such line, the 17:47-19:08 runs passed it by hand) - code inside that
# hook never ran. Mirror the merged copy's value unless --ini=hm_duel_ai=... says otherwise.
MERGED_INI = Path(r"F:\AI\khd-latest-game\autobattle\autobattle.ini")
if not any(k.split(".")[-1] == "hm_duel_ai" for k, _v in EXTRA) and MERGED_INI.exists():
    _hm = ctypes.windll.kernel32.GetPrivateProfileIntW("autobattle", "hm_duel_ai", 0, str(MERGED_INI))
    EXTRA.append(["hm_duel_ai", str(_hm)])
    print(f"hm_duel_ai={_hm} for this run (as in the merged copy)")
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import run_duel_test as D  # noqa: E402

S, T = D.S, D.T
S.GAME = G
S.INI = G / "autobattle" / "autobattle.ini"
T.CAPDIR = G / "autobattle" / "cap"
T.PROXY_LOG = G / "autobattle" / "proxy_log.txt"

raw = S.INI.read_bytes()
try:
    if HANDOVER:
        assert HANDOVER.isdigit(), HANDOVER
        ctypes.windll.kernel32.WritePrivateProfileStringW("autobattle", "handover_test_ms", HANDOVER, str(S.INI))
        ctypes.windll.kernel32.WritePrivateProfileStringW("autobattle", "handover_trace", "1", str(S.INI))
    for k, v in EXTRA:
        # 11-05: "section.key" for another section - e.g. --ini=duel.balance=0 (the DUEL 균형 모드 check box writes [duel]
        # balance, which wins over [autobattle] duel_balance that --balance sets)
        sec, key = k.split(".", 1) if "." in k else ("autobattle", k)
        ctypes.windll.kernel32.WritePrivateProfileStringW(sec, key, v, str(S.INI))
    if CAPTURE:
        assert CAPTURE[:3].upper() == "P:\\" and CAPTURE.lower().endswith(".bmp"), CAPTURE
        ctypes.windll.kernel32.WritePrivateProfileStringW("autobattle", "pause_capture", CAPTURE, str(S.INI))
        ctypes.windll.kernel32.WritePrivateProfileStringW("autobattle", "pause_trace", "1", str(S.INI))
    D.main()
finally:
    S.INI.write_bytes(raw)
