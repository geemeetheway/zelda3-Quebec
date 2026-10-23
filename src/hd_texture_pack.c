#include "hd_texture_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb/stb_image.h"

enum {
  kHdTexturePackMax = 16,
  kHdTextureTileCount = 256,
  kHdTextureTileSize = 32,
  kHdTextureFolderMax = 63,
  kHdTextureNameMax = 95,
  kHdTextureDescriptionMax = 127,
  kHdTexturePathMax = 191,
  kHdTexturePreviewWidth = 96,
  kHdTexturePreviewHeight = 54,
};

typedef struct HdTexturePack {
  char folder[kHdTextureFolderMax + 1];
  char name[kHdTextureNameMax + 1];
  char description[kHdTextureDescriptionMax + 1];
  char preview_path[kHdTexturePathMax + 1];
  uint32_t *preview;
} HdTexturePack;

static Ppu *g_ppu;
static HdTexturePack g_packs[kHdTexturePackMax];
static int g_pack_count;
static int g_selected = -1;
static bool g_enabled;
static uint32_t *g_tiles[kHdTextureTileCount];
static char g_status[160] = "Aucun pack HD installe";

static int HdEqualsNoCase(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
      return 0;
  }
  return *a == 0 && *b == 0;
}

static char *HdTrim(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
  return s;
}

static bool HdSafeFolder(const char *folder) {
  if (!*folder || strlen(folder) > kHdTextureFolderMax)
    return false;
  for (const char *p = folder; *p; p++)
    if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-'))
      return false;
  return true;
}

static bool HdSafeRelativePath(const char *path) {
  if (!*path || strlen(path) > kHdTexturePathMax || *path == '/' || *path == '\\' || strchr(path, ':'))
    return false;
  const char *segment = path;
  for (const char *p = path;; p++) {
    if (*p == '\\' || *p == '/' || *p == 0) {
      size_t length = (size_t)(p - segment);
      if (length == 0 || (length == 1 && segment[0] == '.') ||
          (length == 2 && segment[0] == '.' && segment[1] == '.'))
        return false;
      if (*p == 0)
        break;
      segment = p + 1;
    }
  }
  return true;
}

static void HdBuildPath(char *dst, size_t dst_size, const char *a, const char *b) {
  snprintf(dst, dst_size, "%s/%s", a, b);
}

static void HdClearActive(void) {
  if (g_ppu)
    PpuClearMode7TextureTiles(g_ppu);
  for (int i = 0; i < kHdTextureTileCount; i++) {
    free(g_tiles[i]);
    g_tiles[i] = NULL;
  }
  g_enabled = false;
  g_selected = -1;
}

static bool HdReadPackMetadata(const char *folder, HdTexturePack *pack) {
  char path[320], line[512];
  bool in_pack = false, found = false;
  memset(pack, 0, sizeof(*pack));
  HdBuildPath(path, sizeof(path), "hd_textures", folder);
  size_t used = strlen(path);
  snprintf(path + used, sizeof(path) - used, "/manifest.ini");
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  while (fgets(line, sizeof(line), f)) {
    char *s = HdTrim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      in_pack = HdEqualsNoCase(s, "[Pack]");
      continue;
    }
    if (!in_pack)
      continue;
    char *equals = strchr(s, '=');
    if (!equals)
      continue;
    *equals++ = 0;
    char *key = HdTrim(s), *value = HdTrim(equals);
    if (HdEqualsNoCase(key, "Name")) {
      if (*value && strlen(value) < sizeof(pack->name)) {
        snprintf(pack->name, sizeof(pack->name), "%s", value);
        found = true;
      }
    } else if (HdEqualsNoCase(key, "Description")) {
      if (strlen(value) <= kHdTextureDescriptionMax)
        snprintf(pack->description, sizeof(pack->description), "%s", value);
    } else if (HdEqualsNoCase(key, "Preview")) {
      if (HdSafeRelativePath(value))
        snprintf(pack->preview_path, sizeof(pack->preview_path), "%s", value);
    }
  }
  fclose(f);
  return found;
}

static void HdAddPack(const char *folder) {
  if (g_pack_count >= kHdTexturePackMax || !HdSafeFolder(folder))
    return;
  for (int i = 0; i < g_pack_count; i++)
    if (HdEqualsNoCase(g_packs[i].folder, folder)) return;
  HdTexturePack pack;
  if (!HdReadPackMetadata(folder, &pack))
    return;
  snprintf(pack.folder, sizeof(pack.folder), "%s", folder);
  g_packs[g_pack_count++] = pack;
}

static int HdParseCatalog(void) {
  FILE *f = fopen("hd_textures/catalog.ini", "rb");
  char line[512];
  bool in_catalog = false;
  if (f) while (g_pack_count < kHdTexturePackMax && fgets(line, sizeof(line), f)) {
    char *s = HdTrim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      in_catalog = HdEqualsNoCase(s, "[Catalog]");
      continue;
    }
    if (!in_catalog)
      continue;
    char *equals = strchr(s, '=');
    if (!equals)
      continue;
    *equals++ = 0;
    char *key = HdTrim(s), *folder = HdTrim(equals);
    if (tolower((unsigned char)key[0]) != 'p' || tolower((unsigned char)key[1]) != 'a' ||
        tolower((unsigned char)key[2]) != 'c' || tolower((unsigned char)key[3]) != 'k' || !key[4])
      continue;
    char *end;
    long order = strtol(key + 4, &end, 10);
    if (*end || order < 1 || !HdSafeFolder(folder))
      continue;
    HdAddPack(folder);
  }
  if (f) fclose(f);

#ifdef _WIN32
  struct _finddata_t entry;
  intptr_t search = _findfirst("hd_textures/*", &entry);
  if (search != -1) {
    do {
      if ((entry.attrib & _A_SUBDIR) && strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0)
        HdAddPack(entry.name);
    } while (g_pack_count < kHdTexturePackMax && _findnext(search, &entry) == 0);
    _findclose(search);
  }
#else
  DIR *directory = opendir("hd_textures");
  if (directory) {
    struct dirent *entry;
    while (g_pack_count < kHdTexturePackMax && (entry = readdir(directory)) != NULL) {
      if (entry->d_name[0] == '.')
        continue;
      char path[256];
      struct stat info;
      snprintf(path, sizeof(path), "hd_textures/%s", entry->d_name);
      if (lstat(path, &info) == 0 && S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode))
        HdAddPack(entry->d_name);
    }
    closedir(directory);
  }
#endif
  return g_pack_count;
}

static bool HdReadPngSize(const char *path, uint32_t *width_out, uint32_t *height_out) {
  static const uint8_t png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  uint8_t header[24];
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  size_t count = fread(header, 1, sizeof(header), f);
  fclose(f);
  if (count != sizeof(header) || memcmp(header, png_signature, sizeof(png_signature)) != 0 ||
      memcmp(header + 12, "IHDR", 4) != 0)
    return false;
  uint32_t width = (uint32_t)header[16] << 24 | (uint32_t)header[17] << 16 |
                   (uint32_t)header[18] << 8 | header[19];
  uint32_t height = (uint32_t)header[20] << 24 | (uint32_t)header[21] << 16 |
                    (uint32_t)header[22] << 8 | header[23];
  if (width_out) *width_out = width;
  if (height_out) *height_out = height;
  return true;
}

static bool HdReadTilePngSize(const char *path) {
  uint32_t width, height;
  return HdReadPngSize(path, &width, &height) && width == kHdTextureTileSize && height == kHdTextureTileSize;
}

static void HdLoadPreview(HdTexturePack *pack) {
  if (!pack->preview_path[0])
    return;
  char folder_path[256], image_path[512];
  snprintf(folder_path, sizeof(folder_path), "hd_textures/%s", pack->folder);
  HdBuildPath(image_path, sizeof(image_path), folder_path, pack->preview_path);
  uint32_t width, height;
  if (!HdReadPngSize(image_path, &width, &height) || width != kHdTexturePreviewWidth || height != kHdTexturePreviewHeight)
    return;
  int decoded_width = 0, decoded_height = 0, channels = 0;
  uint8_t *rgba = stbi_load(image_path, &decoded_width, &decoded_height, &channels, 4);
  if (!rgba || decoded_width != kHdTexturePreviewWidth || decoded_height != kHdTexturePreviewHeight) {
    if (rgba) stbi_image_free(rgba);
    return;
  }
  pack->preview = (uint32_t*)malloc(kHdTexturePreviewWidth * kHdTexturePreviewHeight * sizeof(uint32_t));
  if (!pack->preview) {
    stbi_image_free(rgba);
    return;
  }
  for (int i = 0; i < kHdTexturePreviewWidth * kHdTexturePreviewHeight; i++)
    pack->preview[i] = (uint32_t)rgba[i * 4 + 3] << 24 | (uint32_t)rgba[i * 4] << 16 |
                       (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
  stbi_image_free(rgba);
}

static bool HdParseAndLoadTiles(int index, uint32_t *tiles[kHdTextureTileCount], int *loaded_count) {
  char manifest[320], line[512];
  snprintf(manifest, sizeof(manifest), "hd_textures/%s/manifest.ini", g_packs[index].folder);
  FILE *f = fopen(manifest, "rb");
  if (!f)
    return false;
  bool in_mode7 = false;
  int loaded = 0;
  while (fgets(line, sizeof(line), f)) {
    char *s = HdTrim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      in_mode7 = HdEqualsNoCase(s, "[Mode7]");
      continue;
    }
    if (!in_mode7)
      continue;
    char *equals = strchr(s, '=');
    if (!equals)
      continue;
    *equals++ = 0;
    char *key = HdTrim(s), *relative = HdTrim(equals);
    if (tolower((unsigned char)key[0]) != 't' || tolower((unsigned char)key[1]) != 'i' ||
        tolower((unsigned char)key[2]) != 'l' || tolower((unsigned char)key[3]) != 'e' || !key[4])
      continue;
    char *end;
    long tile = strtol(key + 4, &end, 10);
    if (*end || tile < 0 || tile >= kHdTextureTileCount || !HdSafeRelativePath(relative))
      continue;
    char folder_path[256], image_path[512];
    snprintf(folder_path, sizeof(folder_path), "hd_textures/%s", g_packs[index].folder);
    HdBuildPath(image_path, sizeof(image_path), folder_path, relative);
    if (!HdReadTilePngSize(image_path))
      continue;
    int width = 0, height = 0, channels = 0;
    uint8_t *rgba = stbi_load(image_path, &width, &height, &channels, 4);
    if (!rgba || width != kHdTextureTileSize || height != kHdTextureTileSize) {
      if (rgba) stbi_image_free(rgba);
      continue;
    }
    uint32_t *pixels = (uint32_t*)malloc(kHdTextureTileSize * kHdTextureTileSize * sizeof(uint32_t));
    if (!pixels) {
      stbi_image_free(rgba);
      fclose(f);
      return false;
    }
    for (int i = 0; i < kHdTextureTileSize * kHdTextureTileSize; i++)
      pixels[i] = (uint32_t)rgba[i * 4 + 3] << 24 | (uint32_t)rgba[i * 4] << 16 |
                  (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
    stbi_image_free(rgba);
    free(tiles[tile]);
    tiles[tile] = pixels;
    loaded++;
  }
  fclose(f);
  *loaded_count = loaded;
  return true;
}

void HdTexturePack_Init(Ppu *ppu) {
  HdClearActive();
  for (int i = 0; i < g_pack_count; i++)
    free(g_packs[i].preview);
  memset(g_packs, 0, sizeof(g_packs));
  g_pack_count = 0;
  g_ppu = ppu;
  if (g_ppu)
    PpuClearMode7TextureTiles(g_ppu);
  g_pack_count = HdParseCatalog();
  for (int i = 0; i < g_pack_count; i++)
    HdLoadPreview(&g_packs[i]);
  snprintf(g_status, sizeof(g_status), g_pack_count ? "%d packs HD detectes" : "Aucun pack HD installe", g_pack_count);
}

int HdTexturePack_Count(void) {
  return g_pack_count;
}

int HdTexturePack_Selected(void) {
  return g_selected;
}

const char *HdTexturePack_Folder(int index) {
  return index >= 0 && index < g_pack_count ? g_packs[index].folder : NULL;
}

int HdTexturePack_FindFolder(const char *folder) {
  if (!folder || !*folder)
    return -1;
  for (int i = 0; i < g_pack_count; i++)
    if (HdEqualsNoCase(g_packs[i].folder, folder))
      return i;
  return -1;
}

const char *HdTexturePack_Name(int index) {
  if (index == -1)
    return "DESACTIVE";
  return index >= 0 && index < g_pack_count ? g_packs[index].name : "INCONNU";
}

const char *HdTexturePack_Description(int index) {
  return index >= 0 && index < g_pack_count ? g_packs[index].description : "";
}

const uint32_t *HdTexturePack_Preview(int index, int *width, int *height) {
  if (width) *width = index >= 0 && index < g_pack_count && g_packs[index].preview ? kHdTexturePreviewWidth : 0;
  if (height) *height = index >= 0 && index < g_pack_count && g_packs[index].preview ? kHdTexturePreviewHeight : 0;
  return index >= 0 && index < g_pack_count ? g_packs[index].preview : NULL;
}

bool HdTexturePack_Select(int index) {
  if (index == -1) {
    HdClearActive();
    snprintf(g_status, sizeof(g_status), "Pack HD desactive");
    return true;
  }
  if (index < 0 || index >= g_pack_count) {
    snprintf(g_status, sizeof(g_status), "Index de pack HD invalide");
    return false;
  }

  uint32_t *staged[kHdTextureTileCount] = { 0 };
  int loaded_count = 0;
  if (!HdParseAndLoadTiles(index, staged, &loaded_count)) {
    for (int i = 0; i < kHdTextureTileCount; i++) free(staged[i]);
    snprintf(g_status, sizeof(g_status), "Manifest HD illisible");
    return false;
  }
  if (loaded_count == 0) {
    for (int i = 0; i < kHdTextureTileCount; i++) free(staged[i]);
    snprintf(g_status, sizeof(g_status), "%s: aucune tuile PNG valide; pack actif conserve", g_packs[index].name);
    return false;
  }
  HdClearActive();
  for (int i = 0; i < kHdTextureTileCount; i++) {
    g_tiles[i] = staged[i];
    if (g_tiles[i] && g_ppu)
      PpuSetMode7TextureTile(g_ppu, i, g_tiles[i]);
  }
  g_selected = index;
  g_enabled = loaded_count != 0;
  snprintf(g_status, sizeof(g_status), g_enabled ? "%s: %d tuiles Mode 7 chargees" : "%s: aucune tuile PNG valide",
           g_packs[index].name, loaded_count);
  return true;
}

bool HdTexturePack_Enabled(void) {
  return g_enabled;
}

const char *HdTexturePack_Status(void) {
  return g_status;
}

void HdTexturePack_Shutdown(void) {
  HdClearActive();
  for (int i = 0; i < g_pack_count; i++)
    free(g_packs[i].preview);
  memset(g_packs, 0, sizeof(g_packs));
  g_ppu = NULL;
  g_pack_count = 0;
}
