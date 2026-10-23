#ifndef ZELDA3_SAVE_METADATA_H_
#define ZELDA3_SAVE_METADATA_H_

#include <stdbool.h>
#include <stddef.h>

void SaveMetadata_Init(void);
const char *SaveMetadata_Label(int slot);
bool SaveMetadata_SetLabel(int slot, const char *label);
bool SaveMetadata_BackupBeforeOverwrite(int slot);
int SaveMetadata_BackupCount(int slot);
bool SaveMetadata_RestoreLatest(int slot);
bool SaveMetadata_LatestBackupDate(int slot, char *dst, size_t dst_size);

#endif
