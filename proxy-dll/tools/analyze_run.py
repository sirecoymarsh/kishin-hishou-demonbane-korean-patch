"""시험 판 정리 — 대리 DLL 기록(proxy_log.txt)에서 판마다 AI 켜짐·요청/시작·체력·KO·소리 기록을 센다(읽기만).

python -I -X utf8 analyze_run.py <proxy_log.txt> <시작 시각 "YYYY-MM-DD HH:MM:SS">
"""
from __future__ import annotations

import re
import sys
from datetime import datetime

path, since = sys.argv[1], datetime.strptime(sys.argv[2], "%Y-%m-%d %H:%M:%S")
LINE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) pid (\d+)\s+(.*)$")
ENABLED = re.compile(r"\b(HM_NATIVE|AM_NATIVE|A2_NATIVE|EM_OWNED_ENABLED|F2_NATIVE) enabled=(\d)(.*)")
OFF = re.compile(r"\b(HM_NATIVE DISABLED|AM_NATIVE DISABLED|A2_NATIVE fingerprint|A2_NATIVE private|EM_OWNED_DISABLED|"
                 r"F2_NATIVE disabled|F2_NATIVE missing|AM_NATIVE missing|HM_NATIVE missing)")
HP = re.compile(r"\b(HM|AM|A2|EM|F2)_STATE\b.* hp=(-?\d+) enemyhp=(-?\d+)")
fights: list[dict] = []
cur = None
with open(path, encoding="utf-8", errors="replace") as f:
    for raw in f:
        m = LINE.match(raw.rstrip("\n"))
        if not m:
            continue
        t = datetime.strptime(m[1], "%Y-%m-%d %H:%M:%S")
        if t < since:
            continue
        pid, msg = m[2], m[3]
        if msg.startswith("fight difficulty scope: scene="):
            cur = dict(pid=pid, start=t, scene=int(msg.split("scene=")[1].split()[0]), lines=[], enabled=[], off=[],
                       req=0, started=0, not_started=0, finished=0, play=0, made=0, hp=None, ko=None, over=None, sound_lines=0)
            fights.append(cur)
            continue
        if cur is None or pid != cur["pid"]:
            continue
        if msg.startswith(("DUEL auto battle:", "DUEL balance:", "HM_GENERAL", "AION_ENABLED", "NERO_GROUND balanced")):
            cur["lines"].append(msg[:120])
        e = ENABLED.search(msg)
        if e:
            cur["enabled"].append(f"{e[1]}={e[2]}{e[3][:70]}")
        o = OFF.search(msg)
        if o:
            cur["off"].append(msg[:120])
        if re.search(r"\b(HM|AM|A2|EM|F2)_REQUEST\b.*attack=1", msg):
            cur["req"] += 1
        if re.search(r"\b(HM|AM|A2|EM|F2)_STARTED\b", msg):
            cur["started"] += 1
        if re.search(r"\b(HM|AM|A2|EM|F2)_NOT_STARTED\b", msg):
            cur["not_started"] += 1
        if re.search(r"\b(HM|AM|A2|EM|F2)_FINISHED\b", msg):
            cur["finished"] += 1
        if msg.startswith("SOUND "):
            cur["sound_lines"] += 1
            if msg.startswith("SOUND play"):
                cur["play"] += 1
            if msg.startswith("SOUND made"):
                cur["made"] += 1
        h = HP.search(msg)
        if h:
            cur["hp"] = (int(h[2]), int(h[3]))
            if cur["ko"] is None and (int(h[2]) <= 0 or int(h[3]) <= 0):
                cur["ko"] = (t - cur["start"]).total_seconds()
        if msg.startswith("fight over") and cur["over"] is None:
            cur["over"] = (t - cur["start"]).total_seconds()

for i, c in enumerate(fights, 1):
    print(f"[{i}] pid {c['pid']} {c['start']:%H:%M:%S} scene {c['scene']}")
    for x in c["lines"]:
        print("     ", x)
    print("      AI:", c["enabled"] or "-", "| 꺼짐:", c["off"] or "-")
    print(f"      공격 요청 {c['req']} 시작 {c['started']} 미시작 {c['not_started']} 끝 {c['finished']}"
          f" | 체력(마지막 AI 기록) {c['hp']} KO {c['ko']}s | 판 끝 {c['over']}s")
    print(f"      소리 기록 {c['sound_lines']}줄 (버퍼 {c['made']}·재생 {c['play']})")
