#ifndef BGA_H
#define BGA_H

#include "pumpy.h"

bool isMenuOverlayLayer(BGALayer* layer);
bool isMenuArrowLayer(BGALayer* layer);
bool isMenuTextLayer(BGALayer* layer);
bool isMenuCenterLayer(BGALayer* layer);
bool layerMatchesDirection(BGALayer* layer, int sel);
int findBGALoopStart(void);
int findBGALoopEnd(void);

#endif
