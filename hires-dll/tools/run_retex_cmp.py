"""4K 로봇 텍스처 비교 촬영(10-16) — 데몬베인 텍스처를 4배로 키운 덧씌우기 묶음 하나를 사본에 잠깐 넣고(orig 는 안 넣음)
FINISHERS 칸 하나를 화면 밖·소리 끔·활성화 없음(khd4k uhd_offscreen)으로 틀어, 게임 안 프레임을 저장하고 게임 메모리와
앞 창(포커스)을 잰다. 정상 종료.
사용자 10-16 「데몬베인 텍스처를 방식 3~4개로 키워서 4K 게임 화면에서 비교해 줘. 게임이 큰 텍스처를 읽는지랑 메모리도 같이
확인해.」 「백그라운드에서 소리 끄고 실험해줘 자꾸 화면 가리면 내가 작업이 힘들어」

  python -I -X utf8 run_retex_cmp.py <orig|ultrasharp|remacri|hifi|x4plus> --cap=<uhd_capture> [칸=4] [초=40]

묶음 P:\\AI\\khd-4k\\tmp\\retex\\packs\\dmpack600_retex_<방식>.dat → game\\dmpack600_retex.dat (게임이 꺼진 뒤 지운다).
결과 P:\\AI\\khd-4k\\tmp\\retex\\runs\\<방식>\\ — 프레임 BMP, mem.tsv(1초마다: 사적 메모리·작업 집합·최대 커밋·쓰는 주소 공간·
가장 큰 빈 칸), khd4k_trace.txt 사본, run.log. 게임이나 그 도우미가 앞 창이 되면 적고 곧바로 사용자 창으로 돌려준다.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import hashlib
import shutil
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, r"P:\AI\Codex\projects\khd_replay_20261015\tools")
sys.path.insert(0, r"P:\AI\khd-4k\tools")
import run_finisher_test as F  # noqa: E402
import run_smoke as S  # noqa: E402
import run_story_test3 as T3  # noqa: E402
import realreg  # noqa: E402
import run_uhd_a as UA  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
WAY = ARGS[0] if ARGS else ""
PICK = int(ARGS[1]) if len(ARGS) > 1 else 4
SECONDS = float(ARGS[2]) if len(ARGS) > 2 else 40
CAP = next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--cap=")), "")
CAPFROM = int(next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--capfrom=")), "1"))   # 0 = from the device
MODE = next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--mode=")), "full")          # full | window
SCALE = int(next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--scale=")), "2"))      # window: 800x600 x this
TAG = next((x.split("=", 1)[1] for x in sys.argv if x.startswith("--tag=")), "")               # results in runs\<way><tag>
assert WAY in ("orig", "ultrasharp", "remacri", "hifi", "x4plus"), WAY
RETEX = Path(r"P:\AI\khd-4k\tmp\retex")
PACK_SRC = RETEX / "packs" / f"dmpack600_retex_{WAY}.dat"
PACK_DST = S.GAME / "dmpack600_retex.dat"
OUT = RETEX / "runs" / (WAY + TAG)
CAPDIR = S.GAME / "autobattle" / "cap"
GAME_EXES = ("kishinhisho_chs.exe", "khdautobattle.exe", "khdsubtitles.exe")

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32.GetForegroundWindow.restype = W.HWND
user32.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
user32.SetForegroundWindow.argtypes = [W.HWND]
user32.IsWindow.argtypes = [W.HWND]
kernel32.OpenProcess.restype = W.HANDLE
kernel32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
kernel32.CloseHandle.argtypes = [W.HANDLE]
kernel32.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD)]


class PMC(ctypes.Structure):                       # PROCESS_MEMORY_COUNTERS_EX
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


class MBI(ctypes.Structure):                       # MEMORY_BASIC_INFORMATION (64-bit caller)
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", W.DWORD),
                ("PartitionId", W.WORD), ("RegionSize", ctypes.c_size_t), ("State", W.DWORD), ("Protect", W.DWORD),
                ("Type", W.DWORD)]


kernel32.K32GetProcessMemoryInfo.argtypes = [W.HANDLE, ctypes.POINTER(PMC), W.DWORD]
kernel32.VirtualQueryEx.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
kernel32.VirtualQueryEx.restype = ctypes.c_size_t
LINES: list[str] = []


def log(m: str):
    S.log(m)
    LINES.append(f"{time.strftime('%H:%M:%S')}\t{m}")


def exe_of(pid: int) -> str:
    hp = kernel32.OpenProcess(0x1000, False, pid)            # PROCESS_QUERY_LIMITED_INFORMATION
    if not hp:
        return "?"
    try:
        buf, n = ctypes.create_unicode_buffer(520), W.DWORD(520)
        return buf.value.rsplit("\\", 1)[-1] if kernel32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(n)) else "?"
    finally:
        kernel32.CloseHandle(hp)


def watch_foreground(fg0, stop: threading.Event, events: list):
    """the user's window stays in front: if the game or a helper of it ever is, note it and give the front back"""
    names: dict[int, str] = {}
    while not stop.is_set():
        fg = user32.GetForegroundWindow()
        pid = W.DWORD()
        user32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
        if pid.value not in names:
            names[pid.value] = exe_of(pid.value)
        if names[pid.value].lower() in GAME_EXES:
            back = bool(fg0 and user32.IsWindow(fg0) and user32.SetForegroundWindow(fg0))
            events.append(f"{time.strftime('%H:%M:%S')} {names[pid.value]} came to the front; given back: {back}")
            stop.wait(0.5)
            continue
        stop.wait(0.1)


def address_space(hp) -> tuple[int, int]:
    """bytes of the 2 GB (the game is not large-address-aware) in use (reserved or committed), and the largest free run"""
    used = free_max = 0
    addr, m = 0x10000, MBI()
    while addr < 0x7FFF0000:
        if not kernel32.VirtualQueryEx(hp, ctypes.c_void_p(addr), ctypes.byref(m), ctypes.sizeof(m)):
            break
        if m.State == 0x10000:                                 # MEM_FREE
            free_max = max(free_max, m.RegionSize)
        else:
            used += m.RegionSize
        addr = (m.BaseAddress or 0) + m.RegionSize
    return used, free_max


def sample_memory(pid: int, stop: threading.Event, rows: list):
    hp = kernel32.OpenProcess(0x0410, False, pid)            # QUERY_INFORMATION | VM_READ
    if not hp:
        rows.append(("cannot open", ctypes.get_last_error()))
        return
    t0 = time.time()
    try:
        while not stop.is_set():
            pmc = PMC()
            pmc.cb = ctypes.sizeof(PMC)
            if not kernel32.K32GetProcessMemoryInfo(hp, ctypes.byref(pmc), pmc.cb):
                break
            used, free_max = address_space(hp)
            rows.append((round(time.time() - t0, 1), pmc.PrivateUsage, pmc.WorkingSetSize, pmc.PeakPagefileUsage, used,
                         free_max))
            stop.wait(1.0)
    finally:
        kernel32.CloseHandle(hp)


def mb(v) -> str:
    return f"{v / 1048576:.0f}"


def main():
    if S.running():
        sys.exit("게임이 이미 켜져 있다 — 시험하지 않는다")
    if PACK_DST.exists():
        sys.exit(f"{PACK_DST.name} 이 이미 사본에 있다 — 손대지 않는다")
    if CAPDIR.exists() and any(CAPDIR.iterdir()):
        sys.exit(f"{CAPDIR} 에 전 시험 그림이 남아 있다")
    if OUT.exists() and any(OUT.iterdir()):
        sys.exit(f"{OUT} 가 비어 있지 않다")
    if WAY != "orig" and not PACK_SRC.exists():
        sys.exit(f"{PACK_SRC} 없음")
    OUT.mkdir(parents=True, exist_ok=True)
    raw, enc = S.read_ini()
    reg = realreg.query()
    pos = [reg.get("XPos", 0), reg.get("YPos", 0)]
    if UA.TRACE.exists():
        UA.TRACE.unlink()
    # S.set_ini only changes lines that are there: a key the user's ini lacks goes in for the run (raw is put back after)
    text = raw.decode(enc)
    if "uhd_capfrom=" not in text:
        eol = "\r\n" if "\r\n" in text else "\n"
        text = text.replace("uhd_capture=", f"uhd_capfrom=0{eol}uhd_capture=", 1)
        if "uhd_capfrom=" not in text:
            sys.exit("autobattle.ini 에 uhd_capture 줄이 없어 uhd_capfrom 을 넣을 자리를 못 찾았다")
    S.set_ini(text.encode(enc), enc, agent=1, background=1, advance=0, skip=0, mute=1, subtitles=0, uhd=1, uhd_mode=MODE,
              uhd_scale=SCALE,
              uhd_offscreen=1, uhd_trace=0, uhd_probe=0, uhd_text=1, uhd_battle=1, uhd_capture=CAP, uhd_envfix=0,
              uhd_capfrom=CAPFROM)  # 1: frames counted from the fight's start (khd4k): the runs line up only that way
    if WAY != "orig":
        shutil.copy2(PACK_SRC, PACK_DST)
        log(f"pack {PACK_SRC.name} -> {PACK_DST.name} sha256 {hashlib.sha256(PACK_DST.read_bytes()).hexdigest()[:8]}")
    log(f"way {WAY}, finisher {PICK}, {SECONDS:.0f}s, {MODE}{f' x{SCALE}' if MODE == 'window' else ''}, capture {CAP or '-'}")
    fg0 = user32.GetForegroundWindow()
    stop, events, rows = threading.Event(), [], []
    threading.Thread(target=watch_foreground, args=(fg0, stop, events), daemon=True).start()
    try:
        h = T3.launch()
        pid = W.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        log(f"game window {h:#x} pid {pid.value}")
        threading.Thread(target=sample_memory, args=(pid.value, stop, rows), daemon=True).start()
        time.sleep(3)
        UA.layout(h)
        time.sleep(8)
        UA.click(h, 400, 500); time.sleep(4)
        UA.click(h, 405, 416); time.sleep(4)
        UA.click(h, 400, 431); time.sleep(6)
        UA.click(h, *F.bar_center(PICK))
        t0 = time.time()
        while time.time() - t0 < SECONDS and S.running():
            time.sleep(0.5)
        time.sleep(3)
        UA.quit_game(h)
    finally:
        for _ in range(60):
            if not S.running():
                break
            time.sleep(0.5)
        stop.set()
        S.INI.write_bytes(raw)
        log("autobattle.ini restored")
        if S.running():
            log("game still running - pack and window position left as they are")
        else:
            realreg.set_dword("XPos", pos[0])
            realreg.set_dword("YPos", pos[1])
            log(f"window position restored to {pos}")
            if PACK_DST.exists():
                PACK_DST.unlink()
                log(f"{PACK_DST.name} removed from the copy")
        n = 0
        if CAPDIR.exists():
            for p in sorted(CAPDIR.glob("*.bmp")):
                shutil.move(str(p), str(OUT / p.name))
                n += 1
        log(f"{n} frames -> {OUT}")
        if UA.TRACE.exists():
            shutil.copy2(UA.TRACE, OUT / "khd4k_trace.txt")
        good = [r for r in rows if isinstance(r[0], float) and r[4] > 0]   # not the samples after the game closed
        with open(OUT / "mem.tsv", "w", encoding="utf-8") as fh:
            fh.write("sec\tprivate_MB\tworking_set_MB\tpeak_commit_MB\taddress_space_used_MB\tlargest_free_MB\n")
            for r in good:
                fh.write("\t".join([str(r[0])] + [mb(v) for v in r[1:]]) + "\n")
        if good:
            log(f"memory peaks: private {mb(max(r[1] for r in good))} MB, working set {mb(max(r[2] for r in good))} MB, "
                f"commit {mb(max(r[3] for r in good))} MB, address space used {mb(max(r[4] for r in good))} of 2048 MB, "
                f"smallest largest-free {mb(min(r[5] for r in good))} MB")
        else:
            log(f"memory: no samples {rows[:1]}")
        log(f"front window: {'; '.join(events) if events else 'the game never came to the front'}")
        (OUT / "run.log").write_text("\n".join(LINES) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
