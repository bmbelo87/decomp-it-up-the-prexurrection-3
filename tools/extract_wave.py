#!/usr/bin/env python3
"""Extrai os 35 efeitos sonoros (recursos WAVE) do PUMPY.EXE original para a pasta WAVE/.

O original guarda os sons dentro do .exe; o port os le de WAVE/<nome>.wav.
O PUMPY.EXE distribuido e' compactado com UPX: descompacte uma COPIA antes
(upx -d -o PUMPY_unpacked.exe PUMPY.EXE) e passe a copia aqui.

  python3 tools/extract_wave.py PUMPY_unpacked.exe "/caminho/do/jogo/WAVE"

Nomes: os ids dos recursos no PE sao 148..182, na mesma ordem (alfabetica) da lista de
src/resources.rc (ids 100..134) -> deslocamento fixo de 48.
Requer: pip install pefile
"""
import os, re, sys
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
RC = os.path.join(HERE, "..", "src", "resources.rc")

def names_from_rc():
    out = []
    for line in open(RC, encoding="utf-8", errors="ignore"):
        m = re.match(r'IDR_WAVE_\w+\s+WAVE\s+"[^"]*\\\\([^\\"]+\.wav)"', line)
        if m:
            out.append(m.group(1))
    return out

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exe, dest = sys.argv[1], sys.argv[2]
    names = names_from_rc()
    pe = pefile.PE(exe)
    pe.parse_data_directories()
    waves = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.name and t.name.decode() == "WAVE":
            for r in t.directory.entries:
                de = r.directory.entries[0].data.struct
                waves[r.struct.Id] = pe.get_data(de.OffsetToData, de.Size)
    ids = sorted(waves)
    if len(ids) != len(names):
        sys.exit("esperava %d recursos WAVE, achei %d (o exe esta descompactado?)" % (len(names), len(ids)))
    os.makedirs(dest, exist_ok=True)
    for rid, name in zip(ids, names):
        with open(os.path.join(dest, name), "wb") as f:
            f.write(waves[rid])
        print("%3d -> %s (%d bytes)" % (rid, name, len(waves[rid])))

if __name__ == "__main__":
    main()
