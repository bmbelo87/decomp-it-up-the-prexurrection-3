import pefile, struct, re
from capstone import *
pe = pefile.PE('pumpy_unpacked.exe'); base = pe.OPTIONAL_HEADER.ImageBase
def rd(va, n):
    for s in pe.sections:
        a = base + s.VirtualAddress
        if a <= va < a + s.SizeOfRawData: return s.get_data()[va-a:va-a+n]
md = Cs(CS_ARCH_X86, CS_MODE_32)
def handler_vars(addr):
    """le o handler ate o jmp final e devolve variaveis 0x144bb../0x144bc.. escritas"""
    code = rd(addr, 80); w = []
    for i in md.disasm(code, addr):
        m = re.match(r'(mov|or) dword ptr \[(0x144b[b-c][0-9a-f]{2})\], (\w+)', i.mnemonic+' '+i.op_str)
        if m: w.append(m.group(2))
        if i.mnemonic == 'jmp': break
    return w
VKN = {0x0c:'CLEAR(Num5 NumLock off)',0x21:'PGUP',0x22:'PGDN',0x23:'END',0x24:'HOME',0x25:'LEFT',0x26:'UP',0x27:'RIGHT',0x28:'DOWN',
       0x0d:'ENTER',0x1b:'ESC',0x20:'SPACE',0x2d:'INSERT',0x2e:'DELETE'}
def name(vk):
    if vk in VKN: return VKN[vk]
    if 0x30<=vk<=0x39 or 0x41<=vk<=0x5a: return chr(vk)
    return hex(vk)
for label, btab, dtab in (("KEYDOWN", 0x41a0e8, 0x41a0a4), ("KEYUP", 0x41a164, 0x41a138)):
    print("==", label)
    idx = rd(btab, 0x4f); nt = max(idx)+1
    targets = struct.unpack('<%dI' % nt, rd(dtab, 4*nt))
    default = None
    for vk in range(0x0c, 0x0c+0x4f):
        t = targets[idx[vk-0x0c]]
        if t == 0x41a08a: continue
        print("  VK %#04x %-22s -> %#x  vars=%s" % (vk, name(vk), t, handler_vars(t)))
