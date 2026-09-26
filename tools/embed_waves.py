#!/usr/bin/env python3
"""Gera um .c com os WAVs da pasta WAVE/ embutidos como arrays de bytes.

O PUMPY.EXE original guarda os WAVs como recursos Win32 dentro do executavel.
O port faz o equivalente de forma portavel: cada arquivo vira um array C e uma
tabela g_embeddedWaves[] liga o nome do arquivo aos bytes.

Uso: embed_waves.py <pasta WAVE> <saida.c>
"""
import os
import sys


def c_ident(name):
    return "wave_" + "".join(c if c.isalnum() else "_" for c in name)


def main():
    if len(sys.argv) != 3:
        print("uso: embed_waves.py <pasta WAVE> <saida.c>", file=sys.stderr)
        return 1
    src_dir, out_path = sys.argv[1], sys.argv[2]
    if not os.path.isdir(src_dir):
        print("embed_waves: pasta nao encontrada: %s" % src_dir, file=sys.stderr)
        return 1

    names = sorted(f for f in os.listdir(src_dir) if f.lower().endswith(".wav"))
    total = 0
    lines = ["/* Gerado por tools/embed_waves.py - nao editar. */",
             "#include <stdint.h>", "#include <stddef.h>", ""]
    for name in names:
        with open(os.path.join(src_dir, name), "rb") as f:
            data = f.read()
        total += len(data)
        lines.append("static const uint8_t %s[%d] = {" % (c_ident(name), len(data)))
        for i in range(0, len(data), 16):
            lines.append("    " + ",".join(str(b) for b in data[i:i + 16]) + ",")
        lines.append("};")
        lines.append("")

    lines.append("typedef struct { const char* name; const uint8_t* data; uint32_t size; } EmbeddedWave;")
    lines.append("const EmbeddedWave g_embeddedWaves[] = {")
    for name in names:
        lines.append('    { "%s", %s, (uint32_t)sizeof(%s) },' % (name, c_ident(name), c_ident(name)))
    lines.append("    { NULL, NULL, 0 }")
    lines.append("};")
    lines.append("")

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(lines))
    print("embed_waves: %d arquivos, %d bytes -> %s" % (len(names), total, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
