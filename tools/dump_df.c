#include "df_resource.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: dump_df <file.DAT> [output_dir]\n");
        printf("  Extracts DF/SD format archives\n");
        return 1;
    }

    if (!DF_Open(argv[1])) {
        printf("Failed to open: %s\n", argv[1]);
        return 1;
    }

    const char* outDir = argc >= 3 ? argv[2] : NULL;

    printf("Archive: %s\n", argv[1]);
    printf("  Entries: %d\n", DF_GetCount());

    for (int i = 0; i < DF_GetCount(); i++) {
        uint32_t sz = DF_GetSize(i);
        const char* name = DF_GetName(i);
        if (!name) name = "(null)";
        printf("  [%3d] %-28s sz=0x%08X\n", i, name, sz);

        if (outDir) {
            uint8_t* data = DF_ReadAlloc(i);
            if (data) {
                char outPath[512];
                snprintf(outPath, sizeof(outPath), "%s/%s", outDir, name);
                FILE* f = fopen(outPath, "wb");
                if (f) {
                    fwrite(data, 1, sz, f);
                    fclose(f);
                }
                free(data);
            }
        }
    }

    DF_Close();
    return 0;
}
