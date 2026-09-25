/* res_extract — extrator dos containers RES (os .DAT da pasta BGA) do Prex3Recomp.
 *
 * Reimplementa RES_Open / RES_Read de src/resource.c (mesmo layout de diretório
 * de 28 bytes e mesmo XOR key=0xEF step=0x4F), sem depender de OpenGL.
 *
 * Uso:
 *   res_extract <arquivo.DAT>                lista as entradas
 *   res_extract <arquivo.DAT> -x [destino]   extrai tudo (default: <DAT>_extracted)
 *   res_extract <arquivo.DAT> -x nome.tga    extrai só essa entrada
 *   res_extract <arquivo.DAT> -a nome.tga    preview ASCII da faixa dos dígitos
 *
 * O modo -a é o que interessa para o bug do contador TIME: ele decodifica a TGA
 * exatamente como Texture_LoadTGA faz (inversão incondicional das linhas) e
 * imprime a região do atlas na ORDEM DE MEMÓRIA, que é a ordem em que
 * glTexImage2D mapeia V crescente. Se o dígito sair em pé no preview, V cresce
 * do topo para a base do glifo; se sair de cabeça para baixo, é o contrário —
 * e é isso que decide qual V vai em qual vértice do quad.
 *
 * Compilar:  cc -O2 -o res_extract tools/res_extract.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>

#define RES_KEY_INIT 0xEF
#define RES_KEY_STEP 0x4F

#pragma pack(push, 1)
typedef struct {
    char     name[16];
    uint32_t size;
    uint32_t offset;
    uint32_t checksum;
} RESEntry;                      /* 28 bytes, igual src/resource.c */
#pragma pack(pop)

static void res_xor_decrypt(uint8_t* data, uint32_t size) {
    uint8_t key = RES_KEY_INIT;
    for (uint32_t i = 0; i < size; i++) {
        data[i] ^= key;
        key = (uint8_t)(key + RES_KEY_STEP);
    }
}

static int ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ─── TGA ────────────────────────────────────────────────────────────────── */

/* Decodifica igual Texture_LoadTGA: suporta 24/32 bpp, raw (tipo 2) e RLE
 * (tipo 10), e inverte TODAS as linhas incondicionalmente no fim. Devolve RGBA. */
static uint8_t* tga_decode(const uint8_t* buf, uint32_t len, int* wOut, int* hOut,
                           int* descOut, int* typeOut, int* bppOut) {
    if (len < 18) return NULL;
    int width  = buf[12] | (buf[13] << 8);
    int height = buf[14] | (buf[15] << 8);
    int bpp    = buf[16];
    int type   = buf[2];
    int desc   = buf[17];
    if (width <= 0 || height <= 0 || (bpp != 24 && bpp != 32)) return NULL;

    int channels = bpp / 8;
    uint32_t dataSize = (uint32_t)width * height * channels;
    uint8_t* px = (uint8_t*)malloc(dataSize);
    if (!px) return NULL;
    memset(px, 0, dataSize);

    const uint8_t* p   = buf + 18 + buf[0];   /* pula o ID field */
    const uint8_t* end = buf + len;

    if (type == 10) {
        uint32_t o = 0;
        while (o < dataSize && p < end) {
            uint8_t hdr = *p++;
            int count = (hdr & 0x7F) + 1;
            if (hdr & 0x80) {
                if (p + channels > end) break;
                for (int i = 0; i < count && o < dataSize; i++)
                    for (int c = 0; c < channels && o < dataSize; c++)
                        px[o++] = p[c];
                p += channels;
            } else {
                int bytes = count * channels;
                if (p + bytes > end) bytes = (int)(end - p);
                memcpy(px + o, p, (size_t)bytes);
                o += (uint32_t)bytes;
                p += bytes;
            }
        }
    } else {
        uint32_t n = (uint32_t)(end - p);
        if (n > dataSize) n = dataSize;
        memcpy(px, p, n);
    }

    /* inversão incondicional das linhas — o mesmo que o engine faz */
    for (int y = 0; y < height / 2; y++) {
        uint8_t* a = px + (size_t)y * width * channels;
        uint8_t* b = px + (size_t)(height - 1 - y) * width * channels;
        for (int i = 0; i < width * channels; i++) {
            uint8_t t = a[i]; a[i] = b[i]; b[i] = t;
        }
    }

    uint8_t* rgba = (uint8_t*)malloc((size_t)width * height * 4);
    if (!rgba) { free(px); return NULL; }
    for (int i = 0; i < width * height; i++) {
        if (channels == 3) {
            rgba[i*4+0] = px[i*3+2];
            rgba[i*4+1] = px[i*3+1];
            rgba[i*4+2] = px[i*3+0];
            rgba[i*4+3] = 0xFF;
        } else {
            rgba[i*4+0] = px[i*4+2];
            rgba[i*4+1] = px[i*4+1];
            rgba[i*4+2] = px[i*4+0];
            rgba[i*4+3] = px[i*4+3];
        }
    }
    free(px);

    *wOut = width; *hOut = height;
    *descOut = desc; *typeOut = type; *bppOut = bpp;
    return rgba;
}

/* Faixa dos dígitos no atlas da fonte, conforme Font_DrawDigit (src/font.c):
 * v = row*31 + 73, altura 31, largura 32 por coluna. */
#define DIG_V0   73
#define DIG_H    31
#define DIG_W    32

static void ascii_preview(const uint8_t* rgba, int w, int h) {
    int y0 = DIG_V0 - 6;  if (y0 < 0) y0 = 0;
    int y1 = DIG_V0 + DIG_H * 2 + 6;  if (y1 > h) y1 = h;
    int x1 = DIG_W * 3;  if (x1 > w) x1 = w;

    printf("\nPreview em ORDEM DE MEMORIA (linha 0 = V 0.0, linha de baixo = V maior)\n");
    printf("colunas 0..%d (digitos 0,1,2), linhas %d..%d\n\n", x1 - 1, y0, y1 - 1);

    for (int y = y0; y < y1; y++) {
        printf("%3d |", y);
        for (int x = 0; x < x1; x++) {
            const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            int lum = (p[0] + p[1] + p[2]) / 3;
            int a   = p[3];
            int v   = lum * a / 255;
            putchar(v > 160 ? '#' : v > 80 ? '+' : v > 25 ? '.' : ' ');
        }
        printf("|\n");
        if (y == DIG_V0 - 1 || y == DIG_V0 + DIG_H - 1) {
            printf("    +");
            for (int x = 0; x < x1; x++) putchar('-');
            printf("+  <- borda da faixa (v=%d)\n", y + 1);
        }
    }
    printf("\n");
}

/* ─── main ───────────────────────────────────────────────────────────────── */

static int write_file(const char* dir, const char* name, const uint8_t* data, uint32_t size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "  !! nao consegui escrever '%s'\n", path); return 0; }
    fwrite(data, 1, size, f);
    fclose(f);
    printf("  -> %s (%u bytes)\n", path, size);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
            "uso: %s <arquivo.DAT> [-x [destino|nome]] [-a nome.tga]\n", argv[0]);
        return 1;
    }
    const char* datPath = argv[1];
    const char* mode = (argc > 2) ? argv[2] : NULL;
    const char* arg3 = (argc > 3) ? argv[3] : NULL;

    FILE* f = fopen(datPath, "rb");
    if (!f) { fprintf(stderr, "nao abriu '%s'\n", datPath); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc((size_t)fsize);
    if (!data || fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "leitura falhou\n"); return 1;
    }
    fclose(f);

    if (memcmp(data, "RES", 3) != 0) { fprintf(stderr, "magic invalido (nao e RES)\n"); return 1; }

    uint32_t fileCount = *(uint32_t*)(data + 8);
    uint32_t dirSize   = fileCount * (uint32_t)sizeof(RESEntry);
    if (0x18 + dirSize > (uint32_t)fsize) { fprintf(stderr, "diretorio fora do arquivo\n"); return 1; }

    RESEntry* entries = (RESEntry*)malloc(dirSize);
    memcpy(entries, data + 0x18, dirSize);
    res_xor_decrypt((uint8_t*)entries, dirSize);

    uint32_t h2        = 0x18 + dirSize;
    uint32_t seekRel   = *(uint32_t*)(data + h2 + 0x40);
    uint32_t dataStart = h2 + 0x44 + seekRel;
    for (uint32_t i = 0; i < fileCount; i++) entries[i].offset += dataStart;

    printf("RES '%s': %ld bytes, %u entradas (dataStart=0x%x)\n",
           datPath, fsize, fileCount, dataStart);

    int doExtract = mode && strcmp(mode, "-x") == 0;
    int doAscii   = mode && strcmp(mode, "-a") == 0;

    char outDir[1024];
    const char* onlyName = NULL;
    if (doExtract) {
        if (arg3 && strchr(arg3, '.')) onlyName = arg3;           /* -x nome.tga */
        if (arg3 && !onlyName) snprintf(outDir, sizeof(outDir), "%s", arg3);
        else snprintf(outDir, sizeof(outDir), "%s_extracted", datPath);
        if (!onlyName) mkdir(outDir, 0755);
        else snprintf(outDir, sizeof(outDir), ".");
        if (!onlyName) printf("destino: %s\n", outDir);
    }
    if (doAscii && !arg3) { fprintf(stderr, "-a precisa do nome da entrada\n"); return 1; }

    for (uint32_t i = 0; i < fileCount; i++) {
        RESEntry* e = &entries[i];
        char name[17];
        memcpy(name, e->name, 16); name[16] = '\0';

        if (!doExtract && !doAscii) {
            printf("[%2u] %-16s offset=0x%08x size=%u\n", i, name, e->offset, e->size);
            continue;
        }
        if (onlyName && !ieq(name, onlyName)) continue;
        if (doAscii   && !ieq(name, arg3))    continue;
        if (e->offset + e->size > (uint32_t)fsize) {
            fprintf(stderr, "[%2u] %s: fora do arquivo, pulando\n", i, name);
            continue;
        }

        uint8_t* buf = (uint8_t*)malloc(e->size);
        memcpy(buf, data + e->offset, e->size);
        res_xor_decrypt(buf, e->size);

        if (doExtract) write_file(onlyName ? "." : outDir, name, buf, e->size);

        size_t nl = strlen(name);
        int isTga = nl > 4 && ieq(name + nl - 4, ".tga");
        if (isTga && (doAscii || doExtract)) {
            int w, h, desc, type, bpp;
            uint8_t* rgba = tga_decode(buf, e->size, &w, &h, &desc, &type, &bpp);
            if (rgba) {
                int topOrigin = (desc & 0x20) != 0;
                printf("  %s: %dx%d %dbpp tipo=%d descriptor=0x%02x origem=%s\n",
                       name, w, h, bpp, type, desc,
                       topOrigin ? "TOPO-ESQUERDA" : "BASE-ESQUERDA (padrao TGA)");
                printf("  apos a inversao do Texture_LoadTGA, V=0 fica no %s da imagem\n",
                       topOrigin ? "BASE" : "TOPO");
                if (doAscii) ascii_preview(rgba, w, h);
                free(rgba);
            } else {
                printf("  %s: nao consegui decodificar como TGA\n", name);
            }
        }
        free(buf);
    }

    free(entries);
    free(data);
    return 0;
}
