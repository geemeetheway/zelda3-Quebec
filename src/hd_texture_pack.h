#ifndef ZELDA3_HD_TEXTURE_PACK_H_
#define ZELDA3_HD_TEXTURE_PACK_H_

#include <stdbool.h>
#include "snes/ppu.h"

void HdTexturePack_Init(Ppu *ppu);
int HdTexturePack_Count(void);
int HdTexturePack_Selected(void);
const char *HdTexturePack_Folder(int index);
int HdTexturePack_FindFolder(const char *folder);
const char *HdTexturePack_Name(int index);
const char *HdTexturePack_Description(int index);
const uint32_t *HdTexturePack_Preview(int index, int *width, int *height);
bool HdTexturePack_Select(int index);
bool HdTexturePack_Enabled(void);
const char *HdTexturePack_Status(void);
void HdTexturePack_Shutdown(void);

#endif
