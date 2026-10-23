#ifndef ZELDA3_EXTRA_TOOLS_H_
#define ZELDA3_EXTRA_TOOLS_H_

#include <stdbool.h>
#include "types.h"

typedef struct ExtraToolsGhostPoint {
  uint16 x, y;
  uint8 screen;
} ExtraToolsGhostPoint;

typedef struct ExtraToolsStats {
  uint32 frames_played, deaths, visited_screens;
  uint16 challenge_screens;
  uint16 peak_rupees;
  uint32 best_challenge_frames;
} ExtraToolsStats;

void ExtraTools_Init(void);
void ExtraTools_RecordFrame(bool active, uint8 screen, uint16 x, uint16 y, uint8 health, uint16 rupees);
bool ExtraTools_IsVisited(uint8 screen);
int ExtraTools_GetVisitedCount(void);
void ExtraTools_ClearAtlas(void);
bool ExtraTools_GetGhostPoint(int age, ExtraToolsGhostPoint *out);
const ExtraToolsStats *ExtraTools_GetStats(void);
void ExtraTools_ResetChallenge(void);
void ExtraTools_Save(void);

#endif  // ZELDA3_EXTRA_TOOLS_H_
