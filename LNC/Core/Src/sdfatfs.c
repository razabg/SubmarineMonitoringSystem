#include "sdfatfs.h"
#include <string.h>
#include <stdio.h>

/* FATFS and FIL each carry a 512-byte sector buffer (_MAX_SS in ffconf.h),
   so keeping them as plain locals overflows a normal RTOS task stack the
   moment these functions are entered. Static storage keeps them off the
   stack entirely - safe here since this module only ever has one volume
   mounted and one file open at a time. */
static FATFS s_fs;
static FIL s_fil;

static FRESULT sd_mount(void)
{
    return f_mount(&s_fs, "", 1);
}

static void sd_unmount(void)
{
    f_mount(NULL, "", 0);
}

FRESULT SDFatFS_SaveData(const char *filename, const void *data, UINT len)
{
    UINT written;
    FRESULT fres;

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        return fres;
    }

    /* FA_OPEN_APPEND creates the file if it's missing and seeks to its
       end if it already exists, so earlier saved data is kept intact. */
    fres = f_open(&s_fil, filename, FA_WRITE | FA_OPEN_APPEND);
    if (fres != FR_OK) {
        printf("SD open failed (%i)\r\n", fres);
        sd_unmount();
        return fres;
    }

    fres = f_write(&s_fil, data, len, &written);
    f_close(&s_fil);
    sd_unmount();

    if (fres != FR_OK) {
        printf("SD write failed (%i)\r\n", fres);
        return fres;
    }

    printf("Wrote %u bytes to %s\r\n", written, filename);
    return FR_OK;
}

FRESULT SDFatFS_SaveString(const char *filename, const char *str)
{
    return SDFatFS_SaveData(filename, str, (UINT)strlen(str));
}

FRESULT SDFatFS_PrintFile(const char *filename)
{
    char line[128];
    FRESULT fres;

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        return fres;
    }

    fres = f_open(&s_fil, filename, FA_READ);
    if (fres != FR_OK) {
        printf("SD open failed (%i)\r\n", fres);
        sd_unmount();
        return fres;
    }

    printf("---- %s ----\r\n", filename);
    while (f_gets(line, sizeof(line), &s_fil)) {
        printf("%s", line);
    }

    f_close(&s_fil);
    sd_unmount();
    return FR_OK;
}

FRESULT SDFatFS_DeleteFile(const char *filename)
{
    FRESULT fres;

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        return fres;
    }

    fres = f_unlink(filename);
    sd_unmount();

    if (fres != FR_OK) {
        printf("SD delete failed (%i)\r\n", fres);
        return fres;
    }

    printf("Deleted %s\r\n", filename);
    return FR_OK;
}

FRESULT SDFatFS_ListFiles(void)
{
    DIR dir;
    FILINFO info;
    FRESULT fres;

    fres = sd_mount();
    if (fres != FR_OK) {
        printf("SD mount failed (%i)\r\n", fres);
        return fres;
    }

    fres = f_opendir(&dir, "/");
    if (fres != FR_OK) {
        printf("SD opendir failed (%i)\r\n", fres);
        sd_unmount();
        return fres;
    }

    printf("---- SD card contents ----\r\n");
    for (;;) {
        fres = f_readdir(&dir, &info);
        if (fres != FR_OK || info.fname[0] == 0) {
            break;
        }
        printf("%-13s %10lu bytes\r\n", info.fname, (unsigned long)info.fsize);
    }

    f_closedir(&dir);
    sd_unmount();
    return fres;
}
