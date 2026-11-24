"""autobattle.ini 의 4K 항목을 맞춘다 — 없는 항목은 기본값으로 더하고, 있는 항목은 값은 그대로 두고 바로 위 설명 줄만 고친다.

  python -I -X utf8 ini_add_uhd.py
"""
from pathlib import Path

INI = Path(r"P:\AI\khd-runtime-fix\game\autobattle\autobattle.ini")
ITEMS = [   # (키, 설명, 기본값) — 이 순서로 놓인다
    ("uhd", "; 4K 화면: 1 = 켬, 0 = 끔, 2 = 그리기 기록만(개발용)", "0"),
    ("uhd_mode", "; 4K 화면 방식: full = 모니터 전체(해상도는 안 바꿈), window = 큰 창", "full"),
    ("uhd_scale", "; window 방식일 때 창 배율(1~4, 800×600 의 몇 배)", "3"),
    ("uhd_text", "; 1 = 대사를 화면 해상도로 따로 그림(기본), 0 = 화면만 키움", "1"),
    ("uhd_battle", "; 1 = 전투를 화면 해상도로 그림(기본), 0 = 800×600 으로 그려 키움", "1"),
    ("uhd_trace", "; 1 = 그리기 기록을 남김(개발용)", "0"),
    ("uhd_offscreen", "; 1 = 창을 화면 밖에 둠(시험용)", "0"),
    ("uhd_probe", "; 1 = 대사 판을 읽는 곳 찾기(개발용)", "0"),
    ("uhd_capture", "; 시험용: 전투 프레임 저장(처음-끝/간격, 비우면 끔)", ""),
    ("uhd_envfix", "; 시험용: 전투 환경 매핑 실험(0 끔, 1 반사 좌표 두 개로, 2 반사 텍스처만)", "0"),
]

raw = INI.read_bytes()
text = raw.decode("utf-16")
nl = "\r\n" if "\r\n" in text else "\n"
lines = text.split(nl)
keys = {k for k, _c, _v in ITEMS}


def key_of(line: str) -> str:
    return line.split("=", 1)[0].strip() if "=" in line and not line.lstrip().startswith(";") else ""


values = {key_of(l): l.split("=", 1)[1] for l in lines if key_of(l) in keys}
first = min(k for k, l in enumerate(lines) if key_of(l) in keys)
start = first - 1 if first > 0 and lines[first - 1].lstrip().startswith(";") else first
# the 4K block ends after its last key line
last = max(k for k, l in enumerate(lines) if key_of(l) in keys)
block = []
for key, comment, default in ITEMS:
    block += [comment, f"{key}={values.get(key, default)}"]
# keep any non-4K lines that sat inside the old block (none expected)
inside = [l for l in lines[start:last + 1] if l.strip() and not key_of(l) in keys and not l.lstrip().startswith(";")]
lines[start:last + 1] = block + inside
INI.write_bytes(nl.join(lines).encode("utf-16"))
print("\n".join(block + inside))
