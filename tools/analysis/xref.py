import re, glob, pickle, bisect, collections
starts, tva, tsz = pickle.load(open('starts.pkl','rb'))
S = set(starts)
refs = collections.OrderedDict()
for f in sorted(glob.glob('/home/silver/Documentos/Prex3Recomp/src/*.c')+glob.glob('/home/silver/Documentos/Prex3Recomp/include/*.h')):
    for n,l in enumerate(open(f, errors='ignore'),1):
        for m in re.finditer(r'0x00([4-5][0-9a-fA-F]{5})', l):
            a = int(m.group(1),16)
            refs.setdefault(a, []).append((f.split('/')[-1], n, l.strip()[:90]))
inside = [a for a in refs if tva <= a < tva+tsz]
exact = [a for a in inside if a in S]
print("enderecos distintos citados: %d | dentro do .text: %d | em inicio de funcao: %d" % (len(refs), len(inside), len(exact)))
miss = [a for a in inside if a not in S]
print("\nDentro do .text mas NAO inicio de funcao (%d):" % len(miss))
for a in miss[:20]:
    k = bisect.bisect_right(starts, a)-1
    print("  %#x  (dentro da func %#x+%#x)  %s:%d" % (a, starts[k], a-starts[k], refs[a][0][0], refs[a][0][1]))
print("\nFora do .text (dados/rdata):", len([a for a in refs if a not in inside]))
files = collections.Counter(r[0] for a in exact for r in refs[a][:1])
print("\nFuncoes confirmadas por arquivo nosso:", dict(files))
pickle.dump(refs, open('refs.pkl','wb'))
