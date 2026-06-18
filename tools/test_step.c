#include "step.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: test_step <STX file path>\n");
        return 1;
    }

    for (int ai = 1; ai < argc; ai++) {
        const char* stxPath = argv[ai];

        StepSong song;
        if (!Step_LoadSong(stxPath, &song))
        {
            printf("%s: FAILED to load\n", stxPath);
            continue;
        }

        const char* shortName = strrchr(stxPath, '/');
        if (!shortName) shortName = strrchr(stxPath, '\\');
        if (!shortName) shortName = stxPath - 1;
        shortName++;

        printf("%s: \"%s\" (%d charts)\n", shortName, song.title, song.chartCount);
        for (int ci = 0; ci < song.chartCount; ci++)
        {
            StepChart* c = &song.charts[ci];
            int noteCount = 0;
            for (uint32_t ri = 0; ri < c->rowCount; ri++)
            {
                StepRow* r = &c->rows[ri];
                if (r->half1.dl || r->half1.ul || r->half1.cn || r->half1.ur || r->half1.dr ||
                    r->half2.dl || r->half2.ul || r->half2.cn || r->half2.ur || r->half2.dr)
                    noteCount++;
            }
            printf("  Chart %d: BPM=%.1f bpm=%d beatSplit=%d rows=%d panels=%d nonEmpty=%d\n",
                ci, c->bpm, c->beatPerMeasure, c->beatSplit, c->rowCount, c->panelCount, noteCount);
        }
        Step_FreeSong(&song);
    }

    return 0;
}
