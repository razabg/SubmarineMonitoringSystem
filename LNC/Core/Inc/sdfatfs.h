#ifndef SDFATFS_H
#define SDFATFS_H

#include "ff.h"
#include <stdint.h>

/**
 * Creates the mutex guarding every SD card operation in this module.
 * Call once from main(), before osKernelStart() -- i.e. before any
 * task could possibly call into this module. Every function below
 * serializes on this lock, since s_fs/s_fil (this module's shared
 * file-system/file-object state) can now genuinely be touched from
 * more than one task: Monitor/Log/Event write on their own task
 * schedules, while Communication's RX task calls in too (via
 * log_on_frame()/event_on_frame() answering a query).
 */
void SDFatFS_Init(void);

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
 * Callback invoked once per line by SDFatFS_ForEachLine(), with the
 * trailing '\r'/'\n' already stripped. `ctx` is whatever was passed
 * to SDFatFS_ForEachLine() -- a plain pass-through, not touched here.
 */
typedef void (*SDFatFS_LineCallback)(const char *line, void *ctx);

/**
 * Mounts the SD card and reads the given file line by line, calling
 * `cb` once per line (trailing '\r'/'\n' stripped) instead of printing
 * it -- for a caller that needs to inspect/filter each line itself
 * (e.g. matching a timestamp against a requested range).
 *
 * Returns FR_OK on success (including "file has zero matching lines"
 * -- that's not a failure), or the FatFs error code on failure
 * (e.g. FR_NO_FILE if it doesn't exist).
 */
FRESULT SDFatFS_ForEachLine(const char *filename, SDFatFS_LineCallback cb, void *ctx);

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
