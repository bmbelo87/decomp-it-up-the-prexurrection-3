## Judge Sprites Implementation

**Data:** 22/06/2026  
**Goal:** Replace text-based judge display with sprites from ARROW542.SP2

### Changes Made

1. **Added sprite indices mapping** in `gameplay.c`:
```c
static const int g_judgeSpriteIndices[] = { -1, 10, 11, 12, 13, 14 }; 
// JT_PERFECT=10 (perfec), JT_GREAT=11 (great_), JT_GOOD=12 (good__), JT_BAD=13 (bad___), JT_MISS=14 (miss__)
```

2. **Modified judge rendering** to use sprites instead of text:
```c
int judgeSpriteIdx = g_fontArrow542 + g_judgeSpriteIndices[jt];
if (g_fontArrow542 >= 0 && judgeSpriteIdx >= 0 && judgeSpriteIdx < g_game.sprTileCount) {
    float sw = (float)g_game.sprTiles[judgeSpriteIdx].srcW;
    float sh = (float)g_game.sprTiles[judgeSpriteIdx].srcH;
    Sprite_DrawTileUV(judgeSpriteIdx, centerX, centerY + 55 + 50, sw, sh, brightA);
}
```

3. **Modified combo rendering** to use combo_ sprite:
```c
int comboSpriteIdx = g_fontArrow542 + 14; // combo_ sprite at index 14
if (g_fontArrow542 >= 0 && comboSpriteIdx >= 0 && comboSpriteIdx < g_game.sprTileCount) {
    float sw = (float)g_game.sprTiles[comboSpriteIdx].srcW;
    float sh = (float)g_game.sprTiles[comboSpriteIdx].srcH;
    Sprite_DrawTileUV(comboSpriteIdx, centerX, centerY - 12 + 50, sw, sh, brightA);
}
```

### Key Points

- Sprites are rendered at natural size (no artificial scaling)
- Position adjusted with +50px offset to correct Y-axis display
- Uses existing ARROW542.SP2 resource loaded in `Resource_LoadFontAndArrows()`
- Maintains original timing and alpha fade effects
- Combo sprite replaces "COMBO" text display

### Files Modified

- `src/gameplay.c`: Added sprite indices and modified rendering logic