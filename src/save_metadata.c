#include "save_metadata.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing, const char *replacement, unsigned long flags);
#else
#include <unistd.h>
#endif

enum {
  kSaveMetadataSlotCount = 20,
  kSaveMetadataLabelSize = 25,
  kSaveMetadataBackupCount = 3,
};

static char g_labels[kSaveMetadataSlotCount][kSaveMetadataLabelSize];
static bool g_initialized;

static bool IsSlotValid(int slot) {
  return slot >= 0 && slot < kSaveMetadataSlotCount;
}

static void EnsureSavesDirectory(void) {
#ifdef _WIN32
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif
}

static void GetSavePath(int slot, char *dst, size_t dst_size) {
  snprintf(dst, dst_size, "saves/save%d.sav", slot);
}

static void GetBackupPath(int slot, int backup, char *dst, size_t dst_size) {
  snprintf(dst, dst_size, "saves/save%d.sav.bak%d", slot, backup);
}

static bool FileExists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0;
}

static bool ReplaceFile(const char *temporary, const char *destination);

static bool CopyFileContents(const char *source, const char *destination) {
  FILE *in = fopen(source, "rb");
  if (!in)
    return false;
  char temporary[160];
  snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
  FILE *out = fopen(temporary, "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  uint8_t buffer[8192];
  bool ok = true;
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
    if (fwrite(buffer, 1, n, out) != n) {
      ok = false;
      break;
    }
  }
  if (ferror(in))
    ok = false;
  if (fclose(in) != 0)
    ok = false;
  if (fclose(out) != 0)
    ok = false;
  if (ok)
    ok = ReplaceFile(temporary, destination);
  if (!ok)
    remove(temporary);
  return ok;
}

static bool ReplaceFile(const char *temporary, const char *destination) {
#ifdef _WIN32
  return MoveFileExA(temporary, destination, 0x1 | 0x8) != 0;
#else
  return rename(temporary, destination) == 0;
#endif
}

static void SetDefaultLabels(char labels[kSaveMetadataSlotCount][kSaveMetadataLabelSize]) {
  for (int i = 0; i < kSaveMetadataSlotCount; i++)
    snprintf(labels[i], kSaveMetadataLabelSize, "SAUVEGARDE %02d", i + 1);
}

static bool IsSafeLabelCharacter(unsigned char c) {
  return c >= 32 && c <= 126 && c != '=' && c != '[' && c != ']' && c != ';' && c != '#';
}

static bool NormalizeLabel(const char *label, char *dst, size_t dst_size) {
  if (!label || dst_size == 0)
    return false;
  while (*label == ' ' || *label == '\t')
    label++;
  size_t length = 0;
  while (*label && *label != '\r' && *label != '\n') {
    unsigned char c = (unsigned char)*label++;
    if (!IsSafeLabelCharacter(c) || length + 1 >= dst_size)
      return false;
    dst[length++] = (char)c;
  }
  while (length && dst[length - 1] == ' ')
    length--;
  if (length == 0)
    return false;
  dst[length] = 0;
  return true;
}

static bool WriteLabels(const char labels[kSaveMetadataSlotCount][kSaveMetadataLabelSize]) {
  const char *filename = "saves/labels.ini";
  const char *temporary = "saves/labels.ini.tmp";
  EnsureSavesDirectory();
  FILE *f = fopen(temporary, "wb");
  if (!f)
    return false;
  fputs("# Zelda3 save slot labels\n[Slots]\n", f);
  for (int i = 0; i < kSaveMetadataSlotCount; i++)
    fprintf(f, "%d=%s\n", i, labels[i]);
  bool ok = ferror(f) == 0;
  if (fclose(f) != 0)
    ok = false;
  if (ok)
    ok = ReplaceFile(temporary, filename);
  if (!ok)
    remove(temporary);
  return ok;
}

static void LoadLabels(void) {
  SetDefaultLabels(g_labels);
  FILE *f = fopen("saves/labels.ini", "rb");
  if (!f)
    return;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    char *start = line;
    while (*start == ' ' || *start == '\t')
      start++;
    if (*start == 0 || *start == '#' || *start == ';' || *start == '[')
      continue;
    char *equal = strchr(start, '=');
    if (!equal)
      continue;
    *equal++ = 0;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
      *--end = 0;
    char *index_end;
    long slot = strtol(start, &index_end, 10);
    if (index_end == start || *index_end != 0 || !IsSlotValid((int)slot))
      continue;
    while (*equal == ' ' || *equal == '\t')
      equal++;
    char *newline = strpbrk(equal, "\r\n");
    if (newline)
      *newline = 0;
    char safe[kSaveMetadataLabelSize];
    if (NormalizeLabel(equal, safe, sizeof(safe)))
      strcpy(g_labels[slot], safe);
  }
  fclose(f);
}

void SaveMetadata_Init(void) {
  if (g_initialized)
    return;
  LoadLabels();
  g_initialized = true;
}

const char *SaveMetadata_Label(int slot) {
  SaveMetadata_Init();
  return IsSlotValid(slot) ? g_labels[slot] : "";
}

bool SaveMetadata_SetLabel(int slot, const char *label) {
  SaveMetadata_Init();
  if (!IsSlotValid(slot))
    return false;
  char safe[kSaveMetadataLabelSize];
  if (!NormalizeLabel(label, safe, sizeof(safe)))
    return false;
  char replacement[kSaveMetadataSlotCount][kSaveMetadataLabelSize];
  memcpy(replacement, g_labels, sizeof(replacement));
  strcpy(replacement[slot], safe);
  if (!WriteLabels(replacement))
    return false;
  memcpy(g_labels, replacement, sizeof(g_labels));
  return true;
}

bool SaveMetadata_BackupBeforeOverwrite(int slot) {
  if (!IsSlotValid(slot))
    return false;
  EnsureSavesDirectory();
  char current[64], source[64], destination[64];
  GetSavePath(slot, current, sizeof(current));
  if (!FileExists(current))
    return true;

  // Shift old history from oldest to newest so a failure never overwrites the live save.
  GetBackupPath(slot, 2, source, sizeof(source));
  GetBackupPath(slot, 3, destination, sizeof(destination));
  if (FileExists(source) && !CopyFileContents(source, destination))
    return false;

  GetBackupPath(slot, 1, source, sizeof(source));
  GetBackupPath(slot, 2, destination, sizeof(destination));
  if (FileExists(source) && !CopyFileContents(source, destination))
    return false;

  GetBackupPath(slot, 1, destination, sizeof(destination));
  return CopyFileContents(current, destination);
}

int SaveMetadata_BackupCount(int slot) {
  if (!IsSlotValid(slot))
    return 0;
  int count = 0;
  char path[64];
  for (int i = 1; i <= kSaveMetadataBackupCount; i++) {
    GetBackupPath(slot, i, path, sizeof(path));
    if (FileExists(path))
      count++;
  }
  return count;
}

bool SaveMetadata_RestoreLatest(int slot) {
  if (!IsSlotValid(slot))
    return false;
  char latest[64], current[64], temporary[64];
  GetBackupPath(slot, 1, latest, sizeof(latest));
  if (!FileExists(latest))
    return false;

  // Snapshot the selected backup before rotating history; preserve today's save as backup 1.
  snprintf(temporary, sizeof(temporary), "saves/save%d.restore.tmp", slot);
  if (!CopyFileContents(latest, temporary))
    return false;
  if (!SaveMetadata_BackupBeforeOverwrite(slot)) {
    remove(temporary);
    return false;
  }
  GetSavePath(slot, current, sizeof(current));
  bool ok = ReplaceFile(temporary, current);
  if (!ok) {
    // On Windows the destination may not be replaceable through the C runtime; preserve
    // the original at backup 1 and attempt a direct copy as a safe fallback.
    ok = CopyFileContents(temporary, current);
  }
  remove(temporary);
  return ok;
}

bool SaveMetadata_LatestBackupDate(int slot, char *dst, size_t dst_size) {
  if (!dst || dst_size == 0 || !IsSlotValid(slot))
    return false;
  dst[0] = 0;
  char path[64];
  struct stat info;
  GetBackupPath(slot, 1, path, sizeof(path));
  if (stat(path, &info) != 0)
    return false;
  struct tm *time_info = localtime(&info.st_mtime);
  if (!time_info)
    return false;
  if (strftime(dst, dst_size, "%Y-%m-%d %H:%M", time_info) == 0) {
    dst[0] = 0;
    return false;
  }
  return true;
}
