import pefile, re, pickle, collections
from capstone import *
pe = pefile.PE('pumpy_unpacked.exe'); base = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
tva = base + text.VirtualAddress; tsz = text.Misc_VirtualSize
data = text.get_data()[:tsz]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True; md.skipdata = True
# imports map: IAT addr -> name
imp = {}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports:
        imp[i.address] = (d.dll.decode().lower(), (i.name or b'ord%d'%i.ordinal).decode())
# linear disassembly of whole .text (compiler code, mostly contiguous)
ins = [i for i in md.disasm(data, tva) if i.mnemonic != '.byte']
print("instrucoes:", len(ins))
calls = collections.Counter(); starts = set()
for i in ins:
    if i.mnemonic == 'call' and i.op_str.startswith('0x'):
        t = int(i.op_str, 16)
        if tva <= t < tva+tsz: calls[t] += 1
# prologos
for k, i in enumerate(ins):
    if i.mnemonic == 'push' and i.op_str == 'ebp' and k+1 < len(ins) and ins[k+1].mnemonic == 'mov' and ins[k+1].op_str == 'ebp, esp':
        starts.add(i.address)
starts |= set(calls)
starts = sorted(starts)
print("funcoes (prologo + alvos de call):", len(starts))
pickle.dump((starts, tva, tsz), open('starts.pkl','wb'))
# tamanhos
sizes = {a:(starts[k+1]-a if k+1 < len(starts) else tva+tsz-a) for k,a in enumerate(starts)}
big = sorted(sizes.items(), key=lambda x:-x[1])[:25]
print("\nMaiores funcoes:")
for a,s in big: print("  %#x  %5d bytes  chamadas_recebidas=%d" % (a, s, calls.get(a,0)))
