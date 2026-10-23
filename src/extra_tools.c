#include "extra_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing, const char *replacement, unsigned long flags);
#else
#include <sys/stat.h>
#endif

enum {
  kExtraToolsScreenCount = 128,
  kExtraToolsAtlasBytes = kExtraToolsScreenCount / 8,
  kExtraToolsGhostCapacity = 360,
  kExtraToolsFileVersion = 3,
  kExtraToolsLegacyFileSize = 8 + 4 + 4 + 4 + 4 + 2 + kExtraToolsAtlasBytes,
  kExtraToolsV2FileSize = 8 + 4 + 4 + 4 + 4 + 2 + 2 + kExtraToolsAtlasBytes * 2,
  kExtraToolsFileSize = kExtraToolsV2FileSize + 4,
};

static const uint8 kExtraToolsFileMagic[8] = { 'Z', '3', 'E', 'X', 'T', 'R', 'A', 0 };
static uint8 g_extratools_atlas[kExtraToolsAtlasBytes];
static uint8 g_extratools_challenge_atlas[kExtraToolsAtlasBytes];
static ExtraToolsGhostPoint g_extratools_ghost[kExtraToolsGhostCapacity];
static int g_extratools_ghost_next, g_extratools_ghost_count;
static ExtraToolsStats g_extratools_stats;
static uint8 g_extratools_last_health;
static bool g_extratools_have_last_health, g_extratools_dirty;

static void ExtraTools_WriteU16(uint8 *p, uint16 value) {
  p[0] = value & 0xff;
  p[1] = value >> 8;
}

static uint16 ExtraTools_ReadU16(const uint8 *p) {
  return (uint16)(p[0] | p[1] << 8);
}

static void ExtraTools_WriteU32(uint8 *p, uint32 value) {
  p[0] = value & 0xff;
  p[1] = (value >> 8) & 0xff;
  p[2] = (value >> 16) & 0xff;
  p[3] = value >> 24;
}

static uint32 ExtraTools_ReadU32(const uint8 *p) {
  return (uint32)p[0] | (uint32)p[1] << 8 | (uint32)p[2] << 16 | (uint32)p[3] << 24;
}

static int ExtraTools_CountVisitedScreens(void) {
  int count = 0;
  for (int i = 0; i < kExtraToolsAtlasBytes; i++) {
    uint8 bits = g_extratools_atlas[i];
    while (bits) {
      bits &= bits - 1;
      count++;
    }
  }
  return count;
}

static uint16 ExtraTools_CountChallengeScreens(void) {
  uint16 count = 0;
  for (int i = 0; i < kExtraToolsAtlasBytes; i++) {
    uint8 bits = g_extratools_challenge_atlas[i];
    while (bits) {
      bits &= bits - 1;
      count++;
    }
  }
  return count;
}

static void ExtraTools_Load(void) {
  uint8 data[kExtraToolsFileSize] = { 0 };
  FILE *f = fopen("user_data/extras.dat", "rb");
  if (!f)
    return;
  size_t count = fread(data, 1, 12, f);
  uint32 version = count == 12 ? ExtraTools_ReadU32(data + 8) : 0;
  size_t expected = version == 1 ? kExtraToolsLegacyFileSize :
                    version == 2 ? kExtraToolsV2FileSize :
                    version == kExtraToolsFileVersion ? kExtraToolsFileSize : 0;
  if (expected >= 12)
    count += fread(data + 12, 1, expected - 12, f);
  int extra = fgetc(f);
  fclose(f);
  if (expected == 0 || count != expected || extra != EOF || memcmp(data, kExtraToolsFileMagic, sizeof(kExtraToolsFileMagic)) != 0) {
    memset(g_extratools_atlas, 0, sizeof(g_extratools_atlas));
    memset(g_extratools_challenge_atlas, 0, sizeof(g_extratools_challenge_atlas));
    memset(&g_extratools_stats, 0, sizeof(g_extratools_stats));
    g_extratools_dirty = true;
    return;
  }
  g_extratools_stats.frames_played = ExtraTools_ReadU32(data + 12);
  g_extratools_stats.deaths = ExtraTools_ReadU32(data + 16);
  g_extratools_stats.visited_screens = ExtraTools_ReadU32(data + 20);
  g_extratools_stats.peak_rupees = ExtraTools_ReadU16(data + 24);
  memcpy(g_extratools_atlas, data + 26, sizeof(g_extratools_atlas));
  g_extratools_stats.visited_screens = ExtraTools_CountVisitedScreens();
  if (version == 1) {
    memset(g_extratools_challenge_atlas, 0, sizeof(g_extratools_challenge_atlas));
    g_extratools_stats.challenge_screens = 0;
    g_extratools_dirty = true;
  } else {
    g_extratools_stats.challenge_screens = ExtraTools_ReadU16(data + 26 + kExtraToolsAtlasBytes);
    memcpy(g_extratools_challenge_atlas, data + 28 + kExtraToolsAtlasBytes, sizeof(g_extratools_challenge_atlas));
    g_extratools_stats.challenge_screens = ExtraTools_CountChallengeScreens();
    if (version == 3) {
      g_extratools_stats.best_challenge_frames = ExtraTools_ReadU32(data + kExtraToolsV2FileSize);
    } else {
      g_extratools_stats.best_challenge_frames = 0;
      g_extratools_dirty = true;
    }
  }
}

void ExtraTools_Init(void) {
  memset(g_extratools_atlas, 0, sizeof(g_extratools_atlas));
  memset(g_extratools_challenge_atlas, 0, sizeof(g_extratools_challenge_atlas));
  memset(g_extratools_ghost, 0, sizeof(g_extratools_ghost));
  memset(&g_extratools_stats, 0, sizeof(g_extratools_stats));
  g_extratools_ghost_next = 0;
  g_extratools_ghost_count = 0;
  g_extratools_last_health = 0;
  g_extratools_have_last_health = false;
  g_extratools_dirty = false;
  ExtraTools_Load();
}

void ExtraTools_RecordFrame(bool active, uint8 screen, uint16 x, uint16 y, uint8 health, uint16 rupees) {
  if (!active)
    return;

  g_extratools_stats.frames_played++;
  g_extratools_dirty = true;
  if (screen < kExtraToolsScreenCount) {
    uint8 mask = (uint8)(1 << (screen & 7));
    uint8 *entry = &g_extratools_atlas[screen >> 3];
    if ((*entry & mask) == 0) {
      *entry |= mask;
      g_extratools_stats.visited_screens++;
      g_extratools_dirty = true;
    }
    uint8 *challenge_entry = &g_extratools_challenge_atlas[screen >> 3];
    if ((*challenge_entry & mask) == 0) {
      *challenge_entry |= mask;
      g_extratools_stats.challenge_screens++;
      g_extratools_dirty = true;
    }
  }
  if (g_extratools_have_last_health && g_extratools_last_health != 0 && health == 0) {
    g_extratools_stats.deaths++;
    g_extratools_dirty = true;
  }
  g_extratools_last_health = health;
  g_extratools_have_last_health = true;
  if (rupees > g_extratools_stats.peak_rupees) {
    g_extratools_stats.peak_rupees = rupees;
    g_extratools_dirty = true;
  }
  if (g_extratools_stats.challenge_screens >= 20 && g_extratools_stats.deaths == 0 &&
      g_extratools_stats.frames_played < 600 * 60 &&
      (!g_extratools_stats.best_challenge_frames || g_extratools_stats.frames_played < g_extratools_stats.best_challenge_frames)) {
    g_extratools_stats.best_challenge_frames = g_extratools_stats.frames_played;
    g_extratools_dirty = true;
  }

  g_extratools_ghost[g_extratools_ghost_next] = (ExtraToolsGhostPoint){ x, y, screen };
  g_extratools_ghost_next = (g_extratools_ghost_next + 1) % kExtraToolsGhostCapacity;
  if (g_extratools_ghost_count < kExtraToolsGhostCapacity)
    g_extratools_ghost_count++;
}

bool ExtraTools_IsVisited(uint8 screen) {
  if (screen >= kExtraToolsScreenCount)
    return false;
  return (g_extratools_atlas[screen >> 3] & (1 << (screen & 7))) != 0;
}

int ExtraTools_GetVisitedCount(void) {
  return (int)g_extratools_stats.visited_screens;
}

void ExtraTools_ClearAtlas(void) {
  memset(g_extratools_atlas, 0, sizeof(g_extratools_atlas));
  g_extratools_stats.visited_screens = 0;
  g_extratools_dirty = true;
}

bool ExtraTools_GetGhostPoint(int age, ExtraToolsGhostPoint *out) {
  if (!out || age < 0 || age >= g_extratools_ghost_count)
    return false;
  int index = (g_extratools_ghost_next + kExtraToolsGhostCapacity - 1 - age) % kExtraToolsGhostCapacity;
  *out = g_extratools_ghost[index];
  return true;
}

const ExtraToolsStats *ExtraTools_GetStats(void) {
  return &g_extratools_stats;
}

void ExtraTools_ResetChallenge(void) {
  g_extratools_stats.frames_played = 0;
  g_extratools_stats.deaths = 0;
  g_extratools_stats.challenge_screens = 0;
  g_extratools_stats.peak_rupees = 0;
  memset(g_extratools_challenge_atlas, 0, sizeof(g_extratools_challenge_atlas));
  g_extratools_have_last_health = false;
  g_extratools_dirty = true;
}

void ExtraTools_Save(void) {
  if (!g_extratools_dirty)
    return;
#ifdef _WIN32
  _mkdir("user_data");
#else
  mkdir("user_data", 0777);
#endif

  uint8 data[kExtraToolsFileSize];
  memcpy(data, kExtraToolsFileMagic, sizeof(kExtraToolsFileMagic));
  ExtraTools_WriteU32(data + 8, kExtraToolsFileVersion);
  ExtraTools_WriteU32(data + 12, g_extratools_stats.frames_played);
  ExtraTools_WriteU32(data + 16, g_extratools_stats.deaths);
  g_extratools_stats.visited_screens = ExtraTools_CountVisitedScreens();
  ExtraTools_WriteU32(data + 20, g_extratools_stats.visited_screens);
  ExtraTools_WriteU16(data + 24, g_extratools_stats.peak_rupees);
  memcpy(data + 26, g_extratools_atlas, sizeof(g_extratools_atlas));
  g_extratools_stats.challenge_screens = ExtraTools_CountChallengeScreens();
  ExtraTools_WriteU16(data + 26 + kExtraToolsAtlasBytes, g_extratools_stats.challenge_screens);
  memcpy(data + 28 + kExtraToolsAtlasBytes, g_extratools_challenge_atlas, sizeof(g_extratools_challenge_atlas));
  ExtraTools_WriteU32(data + kExtraToolsV2FileSize, g_extratools_stats.best_challenge_frames);

  FILE *f = fopen("user_data/extras.dat.tmp", "wb");
  if (!f)
    return;
  bool ok = fwrite(data, 1, sizeof(data), f) == sizeof(data) && fflush(f) == 0;
  if (fclose(f) != 0)
    ok = false;
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA("user_data/extras.dat.tmp", "user_data/extras.dat", 0x1 | 0x8) != 0;
#else
  if (ok)
    ok = rename("user_data/extras.dat.tmp", "user_data/extras.dat") == 0;
#endif
  if (ok)
    g_extratools_dirty = false;
  else
    remove("user_data/extras.dat.tmp");
}
