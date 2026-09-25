import pefile, sys, pickle
from capstone import *
pe = pefile.PE('pumpy_unpacked.exe'); base = pe.OPTIONAL_HEADER.ImageBase
strs = pickle.load(open('strs.pkl','rb'))
imp = {i.address:(i.name or b'?').decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports}
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
tva = base+text.VirtualAddress; data = text.get_data()
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
a = int(sys.argv[1],16); n = int(sys.argv[2],0)
import re, struct
def rdata(v):
    for sec in pe.sections:
        a=base+sec.VirtualAddress
        if sec.Name.startswith(b'.rdata') and a<=v<a+sec.SizeOfRawData:
            d=sec.get_data()[v-a:v-a+8]
            return d
    return None
for i in md.disasm(data[a-tva:a-tva+n], a):
    note = ''
    for m in re.finditer(r'0x[0-9a-f]{6,8}', i.op_str):
        v = int(m.group(),16)
        if v in strs: note = '  ; "%s"' % strs[v]
        elif v in imp: note = '  ; %s' % imp[v]
        else:
            d = rdata(v)
            if d and ('fld' in i.mnemonic or 'fmul' in i.mnemonic or 'fadd' in i.mnemonic or 'fsub' in i.mnemonic or 'fdiv' in i.mnemonic or 'fcom' in i.mnemonic or 'fild' in i.mnemonic):
                if 'qword' in i.op_str: note = '  ; f64=%g' % struct.unpack('<d', d)[0]
                else: note = '  ; f32=%g' % struct.unpack('<f', d[:4])[0]
    print("%08x  %-6s %s%s" % (i.address, i.mnemonic, i.op_str, note))
