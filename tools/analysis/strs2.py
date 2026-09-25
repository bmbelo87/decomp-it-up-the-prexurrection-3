import pefile, re, glob, collections
pe = pefile.PE('pumpy_unpacked.exe')
raw = []
for s in pe.sections:
    if s.Name.rstrip(b'\0').decode() in ('.rdata','.data'):
        d = s.get_data()[:min(s.SizeOfRawData, s.Misc_VirtualSize)]
        raw += [m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{4,}', d)]
ours = ''.join(open(f, errors='ignore').read() for f in glob.glob('/home/silver/Documentos/Prex3Recomp/src/*.[ch]')+glob.glob('/home/silver/Documentos/Prex3Recomp/include/*.h')).lower().replace('\\\\','/')
noise = re.compile(r'libpng|png|zlib|inflate|deflate|heap|runtime|R6\d\d\d|stack|floating|_onexit|lowio|stdio|thread|console device|virtual function|Buffer|chunk|IHDR|gAMA|cHRM|sRGB|iCCP|1#|error|invalid|unknown|too |bad |Unsupported|Ignoring|Setting gamma|Application', re.I)
game = []
for t in sorted(set(raw)):
    n = t.replace('\\','/').lower().strip()
    if len(n) < 5 or noise.search(t) or not re.search(r'[A-Za-z]{3}', t): continue
    if n in ours: continue
    if not re.fullmatch(r"[A-Za-z0-9_ .,:%/\-!'\[\]()#+*=]{5,70}", t): continue
    game.append(t)
# agrupa TGA/SPR/DAT numerados
groups = collections.defaultdict(list); rest = []
for t in game:
    m = re.fullmatch(r'(.*?)(\d+)(\w*\.(?:TGA|SPR|DAT|AUD|SP2|WAV))', t.replace('\\','/'), re.I)
    (groups[(m.group(1), m.group(3))] if m else rest).append(t) if False else None
    if m: groups[(m.group(1), m.group(3))].append(t)
    else: rest.append(t)
print("Strings do jogo ausentes no nosso codigo:", len(game))
print("\nArquivos numerados (padrao -> quantidade):")
for (a,b),v in sorted(groups.items(), key=lambda x:-len(x[1]))[:25]: print("  %-22s x%d  ex: %s" % (a+'N'+b, len(v), v[0]))
print("\nOutras (texto/mensagens/arquivos unicos):")
import pefile as _p; _imp={i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
for t in [t for t in rest if t not in _imp and not t.endswith(("A","W","32.dll","32.DLL")) and not re.fullmatch(r".{0,7}",t)][40:200]: print("  ", repr(t))
print("total outras:", len(rest))
