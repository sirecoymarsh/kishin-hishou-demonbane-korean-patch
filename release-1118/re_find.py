import pefile, struct, re, sys
p = pefile.PE(r"F:\AI\khd-latest-game\system.dll"); b = open(r"F:\AI\khd-latest-game\system.dll", "rb").read()
t = p.sections[0]; data = t.get_data(); base = 0x10000000 + t.VirtualAddress
for w in sys.argv[1:]:
    wb = w.encode("cp932")
    for m in re.finditer(re.escape(wb), b):
        va = 0x10000000 + p.get_rva_from_offset(m.start())
        refs = [hex(base + r.start() - 1) for r in re.finditer(re.escape(struct.pack("<I", va)), data)]
        print(w, hex(va), b[m.start() - 2:m.start() + 18], refs[:6])
