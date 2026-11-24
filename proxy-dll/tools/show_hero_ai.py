"""사용자가 직접 보는 주인공 AI 자동 전투(11-01) — 사용자 「잠깐 게임좀 켜줄래?」「직접 보면서 하게썽」「자동으로 돌려줘」.

run_duel_test.py 를 그대로 쓰되 게임 사본만 아르곤 사본(F:)으로 바꾼다: 화면 안·소리 켬·자막 켬·놀이 화면 설정(--like-play),
프레임 저장 끔, 자동 전투·균형 모드(노말은 사본 설정 그대로). 지금 그 사본 게임이 켜져 있으면 전투 중이 아닐 때만 정상 종료하고 시작한다.
autobattle.ini 는 끝나면 바이트 그대로 되돌아간다(run_duel_test 가 함).

  python -I -X utf8 show_hero_ai.py [짝...]      (기본: 8짝)
  python -I -X utf8 show_hero_ai.py --kokuin [짝...]   (11-02 네로 각인 666: 네로를 시험 키 ↓+X+A 로, 자동 전투 끔; 기본 QM:BM·GM·MM·CM)
  python -I -X utf8 show_hero_ai.py --kokuin-test [짝...]   (시험 모드: 시작부터 ↓+X+A, 시전을 아무것도 못 끊음 — 묶음 521 은 이 판 동안만;
                                                        기본 QM:QM·RM·SM(사용자 「보고싶긴하네」「미러전이나」) 뒤
                                                        QM:LM·IM·JM = 시전 중 잡기로 끊던 상대들)
"""
import ctypes
import ctypes.wintypes as W
import sys
from pathlib import Path

G = Path(r"F:\AI\khd-argon-game")
KOKUIN = "--kokuin" in sys.argv
KTEST = "--kokuin-test" in sys.argv    # 11-02 사용자 「시작부터 각인 666 써대고 방해 안받는 모드」: 시험 묶음 521 을 이 판 동안만 덧넣음
NERODB = "--nerodb" in sys.argv        # 11-03 사용자 「아틀란티스 스트라이크나 신수 형태를 직접 쏴줄래?」: 데몬베인을 시험 키로(nerodb)
PAIRS = [a for a in sys.argv[1:] if ":" in a] or (["AM:QM"] if NERODB else
                                                   ["QM:QM", "QM:RM", "QM:SM", "QM:LM", "QM:IM", "QM:JM"] if KTEST else
                                                   ["QM:BM", "QM:GM", "QM:MM", "QM:CM"] if KOKUIN else
                                                   ["AM:CM", "A2:MM", "EM:GM", "EM:IM", "HM:GM", "HM:LM", "HM:RM", "F2:MM"])
MODE = (["--keys=nerobeast", "--limit=80"] if NERODB else   # 11-03 신수 형태만(아틀란티스 스트라이크 뺌)
        ["--keys=kokuinfast", "--limit=60", r"--pack=P:\AI\khd-hero-ai\kokuin\out\dmpack521_kokuin666_test.dat"] if KTEST else
        ["--keys=kokuin", "--limit=90"] if KOKUIN else ["--passive", "--auto", "--limit=300"])
sys.argv = [sys.argv[0], *PAIRS, *MODE, "--balance", "--onscreen", "--sound", "--subtitles", "--like-play", "--capture="]
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
import run_duel_test as D  # noqa: E402

S, T, V = D.S, D.T, D.T.V
S.GAME = G
S.INI = G / "autobattle" / "autobattle.ini"
T.CAPDIR = G / "autobattle" / "cap"
T.PROXY_LOG = G / "autobattle" / "proxy_log.txt"

T.OUT.mkdir(parents=True, exist_ok=True)
ws = S.game_windows()
if ws:
    h = ws[0][0]
    p = W.DWORD()
    T.u.GetWindowThreadProcessId(h, ctypes.byref(p))
    ph = V.k.OpenProcess(0x0010 | 0x0400, False, p.value)
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = W.DWORD(1024)
        ctypes.windll.kernel32.QueryFullProcessImageNameW(ph, 0, buf, ctypes.byref(n))
        exe = Path(buf.value)
        in_fight = bool(V.module_base(ph, "demonbane.dll"))
    finally:
        V.k.CloseHandle(ph)
    if exe.resolve() != (G / "KishinHisho_chs.exe").resolve():
        sys.exit(f"켜진 게임이 아르곤 사본이 아니다({exe}) — 손대지 않음")
    if in_fight:
        sys.exit("켜진 게임이 전투 중이다 — 끄지 않음")
    T.log(f"the running game {p.value} (title/menu, not in a fight) is closed normally first")
    if not T.quit_game(h, p.value):
        sys.exit("켜진 게임이 정상 종료되지 않았다 — 멈춤")
    D.wait_gone(p.value)
D.main()
