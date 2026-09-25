import pefile, pickle, bisect, re, sys, collections
from capstone import *
pe = pefile.PE('pumpy_unpacked.exe'); base = pe.OPTIONAL_HEADER.ImageBase
starts, tva, tsz = pickle.load(open('starts.pkl','rb'))
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
lo, hi = int(sys.argv[1],16), int(sys.argv[2],16)
uses = collections.defaultdict(list)
for i in md.disasm(text.get_data()[:tsz], tva):
    if i.mnemonic == '.byte': continue
    for m in re.finditer(r'0x[0-9a-f]{6,8}', i.op_str):
        a = int(m.group(),16)
        if lo <= a <= hi:
            uses[starts[bisect.bisect_right(starts, i.address)-1]].append((i.address, a, i.mnemonic, i.op_str))
for f, l in sorted(uses.items()):
    print("func %#x: %d refs  ex: %s" % (f, len(l), ["%x %s %s"%(x[0],x[2],x[3]) for x in l[:2]]))
