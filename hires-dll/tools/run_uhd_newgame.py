"""4K 종합 시험 — 사용자 설정(자동 전투·자동 진행·스킵)에 4K(모니터 전체, 화면 밖)·소리 끔으로 새 게임을 시작해
프롤로그 전투와 그 뒤 이야기 자동 진행을 몇 분 지켜본다. 15초마다 화면을 찍고 정상 종료.

  python -I -X utf8 run_uhd_newgame.py [분=4] [간격 초=15]
"""
from __future__ import annotations

import sys
import time

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
MINUTES = float(ARGS[0]) if ARGS else 4
EVERY = float(ARGS[1]) if len(ARGS) > 1 else 15
ADV_LOG = S.GAME / "autobattle" / "adv_log.tsv"


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다")
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 1645), reg.get("YPos", 741)]
    S.SHOTS.mkdir(parents=True, exist_ok=True)
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    log_start = ADV_LOG.stat().st_size if ADV_LOG.exists() else 0
    S.set_ini(raw, enc, agent=1, advance=1, skip=1, mute=1, subtitles=0,
              uhd=1, uhd_mode="full", uhd_offscreen=1, uhd_trace=1, uhd_probe=0, uhd_text=1)
    try:
        h = T3.launch()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4); S.shot(h, "N00_title_menu")
        UA.click(h, 405, 344); time.sleep(3)                     # 시작하기
        S.dialog_yes(timeout=3)
        t0 = time.time()
        k = 0
        while time.time() - t0 < MINUTES * 60:
            time.sleep(EVERY)
            if not S.running():
                S.log("game is gone")
                break
            k += 1
            S.shot(h, f"N{k:02d}")
        if S.running():
            UA.quit_game(h)
    finally:
        S.INI.write_bytes(raw)
        S.log("autobattle.ini restored")
        if not S.running():
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            S.log(f"window position restored to {pos}")
        if ADV_LOG.exists():
            with open(ADV_LOG, encoding="utf-8", errors="replace") as fh:
                fh.seek(log_start)
                tail = fh.read()
            (S.SHOTS / "adv_log_this_run.tsv").write_text(tail, encoding="utf-8")
            S.log("driver log this run:\n" + "\n".join(tail.strip().splitlines()[:40]))


if __name__ == "__main__":
    main()
