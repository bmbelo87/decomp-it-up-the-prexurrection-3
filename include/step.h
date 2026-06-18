#ifndef STEP_H
#define STEP_H

#include <stdint.h>
#include <stdbool.h>

#define STX_MAGIC "STF4"
#define STX_HEADER_SIZE 288
#define STX_TITLE_OFFSET 0x3C
#define STX_OFFSET_TABLE 0xFC
#define STX_SECTION_COUNT 9
#define STX_SECTION_HEADER 0xD0
#define STX_ROW_SIZE 13

#define STX_DECOMP_HEADER 128
#define STX_GRID_OFFSET 132

#define STEP_PANELS_SINGLE 5
#define STEP_PANELS_DOUBLE 10
#define STEP_PANELS_HALF 6

typedef struct {
    uint8_t dl;
    uint8_t ul;
    uint8_t cn;
    uint8_t ur;
    uint8_t dr;
} StepHalf;

typedef struct {
    StepHalf half1;
    StepHalf half2;
} StepRow;

typedef struct {
    float bpm;
    uint32_t beatPerMeasure;
    uint32_t beatSplit;
    int32_t delay;
    uint32_t rowCount;
    StepRow* rows;
    int panelCount;
} StepChart;

typedef struct {
    char title[64];
    int chartCount;
    StepChart charts[9];
} StepSong;

bool Step_LoadSong(const char* path, StepSong* song);
void Step_FreeSong(StepSong* song);
int Step_SelectChart(const char* modeName, int fallbackSection);

#endif
