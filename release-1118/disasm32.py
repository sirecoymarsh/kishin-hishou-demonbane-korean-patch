import struct, sys, capstone
p = sys.argv[1]; start = int(sys.argv[2], 16); n = int(sys.argv[3], 16)
b = open(p, "rb").read()
pe = struct.unpack_from("<I", b, 0x3c)[0]
nsec = struct.unpack_from("<H", b, pe + 6)[0]; opt = struct.unpack_from("<H", b, pe + 20)[0]
base = struct.unpack_from("<I", b, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ra = struct.unpack_from("<IIII", b, o + 8); secs.append((va, vs, ra, rs))
def va2off(v):
    for va, vs, ra, rs in secs:
        if base + va <= v < base + va + max(vs, rs): return v - base - va + ra
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
o = va2off(start)
for ins in md.disasm(b[o:o + n], start):
    print(f"{ins.address:08x}  {ins.mnemonic:6} {ins.op_str}")
