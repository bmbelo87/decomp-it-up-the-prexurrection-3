import pefile, re, sys, pickle, bisect
from capstone import *
pe = pefile.PE('pumpy_unpacked.exe'); base = pe.OPTIONAL_HEADER.ImageBase
starts, tva, tsz = pickle.load(open('starts.pkl','rb'))
def va_of(sec, off): return base + sec.VirtualAddress + off
strs = {}
for s in pe.sections:
    if s.Name.rstrip(b'\0').decode() in ('.rdata','.data'):
        d = s.get_data()[:min(s.SizeOfRawData, s.Misc_VirtualSize)]
        for m in re.finditer(rb'[\x20-\x7e]{4,}\x00', d):
            strs[va_of(s, m.start())] = m.group()[:-1].decode()
pickle.dump(strs, open('strs.pkl','wb'))
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
ins = [i for i in md.disasm(text.get_data()[:tsz], tva) if i.mnemonic != '.byte']
# xref: qualquer operando imediato/absoluto apontando para uma string
xr = {}
for i in ins:
    for m in re.finditer(r'0x[0-9a-f]{6,8}', i.op_str):
        a = int(m.group(), 16)
        if a in strs:
            k = bisect.bisect_right(starts, i.address)-1
            xr.setdefault(a, []).append((i.address, starts[k]))
pickle.dump(xr, open('xr.pkl','wb'))
for pat in sys.argv[1:]:
    for a,t in strs.items():
        if re.search(pat, t, re.I):
            print("%#x %r -> refs em %s" % (a, t, [("%#x (func %#x)"%x) for x in xr.get(a,[])][:4]))
