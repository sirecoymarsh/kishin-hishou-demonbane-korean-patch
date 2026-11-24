#!/bin/bash
# 효과음 목록(.se) A/B — 같은 판(앰브로시우스 대 D 블러드, 자동·균형·노말, 새로 켠 게임의 첫 판, 60초)을
# .se 없는 코덱스 후보 513 묶음으로 한 판 돌린다. .se 를 채운 합본 판은 run1 의 다섯째 판(같은 조건)과 비교한다.
set -e
G=/p/AI/khd-runtime-fix/game
FIXED=/p/AI/khd-hero-ai/merged/out/dmpack513_hm_owned.dat
OLD="/p/AI/Codex/projects/migrated-from-c/onedrive-chatgpt/데몬베인 시리즈 2/work/duel-settings-20261021/player-only-ai-20261026/HM/normal/r3/out/dmpack513_hm_owned.dat"
T=/p/AI/khd-hero-ai/merged/tmp
want=$(sha256sum "$FIXED" | cut -c1-64)
[ "$(sha256sum $G/dmpack513_hm_owned.dat | cut -c1-64)" = "$want" ] || { echo "시험 사본 513 이 합본이 아니다 — 멈춤"; exit 1; }
cd /p/AI/khd-runtime-fix/tools
cp -p "$OLD" $G/dmpack513_hm_owned.dat
echo "A: .se 없음 $(sha256sum $G/dmpack513_hm_owned.dat | cut -c1-16)"
python -I -X utf8 run_duel_test.py HM:GM --passive --auto --balance --trace-sound --limit=60 > $T/ab_noSe.out 2>&1 || echo "A 시험 rc=$?"
cp -p "$FIXED" $G/dmpack513_hm_owned.dat
[ "$(sha256sum $G/dmpack513_hm_owned.dat | cut -c1-64)" = "$want" ] || { echo "합본 513 되돌리기 실패"; exit 1; }
echo "합본 513 되돌림 $(sha256sum $G/dmpack513_hm_owned.dat | cut -c1-16) — A/B 끝"
