#ifndef SDFATFS_H
#define SDFATFS_H

#include "ff.h"
#include <stdint.h>

/**
 * Appends data to a file on the SD card, mounting the filesystem and
 * creating the file first if it doesn't already exist. Previously saved
 * content is never overwritten - new data is always added at the end.
 *
 * Returns FR_OK on success, or the FatFs error code on failure.
 */
FRESULT SDFatFS_SaveData(const char *filename, const void *data, UINT len);

/**
 * Convenience wrapper around SDFatFS_SaveData for a null-terminated string.
 */
FRESULT SDFatFS_SaveString(const char *filename, const char *str);

/**
 * Mounts the SD card, reads the given file line by line and prints its
 * contents (via printf, e.g. over the UART used for stdio).
 *
 * Returns FR_OK on success, or the FatFs error code on failure.
 */
FRESULT SDFatFS_PrintFile(const char *filename);

/**
 * Mounts the SD card and deletes the given file.
 *
 * Returns FR_OK on success, or the FatFs error code on failure
 * (e.g. FR_NO_FILE if it doesn't exist).
 */
FRESULT SDFatFS_DeleteFile(const char *filename);

/**
 * Mounts the SD card and prints the name and size of every entry in the
 * root directory.
 *
 * Returns FR_OK on success, or the FatFs error code on failure.
 */
FRESULT SDFatFS_ListFiles(void);

#endif /* SDFATFS_H */
