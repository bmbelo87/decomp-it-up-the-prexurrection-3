#include "step.h"
#include "zlibinflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTION_HEADER_SIZE 0xD0

bool Step_LoadSong(const char* path, StepSong* song)
{
    memset(song, 0, sizeof(StepSong));

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    if (fileSize < STX_OFFSET_TABLE + 36)
    {
        fclose(f);
        return false;
    }

    uint8_t* fileBuf = (uint8_t*)malloc(fileSize);
    if (!fileBuf)
    {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    fread(fileBuf, 1, fileSize, f);
    fclose(f);

    if (memcmp(fileBuf, STX_MAGIC, 4) != 0)
    {
        free(fileBuf);
        return false;
    }

    int maxSkip = 64;
    const char* nameSrc = (const char*)fileBuf + STX_TITLE_OFFSET;
    while (maxSkip-- > 0 && *nameSrc == 0) nameSrc++;
    if (*nameSrc)
    {
        strncpy(song->title, nameSrc, sizeof(song->title) - 1);
        song->title[sizeof(song->title) - 1] = '\0';
    }
    else
    {
        song->title[0] = '\0';
    }

    uint32_t sectionOffsets[9];
    for (int i = 0; i < 9; i++)
    {
        sectionOffsets[i] = ((uint32_t*)&fileBuf[STX_OFFSET_TABLE])[i];
    }

    for (int si = 3; si < 9; si++)
    {
        uint32_t secStart = sectionOffsets[si];
        if (secStart == 0) continue;

        uint32_t secEnd = (si < 8) ? sectionOffsets[si + 1] : (uint32_t)fileSize;
        if (secEnd > (uint32_t)fileSize) secEnd = (uint32_t)fileSize;

        uint32_t secLen = secEnd - secStart;
        if (secLen <= SECTION_HEADER_SIZE) continue;

        uint32_t dataStart = secStart + SECTION_HEADER_SIZE;
        uint32_t dataEnd = secEnd;
        uint32_t pos = dataStart;

        while (pos < dataEnd && song->chartCount < STX_CHART_MAX)
        {
            uint32_t remaining = dataEnd - pos;
            if (remaining < 6) break;

            uint8_t* decompBuf = (uint8_t*)malloc(65536);
            if (!decompBuf) break;

            uint32_t decompLen = 65536;
            uint32_t inConsumed = 0;
            int ret = zlib_decompress_ex(fileBuf + pos, remaining, decompBuf, &decompLen, &inConsumed);

            if (ret != 0 || decompLen < 16)
            {
                free(decompBuf);
                pos++;
                continue;
            }

            float bpm;
            uint32_t difficulty, subdiv;
            memcpy(&bpm, decompBuf, 4);
            memcpy(&difficulty, decompBuf + 4, 4);
            memcpy(&subdiv, decompBuf + 8, 4);

            uint32_t dataBytes = decompLen - 16;
            uint32_t count = dataBytes / STEP_ROW_SIZE;

            StepChart* chart = &song->charts[song->chartCount];
            chart->bpm = bpm;
            chart->difficulty = difficulty;
            chart->subdiv = subdiv;
            chart->rowCount = count;
            chart->rows = (StepRow*)malloc(count * sizeof(StepRow));
            if (!chart->rows)
            {
                free(decompBuf);
                pos++;
                continue;
            }

            for (uint32_t ri = 0; ri < count; ri++)
            {
                const uint8_t* src = decompBuf + 16 + ri * STEP_ROW_SIZE;
                chart->rows[ri].half1.l = src[0];
                chart->rows[ri].half1.d = src[1];
                chart->rows[ri].half1.u = src[2];
                chart->rows[ri].half1.r = src[3];
                chart->rows[ri].half1.c = src[4];
                chart->rows[ri].half2.l = src[8];
                chart->rows[ri].half2.d = src[9];
                chart->rows[ri].half2.u = src[10];
                chart->rows[ri].half2.r = src[11];
                chart->rows[ri].half2.c = src[12];
            }

            song->chartCount++;
            free(decompBuf);
            pos += inConsumed;
        }
    }

    free(fileBuf);
    return song->chartCount > 0;
}

void Step_FreeSong(StepSong* song)
{
    for (int i = 0; i < song->chartCount; i++)
    {
        free(song->charts[i].rows);
        song->charts[i].rows = NULL;
    }
    song->chartCount = 0;
}

int Step_FindChart(const StepSong* song, uint32_t difficulty)
{
    for (int i = 0; i < song->chartCount; i++)
    {
        if (song->charts[i].difficulty == difficulty)
            return i;
    }
    return -1;
}
