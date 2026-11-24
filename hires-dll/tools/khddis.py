"""정적 분석 도우미 — 기신비상 사본의 32비트 모듈(system.dll·demonbane.dll)을 읽어 가져오기 표·문자열·호출처를 찾고 주석 달린 디스어셈블을 낸다.

  python -I -X utf8 khddis.py <모듈> callers <dll!함수>           # 가져온 함수를 부르는 곳
  python -I -X utf8 khddis.py <모듈> xref <VA|RVA>                  # 그 주소를 부르거나(call/jmp) 상수로 쓰는 곳
  python -I -X utf8 khddis.py <모듈> dis <RVA> [바이트=0x200]       # 주석 달린 디스어셈블
  python -I -X utf8 khddis.py <모듈> func <RVA>                     # RVA 를 품은 함수의 시작(앞쪽 int3/정렬 경계로 추정)부터 ret 까지
  python -I -X utf8 khddis.py <모듈> str <정규식>                   # 문자열(ascii/cp932) 찾기와 그 문자열을 쓰는 곳

<모듈>은 game 폴더 기준 파일 이름. 출력 주소는 RVA(모듈 기준). 2026-10-15 Claude(4K 작업).
"""
from __future__ import annotations

import re
import struct
import sys
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, r"P:\AI\python\libs")
import capstone  # noqa: E402

GAME = Path(r"P:\AI\khd-runtime-fix\game")


class Module:
    def __init__(self, name: str):
        self.name = name
        self.b = (GAME / name).read_bytes()
        b = self.b
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        nsec = struct.unpack_from("<H", b, pe + 6)[0]
        optsz = struct.unpack_from("<H", b, pe + 20)[0]
        self.base = struct.unpack_from("<I", b, pe + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + optsz + 40 * i
            nm = b[o:o + 8].rstrip(b"\0").decode()
            vsz, va, rsz, raw = struct.unpack_from("<IIII", b, o + 8)
            self.secs.append((nm, va, max(vsz, rsz), raw, rsz))
        self.iat = {}                       # VA of slot -> "dll!func"
        imp = struct.unpack_from("<I", b, pe + 24 + 96 + 8)[0]
        o = self.r2o(imp)
        while True:
            oft, _, _, nm, ft = struct.unpack_from("<IIIII", b, o)
            if not nm:
                break
            dll = self.cstr(self.base + nm)
            t, k = oft or ft, 0
            while True:
                e = struct.unpack_from("<I", b, self.r2o(t) + 4 * k)[0]
                if not e:
                    break
                fn = self.cstr(self.base + e + 2) if not e & 0x80000000 else f"#{e & 0xffff}"
                self.iat[self.base + ft + 4 * k] = f"{dll}!{fn}"
                k += 1
            o += 20
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.skipdata = True

    def r2o(self, rva):
        for _n, va, sz, raw, rsz in self.secs:
            if va <= rva < va + sz:
                return rva - va + raw if rva - va < rsz else None
        return None

    def v2o(self, v):
        return self.r2o(v - self.base)

    def cstr(self, v, enc="latin1", limit=200):
        o = self.v2o(v)
        if o is None:
            return None
        e = self.b.find(b"\0", o, o + limit)
        if e < 0:
            return None
        return self.b[o:e].decode(enc, "replace")

    def text_section(self):
        return next(s for s in self.secs if s[0] == ".text")

    @lru_cache(maxsize=None)
    def all_ins(self):
        _n, va, sz, raw, rsz = self.text_section()
        return list(self.md.disasm(self.b[raw:raw + rsz], self.base + va))

    def note(self, ins) -> str:
        out = []
        for tok in re.findall(r"0x[0-9a-f]+", ins.op_str):
            v = int(tok, 16)
            if v in self.iat:
                out.append(self.iat[v])
            elif self.base <= v < self.base + 0x200000:
                o = self.v2o(v)
                if o is not None and not self.in_text(v):
                    s = self.cstr(v, "cp932", 80)
                    if s and len(s) >= 3 and all(c.isprintable() for c in s):
                        out.append(repr(s))
        return ("  ; " + " | ".join(out)) if out else ""

    def in_text(self, v):
        _n, va, sz, raw, rsz = self.text_section()
        return self.base + va <= v < self.base + va + sz

    def dis(self, rva, n=0x200):
        o = self.r2o(rva)
        lines = []
        for ins in self.md.disasm(self.b[o:o + n], self.base + rva):
            lines.append(f"{ins.address - self.base:#08x}: {ins.mnemonic:7s} {ins.op_str}{self.note(ins)}")
        return lines

    def func_start(self, rva):
        """앞으로 거슬러 올라가 int3/nop 채움(0xCC 0x90) 뒤의 첫 바이트를 함수 시작으로 본다."""
        o = self.r2o(rva)
        while o > 0:
            if self.b[o - 1] in (0xCC, 0x90) and self.b[o - 2] in (0xCC, 0x90, 0xC3, 0xC2):
                break
            o -= 1
        _n, va, sz, raw, rsz = self.text_section()
        return o - raw + va

    def callers_of_slot(self, slot_va):
        hits = []
        for ins in self.all_ins():
            if f"{slot_va:#x}" in ins.op_str and ins.mnemonic in ("call", "jmp", "mov"):
                hits.append(ins)
        return hits

    def xref(self, v):
        hits = []
        s = f"{v:#x}"
        for ins in self.all_ins():
            if re.search(re.escape(s) + r"\b", ins.op_str):
                hits.append(ins)
        return hits


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    m = Module(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "callers":
        want = sys.argv[3].lower()
        slots = [v for v, n in m.iat.items() if n.lower() == want or n.lower().endswith("!" + want)]
        for s in slots:
            print(f"slot {s:#x} = {m.iat[s]}")
            for ins in m.callers_of_slot(s):
                f = m.func_start(ins.address - m.base)
                print(f"  {ins.address - m.base:#08x} (func {f:#08x}): {ins.mnemonic} {ins.op_str}")
    elif cmd == "xref":
        v = int(sys.argv[3], 16)
        if v < m.base:
            v += m.base
        for ins in m.xref(v):
            f = m.func_start(ins.address - m.base)
            print(f"  {ins.address - m.base:#08x} (func {f:#08x}): {ins.mnemonic} {ins.op_str}")
    elif cmd == "dis":
        rva = int(sys.argv[3], 16)
        n = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x200
        print("\n".join(m.dis(rva, n)))
    elif cmd == "func":
        rva = int(sys.argv[3], 16)
        f = m.func_start(rva)
        lines = m.dis(f, int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x400)
        print(f"; function guessed at {f:#08x}")
        print("\n".join(lines))
    elif cmd == "str":
        pat = re.compile(sys.argv[3].encode("cp932"))
        for mt in pat.finditer(m.b):
            o = mt.start()
            s0 = m.b.rfind(b"\0", 0, o) + 1
            for _n, va, sz, raw, rsz in m.secs:
                if raw <= s0 < raw + rsz:
                    v = m.base + va + s0 - raw
                    txt = m.cstr(v, "cp932")
                    refs = [f"{i.address - m.base:#x}" for i in m.xref(v)]
                    print(f"{v - m.base:#08x}: {txt!r}  used at {refs}")
                    break


if __name__ == "__main__":
    main()
