#ifndef STEP_H
#define STEP_H

#include <stdint.h>
#include <stdbool.h>

#define STX_MAGIC "STF4"
#define STX_TITLE_OFFSET 0x30
#define STX_OFFSET_TABLE 0xF0
#define STX_MAX_STREAMS 9
#define STX_STREAM_START 1
#define STX_CHART_MAX 16
#define STEP_HALF_PANELS 5
#define STEP_ROW_SIZE 16

#define DIFF_NONE 0xFF

typedef struct {
    uint8_t l;
    uint8_t d;
    uint8_t u;
    uint8_t r;
    uint8_t c;
} StepHalf;

typedef struct {
    StepHalf half1;
    StepHalf half2;
} StepRow;

typedef struct {
    float bpm;
    uint32_t difficulty;
    uint32_t subdiv;
    uint32_t rowCount;
    StepRow* rows;
} StepChart;

typedef struct {
    char title[64];
    int chartCount;
    StepChart charts[STX_CHART_MAX];
} StepSong;

bool Step_LoadSong(const char* path, StepSong* song);
void Step_FreeSong(StepSong* song);
int Step_FindChart(const StepSong* song, uint32_t difficulty);

#endif
