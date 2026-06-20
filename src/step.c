#include "step.h"
#include "zlibinflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Step_LoadSong(const char* path, StepSong* song)
{
    memset(song, 0, sizeof(StepSong));

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    if (fileSize < STX_HEADER_SIZE)
    {
        fclose(f);
        return false;
    }

    uint8_t* header = (uint8_t*)malloc(STX_HEADER_SIZE);
    if (!header) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    fread(header, 1, STX_HEADER_SIZE, f);

    if (memcmp(header, STX_MAGIC, 4) != 0)
    {
        free(header); fclose(f);
        return false;
    }

    const char* titlePtr = (const char*)header + STX_TITLE_OFFSET;
    while (*titlePtr == 0 && titlePtr < (const char*)header + STX_OFFSET_TABLE)
        titlePtr++;
    if (*titlePtr)
    {
        strncpy(song->title, titlePtr, sizeof(song->title) - 1);
        song->title[sizeof(song->title) - 1] = '\0';
    }

    uint32_t sectionOffsets[STX_SECTION_COUNT];
    memcpy(sectionOffsets, header + STX_OFFSET_TABLE, STX_SECTION_COUNT * 4);
    free(header);

    for (int si = 0; si < STX_SECTION_COUNT; si++)
    {
        uint32_t secOff = sectionOffsets[si];
        if (secOff == 0 || secOff >= (uint32_t)fileSize) continue;

        fseek(f, secOff, SEEK_SET);

        uint8_t secHeader[STX_SECTION_HEADER];
        if (fread(secHeader, 1, STX_SECTION_HEADER, f) != STX_SECTION_HEADER)
            continue;

        uint32_t compSize;
        memcpy(&compSize, secHeader + STX_SECTION_HEADER - 4, 4);
        if (compSize == 0 || compSize > (uint32_t)(fileSize - secOff - STX_SECTION_HEADER))
            continue;

        uint32_t secEnd = secOff + STX_SECTION_HEADER + compSize;

        // Read main section compressed data
        fseek(f, secOff + STX_SECTION_HEADER, SEEK_SET);
        uint8_t* compData = (uint8_t*)malloc(compSize);
        if (!compData || fread(compData, 1, compSize, f) != compSize)
        {
            free(compData);
            continue;
        }

        uint8_t* decompBuf = (uint8_t*)malloc(65536);
        if (!decompBuf) { free(compData); continue; }

        uint32_t decompLen = 65536;
        uint32_t inConsumed = 0;
        int ret = zlib_decompress_ex(compData, compSize, decompBuf, &decompLen, &inConsumed);
        free(compData);

        if (ret != 0 || decompLen < STX_GRID_OFFSET + STX_ROW_SIZE)
        {
            free(decompBuf);
            continue;
        }

        float bpm;
        uint32_t beatPerMeasure, beatSplit;
        int32_t delay;
        memcpy(&bpm, decompBuf, 4);
        memcpy(&beatPerMeasure, decompBuf + 4, 4);
        memcpy(&beatSplit, decompBuf + 8, 4);
        memcpy(&delay, decompBuf + 12, 4);

        uint32_t rowCount;
        memcpy(&rowCount, decompBuf + STX_DECOMP_HEADER, 4);

        uint32_t dataBytes = (uint32_t)(decompLen - STX_GRID_OFFSET);
        uint32_t expectedRows = dataBytes / STX_ROW_SIZE;
        if (rowCount > expectedRows)
            rowCount = expectedRows;
        if (rowCount == 0)
        {
            free(decompBuf);
            continue;
        }

        int chartIdx = song->chartCount;
        StepChart* chart = &song->charts[chartIdx];
        chart->bpm = bpm;
        chart->beatPerMeasure = beatPerMeasure;
        chart->beatSplit = beatSplit;
        chart->delay = delay;
        chart->rowCount = rowCount;
        chart->hasSplit = false;
        chart->segmentCount = 1;
        chart->segments[0].bpm = bpm;
        chart->segments[0].beatPerMeasure = beatPerMeasure;
        chart->segments[0].beatSplit = beatSplit;
        chart->segments[0].delay = delay;
        chart->segments[0].rowStart = 0;
        chart->segments[0].rowCount = rowCount;

        bool mirror = true;
        chart->panelCount = STEP_PANELS_SINGLE;
        if (si == 3 || si == 5 || si == 6)
        {
            chart->panelCount = STEP_PANELS_DOUBLE;
            mirror = false;
        }

        chart->rows = (StepRow*)malloc(rowCount * sizeof(StepRow));
        if (!chart->rows) { free(decompBuf); continue; }

        for (uint32_t ri = 0; ri < rowCount; ri++)
        {
            const uint8_t* src = decompBuf + STX_GRID_OFFSET + ri * STX_ROW_SIZE;
            chart->rows[ri].half1.dl = src[0];
            chart->rows[ri].half1.ul = src[1];
            chart->rows[ri].half1.cn = src[2];
            chart->rows[ri].half1.ur = src[3];
            chart->rows[ri].half1.dr = src[4];

            if (mirror)
            {
                chart->rows[ri].half2.dl = src[0];
                chart->rows[ri].half2.ul = src[1];
                chart->rows[ri].half2.cn = src[2];
                chart->rows[ri].half2.ur = src[3];
                chart->rows[ri].half2.dr = src[4];
            }
            else
            {
                chart->rows[ri].half2.dl = src[5];
                chart->rows[ri].half2.ul = src[6];
                chart->rows[ri].half2.cn = src[7];
                chart->rows[ri].half2.ur = src[8];
                chart->rows[ri].half2.dr = src[9];
            }
        }

        free(decompBuf);

        // Check for split sections (gap before the NEXT non-zero section only)
        int nextSi = si + 1;
        while (nextSi < STX_SECTION_COUNT && sectionOffsets[nextSi] == 0)
            nextSi++;
        if (nextSi < STX_SECTION_COUNT)
        {
            uint32_t nextOff = sectionOffsets[nextSi];
            uint32_t prevSecEnd = secEnd;

            if (nextOff > prevSecEnd + 4)
            {
                uint32_t gapSize = nextOff - prevSecEnd;
                uint8_t* gapBuf = (uint8_t*)malloc(gapSize);
                if (gapBuf)
                {
                    fseek(f, prevSecEnd, SEEK_SET);
                    if (fread(gapBuf, 1, gapSize, f) == gapSize)
                    {
                        for (uint32_t gapOff = 4; gapOff + 2 < gapSize; )
                        {
                            if (gapBuf[gapOff] != 0x78 || gapBuf[gapOff + 1] != 0x9C) { gapOff++; continue; }

                            uint8_t* splitDecomp = (uint8_t*)malloc(65536);
                            if (!splitDecomp) break;

                            uint32_t sdl = 65536, sic = 0;
                            int sret = zlib_decompress_ex(gapBuf + gapOff, gapSize - gapOff, splitDecomp, &sdl, &sic);
                            if (sret != 0 || sdl < STX_GRID_OFFSET + STX_ROW_SIZE) { free(splitDecomp); break; }

                            float sBpm;
                            uint32_t sBpmM, sBpmS;
                            int32_t sDelay;
                            memcpy(&sBpm, splitDecomp, 4);
                            memcpy(&sBpmM, splitDecomp + 4, 4);
                            memcpy(&sBpmS, splitDecomp + 8, 4);
                            memcpy(&sDelay, splitDecomp + 12, 4);

                            uint32_t sRowCount;
                            memcpy(&sRowCount, splitDecomp + STX_DECOMP_HEADER, 4);
                            uint32_t expRows2 = (sdl - STX_GRID_OFFSET) / STX_ROW_SIZE;
                            if (sRowCount > expRows2) sRowCount = expRows2;
                            if (sRowCount == 0) { free(splitDecomp); break; }

                            bool validBpm = (sBpm > 0.0f && sBpm < 2000.0f);
                            bool validSplitVal = (sBpmS > 0 && sBpmS <= 256 && sBpmM > 0 && (sBpmM % sBpmS == 0));
                            bool validDelay = (sDelay >= 0 && sDelay < 10000);
                            bool validRowCount = (sRowCount > 0 && sRowCount < 50000);
                            bool validSplit = (validBpm && validSplitVal && validDelay && validRowCount);
                            if (!validSplit) { free(splitDecomp); break; }

                            if (!chart->hasSplit) chart->hasSplit = true;

                            int segIdx = chart->segmentCount;
                            bool added = (segIdx < 8);
                            if (added)
                            {
                                chart->segments[segIdx].bpm = sBpm;
                                chart->segments[segIdx].beatPerMeasure = sBpmM;
                                chart->segments[segIdx].beatSplit = sBpmS;
                                chart->segments[segIdx].delay = sDelay;
                                chart->segments[segIdx].rowStart = rowCount;
                                chart->segments[segIdx].rowCount = sRowCount;
                                chart->segmentCount++;
                            }

                            if (added) {
                                uint32_t totalRows = rowCount + sRowCount;
                                StepRow* merged = (StepRow*)realloc(chart->rows, totalRows * sizeof(StepRow));
                                if (!merged) { free(splitDecomp); break; }
                                chart->rows = merged;
                                for (uint32_t ri = 0; ri < sRowCount; ri++)
                                {
                                    const uint8_t* src = splitDecomp + STX_GRID_OFFSET + ri * STX_ROW_SIZE;
                                    StepRow* dst = &chart->rows[rowCount + ri];
                                    dst->half1.dl = src[0]; dst->half1.ul = src[1];
                                    dst->half1.cn = src[2]; dst->half1.ur = src[3];
                                    dst->half1.dr = src[4];
                                    if (mirror) {
                                        dst->half2.dl = src[0]; dst->half2.ul = src[1];
                                        dst->half2.cn = src[2]; dst->half2.ur = src[3];
                                        dst->half2.dr = src[4];
                                    } else {
                                        dst->half2.dl = src[5]; dst->half2.ul = src[6];
                                        dst->half2.cn = src[7]; dst->half2.ur = src[8];
                                        dst->half2.dr = src[9];
                                    }
                                }
                                rowCount += sRowCount;
                                chart->rowCount = rowCount;
                            }
                            free(splitDecomp);
                            if (sic > 0) gapOff += sic - 1;
                        }
                    }
                    free(gapBuf);
                }
            }
        }
        song->chartCount++;
    }

    fclose(f);
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

int Step_SelectChart(const char* modeName, int fallbackSection)
{
    if (modeName == NULL) return fallbackSection;

    if (_stricmp(modeName, "PRACTICE") == 0) return 0;
    if (_stricmp(modeName, "NORMAL") == 0) return 1;
    if (_stricmp(modeName, "HARD") == 0) return 2;
    if (_stricmp(modeName, "NIGHTMARE") == 0) return 3;
    if (_stricmp(modeName, "CRAZY") == 0) return 4;
    if (_stricmp(modeName, "FULLDOUBLE") == 0) return 5;
    if (_stricmp(modeName, "HALFDOUBLE") == 0) return 6;
    if (_stricmp(modeName, "DIVISION") == 0) return 7;
    if (_stricmp(modeName, "LIGHTMAP") == 0) return 8;

    return fallbackSection;
}
