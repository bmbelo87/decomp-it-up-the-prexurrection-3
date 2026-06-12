#include "step.h"
#include <stdio.h>

int main(void)
{
    const char* testSongs[] = {"101","102","104","108","109","112","202","203","204","205","212",
        "301","302","303","305","306","310","311","312","318",
        "401","402","403","404","405","413","414",
        "501","503","504",
        "701","703","704","705","711","712","714","719","721","730","735","736",
        "801","802","803","804","805","806","807","808","809","810","811","812",
        "813","814","815","816","817","818","819","820", NULL};

    for (int si = 0; testSongs[si]; si++)
    {
        char path[256];
        snprintf(path, sizeof(path), "E:/Pumps/PREX3-Original/STEP/%s.STX", testSongs[si]);

        StepSong song;
        if (!Step_LoadSong(path, &song))
        {
            printf("%s.STX: FAILED to load\n", testSongs[si]);
            continue;
        }

        printf("%s.STX: \"%s\" (%d charts)\n", testSongs[si], song.title, song.chartCount);
        for (int ci = 0; ci < song.chartCount; ci++)
        {
            StepChart* c = &song.charts[ci];
            int noteCount = 0;
            for (uint32_t ri = 0; ri < c->rowCount; ri++)
            {
                StepRow* r = &c->rows[ri];
                if (r->half1.l || r->half1.d || r->half1.u || r->half1.r || r->half1.c ||
                    r->half2.l || r->half2.d || r->half2.u || r->half2.r || r->half2.c)
                    noteCount++;
            }
            printf("  Chart %d: BPM=%.1f diff=%d subdiv=%d rows=%d nonEmpty=%d\n",
                ci, c->bpm, c->difficulty, c->subdiv, c->rowCount, noteCount);
        }
        Step_FreeSong(&song);
    }

    return 0;
}
