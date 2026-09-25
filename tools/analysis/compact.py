import re, sys
L = open(sys.argv[1]).read().split('\n')
out = []; i = 0
pat = re.compile(r'^([0-9a-f]{8})  (mov|cmp|ja|test|jne|jae)\s')
while i < len(L):
    # bloco: mov dl,[eax+N]; cmp dl,0x80; ja X; test dl,dl; jne Y   (repetido)
    if i+4 < len(L) and re.search(r'mov +(dl|al), byte ptr \[(eax|edx)( \+ (\d))?\]', L[i]) and 'cmp' in L[i+1] and '0x80' in L[i+1] and re.search(r'\bja\b', L[i+2]) and 'test' in L[i+3] and 'jne' in L[i+4]:
        j = i; n = 0
        while j+4 < len(L) and re.search(r'mov +(dl|al), byte ptr \[(eax|edx)', L[j]) and 'cmp' in L[j+1] and '0x80' in L[j+1] and re.search(r'\bja\b', L[j+2]) and 'test' in L[j+3] and 'jne' in L[j+4]:
            tgt = L[j+4].split()[-1]; j += 5; n += 1
        out.append("%s  <<ROW_CHECK x%d: se alguma nota da linha ainda nao consumida (1..0x7f) -> %s>>" % (L[i].split()[0], n, tgt)); i = j; continue
    out.append(L[i]); i += 1
open(sys.argv[2],'w').write('\n'.join(out))
print(len(L), '->', len(out))
