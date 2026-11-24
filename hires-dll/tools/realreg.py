"""게임이 보는 「진짜」 레지스트리 읽기·쓰기 — Claude 앱 안에서 돈 명령의 HKCU 쓰기는 앱 전용 덧칠 층에만 남아
게임(WMI 로 앱 밖에서 켬)에는 안 보인다(10-15 확인: 앱 안 FullScreen=0, 게임이 읽은 값 1). 그래서 앱 밖(WMI)에서 읽고 쓰고
결과를 파일로 받는다.

11-03: 앱 밖에서 띄운 콘솔 프로그램(cmd·reg.exe)이 Windows 터미널로 넘겨지다 하나에 30초씩 막혔다(사용자 화면에 「reg.exe」
터미널 창도 떴고, 시험이 창 위치를 되돌리다 끊김) → 콘솔이 없는 wscript(VBScript)로 읽고(StdRegProv) 쓴다(WScript.Shell).

  python -I -X utf8 realreg.py query [키=Software\\nitroplus\\KishinHisho_Win11Copy]
  python -I -X utf8 realreg.py set <이름> <정수> [키]
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

COPY_KEY = r"Software\nitroplus\KishinHisho_Win11Copy"
WORK = Path(r"P:\AI\khd-4k\tmp")


def _outside(cmdline: str) -> None:
    ps = ("$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '"
          + cmdline.replace("'", "''") + "'; CurrentDirectory = '" + str(WORK) + "' }; $r.ProcessId")
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True, check=True)


def _wscript(body: list[str], tag: str) -> str:
    """VBScript 를 앱 밖에서 콘솔 없이 돌리고 그 출력(f.WriteLine)을 돌려준다 — .part 로 쓰고 끝에 이름을 바꿔 반쪽을 안 읽음"""
    WORK.mkdir(parents=True, exist_ok=True)
    stem = WORK / f"{tag}_{time.time_ns()}"
    out, part, script = stem.with_suffix(".txt"), stem.with_suffix(".part"), stem.with_suffix(".vbs")
    lines = ['Set fso = CreateObject("Scripting.FileSystemObject")', f'Set f = fso.CreateTextFile("{part}", True)']
    lines += body + ["f.Close", f'fso.MoveFile "{part}", "{out}"']
    script.write_text("\r\n".join(lines) + "\r\n", encoding="mbcs")
    _outside(f'"C:\\Windows\\System32\\wscript.exe" //B //Nologo "{script}"')
    for _ in range(150):
        if out.exists():
            text = out.read_text(encoding="mbcs", errors="replace")
            out.unlink(missing_ok=True)
            script.unlink(missing_ok=True)
            return text
        time.sleep(0.1)
    raise RuntimeError(f"realreg: {script} gave no answer in 15 s")


def query(key: str = COPY_KEY) -> dict:
    """그 키의 REG_DWORD 값들 {이름: 부호 없는 정수} — 옛 reg query 판과 같은 꼴"""
    text = _wscript(['Const HKCU = &H80000001',
                     'Set reg = GetObject("winmgmts:{impersonationLevel=impersonate}!\\\\.\\root\\default:StdRegProv")',
                     f'reg.EnumValues HKCU, "{key}", names, types',
                     'If IsArray(names) Then',
                     '  For i = 0 To UBound(names)',
                     '    If types(i) = 4 Then',
                     f'      reg.GetDWORDValue HKCU, "{key}", names(i), v',
                     '      If v < 0 Then v = v + 4294967296',
                     '      f.WriteLine names(i) & vbTab & CStr(v)',
                     '    End If',
                     '  Next',
                     'End If'], "regq")
    vals = {}
    for line in text.splitlines():
        name, _, v = line.partition("\t")
        if name and v.strip().isdigit():
            vals[name] = int(v)
    return vals


def set_dword(name: str, value: int, key: str = COPY_KEY) -> None:
    v = int(value) & 0xFFFFFFFF
    signed = v - 0x100000000 if v >= 0x80000000 else v        # RegWrite 의 REG_DWORD 는 부호 있는 Long 을 받는다
    _wscript([f'CreateObject("WScript.Shell").RegWrite "HKCU\\{key}\\{name}", CLng({signed}), "REG_DWORD"',
              'f.WriteLine "ok"'], "regs")
    for _ in range(10):
        if query(key).get(name) == v:
            return
        time.sleep(0.2)
    raise RuntimeError(f"{name} did not become {value}")


if __name__ == "__main__":
    if sys.argv[1] == "query":
        print(query(sys.argv[2] if len(sys.argv) > 2 else COPY_KEY))
    elif sys.argv[1] == "set":
        set_dword(sys.argv[2], int(sys.argv[3]), sys.argv[4] if len(sys.argv) > 4 else COPY_KEY)
        print("ok", query(sys.argv[4] if len(sys.argv) > 4 else COPY_KEY))
