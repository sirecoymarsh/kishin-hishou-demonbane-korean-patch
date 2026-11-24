"""켜 있는 기신비상의 demonbane.dll 묶음 목록(전역 배열 +0xfd434, 개수 +0xfd43c, 한 칸 0x10c)을 읽기만 한다"""
import ctypes, ctypes.wintypes as W, re, sys
import psutil
k = ctypes.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.restype = W.HANDLE
pid = next(p.pid for p in psutil.process_iter(["name"]) if (p.info["name"] or "").lower() == "kishinhisho_chs.exe")
h = k.OpenProcess(0x0410, False, pid)   # QUERY_INFORMATION | VM_READ
psapi = ctypes.WinDLL("psapi")
mods = (W.HMODULE * 1024)(); need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
base = None
import time
for _try in range(1800):
    psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
    for m in mods[: need.value // ctypes.sizeof(W.HMODULE)]:
        nm = ctypes.create_unicode_buffer(260); psapi.GetModuleBaseNameW(h, W.HMODULE(m), nm, 260)
        if nm.value.lower() == "demonbane.dll": base = m
    if base: break
    time.sleep(1)
time.sleep(8)   # 묶음을 다 붙일 때까지
for m in [][: need.value // ctypes.sizeof(W.HMODULE)]:
    nm = ctypes.create_unicode_buffer(260); psapi.GetModuleBaseNameW(h, W.HMODULE(m), nm, 260)
    if nm.value.lower() == "demonbane.dll": base = m
print("pid", pid, "demonbane.dll", hex(base) if base else None)
def rd(addr, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)): return None
    return buf.raw[: got.value]
arr = int.from_bytes(rd(base + 0xfd434, 4), "little"); cnt = int.from_bytes(rd(base + 0xfd43c, 4), "little")
print("packs", cnt, "array", hex(arr))
for i in range(cnt):
    rec = rd(arr + i * 0x10c, 0x10c)
    ents, n = int.from_bytes(rec[0:4], "little"), int.from_bytes(rec[4:8], "little")
    names = re.findall(rb"[\x20-\x7e]{4,}", rec[8:])
    first = rd(ents, 0x80) if ents else b""
    fn = re.findall(rb"[\x20-\x7e]{4,}", first or b"")
    print(f"{i:2} entries {n:5} {[x.decode() for x in names][:2]} first={fn[:1]}")
