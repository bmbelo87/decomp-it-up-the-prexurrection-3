#ifndef STEP_H
#define STEP_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if !defined(_WIN32)
#include <strings.h>
#ifndef _stricmp
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#endif
#endif

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

// Note type values:
#define NT_TAP     1  // normal tap (0x01)
#define NT_HOLD_H  10 // hold head (0x0A)
#define NT_HOLD_B  11 // hold body (0x0B)
#define NT_HOLD_T  12 // hold tail (0x0C)

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
    int totalNotes;
    // Split section (BPM changes)
    bool hasSplit;
    // Per-segment timing (segments = main + each split block)
    uint32_t segmentCount; // main + number of splits
    struct {
        float bpm;
        uint32_t beatPerMeasure;
        uint32_t beatSplit;
        int32_t delay;
        uint32_t rowStart; // first row of this segment
        uint32_t rowCount; // rows in this segment
    } segments[8]; // up to 7 splits
} StepChart;

typedef struct {
    char title[64];
    int chartCount;
    StepChart charts[9];
} StepSong;

bool Step_LoadSong(const char* path, StepSong* song);
void Step_FreeSong(StepSong* song);
int Step_SelectChart(const char* modeName, int fallbackSection);

/* Random Step: embaralha painéis do chart no load time.
 *
 * panelMode:
 *   0 = Single (5 painéis): P1 → half1, P2 → half2, permutações independentes
 *   1 = Double/Nightmare (10 painéis): 1 player, embaralha half1+half2 juntos
 *   2 = Half Double (6 painéis): 1 player, embaralha as 6 posições HD específicas
 *
 * Gera permutação via 4 swaps aleatórios (fiel ao original) e aplica
 * uniformemente a todos os rows — hold continuity garantida. */
void Step_ApplyRandomShuffle(StepChart* chart, int panelMode, bool shuffleP1, bool shuffleP2);

/* Mirror: espelha paineis com permutacao fixa (load time, hold-safe).
 * Single:  Z<->E, Q<->C, S fica       perm5=[3,4,2,0,1]
 * HD:      S<->5, E<->1, C<->7        perm6=[5,3,4,1,2,0]
 * DN:      Z<->9, Q<->3, S<->5, E<->1, C<->7  perm10=[8,9,7,5,6,3,4,2,0,1] */
void Step_ApplyMirror(StepChart* chart, int panelMode, bool mirrorP1, bool mirrorP2);

#endif
