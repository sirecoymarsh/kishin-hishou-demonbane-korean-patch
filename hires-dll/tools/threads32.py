"""읽기 전용(잠깐 멈춤만): 실행 중인 32비트 게임 프로세스의 스레드마다 지금 위치(EIP)와 스택의 돌아갈 주소 후보를 적는다.
멈춘(로딩이 안 끝나는) 게임이 무엇을 기다리는지 보려는 것. 스레드는 읽는 동안만 아주 잠깐 멈췄다가 바로 다시 돈다.

  python -I -X utf8 threads32.py <pid> [스택 깊이 dword=1024] [--all]

모듈 파일의 코드로 「call 바로 뒤」인지 따진다(khd-runtime-fix\\tools\\dumpstack.py 의 is_return_address 그대로).
--all 이 없으면 ntdll/win32u 안에서만 맴도는 줄은 줄여 적는다.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as W
import os
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, r"P:\AI\python\libs")              # capstone, pefile
sys.path.insert(0, r"P:\AI\khd-runtime-fix\tools")
src = open(r"P:\AI\khd-runtime-fix\tools\dumpstack.py", encoding="utf-8").read().split("\ndef read_dump(")[0]
ds: dict = {}
exec(compile(src, "dumpstack.py", "exec"), ds)

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll")
k32.OpenProcess.restype = W.HANDLE
k32.OpenThread.restype = W.HANDLE
k32.CreateToolhelp32Snapshot.restype = W.HANDLE


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ThreadID", W.DWORD), ("th32OwnerProcessID", W.DWORD),
                ("tpBasePri", W.LONG), ("tpDeltaPri", W.LONG), ("dwFlags", W.DWORD)]


class MODULEINFO(ctypes.Structure):          # pointer-sized fields: 24 bytes in this 64-bit Python
    _fields_ = [("base", ctypes.c_void_p), ("size", W.DWORD), ("entry", ctypes.c_void_p)]


def modules(hp):
    arr = (ctypes.c_void_p * 1024)()
    need = W.DWORD()
    psapi.EnumProcessModulesEx(hp, arr, ctypes.sizeof(arr), ctypes.byref(need), 0x01)   # LIST_MODULES_32BIT
    out = []
    for m in arr[: need.value // ctypes.sizeof(ctypes.c_void_p)]:
        if not m:
            continue
        info = MODULEINFO()
        psapi.GetModuleInformation(hp, ctypes.c_void_p(m), ctypes.byref(info), ctypes.sizeof(info))
        path = ctypes.create_unicode_buffer(520)
        psapi.GetModuleFileNameExW(hp, ctypes.c_void_p(m), path, 520)
        out.append((info.base or 0, info.size, path.value))
    return sorted(out)


def where(mods, a):
    for base, size, path in mods:
        if base <= a < base + size:
            return base, path
    return None, None


def label(mods, a):
    base, path = where(mods, a)
    return f"{os.path.basename(path)}+{a - base:#x}" if base is not None else f"{a:#010x}"


def main():
    pid = int(sys.argv[1])
    depth = int(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 1024
    show_all = "--all" in sys.argv
    hp = k32.OpenProcess(0x0410 | 0x0008, False, pid)     # QUERY_INFORMATION | VM_READ | VM_OPERATION
    if not hp:
        sys.exit(f"cannot open {pid}")
    mods = modules(hp)
    snap = k32.CreateToolhelp32Snapshot(0x4, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    tids = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            tids.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    print(f"pid {pid}: {len(tids)} threads, {len(mods)} modules")
    for tid in tids:
        ht = k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, tid)
        if not ht:
            print(f"tid {tid}: cannot open")
            continue
        start = ctypes.c_uint64()
        ntdll.NtQueryInformationThread(ht, 9, ctypes.byref(start), 8, None)    # Win32StartAddress
        ctx = (ctypes.c_ubyte * 716)()
        struct.pack_into("<I", ctx, 0, 0x10001 | 0x10002)                     # WOW64_CONTEXT_CONTROL | INTEGER
        k32.Wow64SuspendThread(ht)
        got = k32.Wow64GetThreadContext(ht, ctypes.byref(ctx))
        k32.ResumeThread(ht)
        k32.CloseHandle(ht)
        if not got:
            print(f"tid {tid}: no context (error {ctypes.get_last_error()})")
            continue
        eip, esp, ebp = struct.unpack_from("<I", ctx, 184)[0], struct.unpack_from("<I", ctx, 196)[0], struct.unpack_from("<I", ctx, 180)[0]
        buf = (ctypes.c_ubyte * (depth * 4))()
        n = ctypes.c_size_t()
        k32.ReadProcessMemory(hp, ctypes.c_void_p(esp), buf, depth * 4, ctypes.byref(n))
        rets = []
        for i in range(n.value // 4):
            v = struct.unpack_from("<I", buf, i * 4)[0]
            base, path = where(mods, v)
            if base is None:
                continue
            if ds["is_return_address"](path, v - base):
                rets.append(label(mods, v))
        names = [r.split("+")[0].lower() for r in rets]
        boring = all(n in ("ntdll.dll", "win32u.dll", "kernelbase.dll", "kernel32.dll", "wow64win.dll") for n in names[:6])
        start_label = label(mods, start.value & 0xFFFFFFFF) if start.value else "?"
        print(f"tid {tid}: start {start_label}  eip {label(mods, eip)}")
        shown = rets if (show_all or not boring) else rets[:3]
        for r in shown[:24]:
            print(f"    {r}")
    k32.CloseHandle(hp)


if __name__ == "__main__":
    main()
