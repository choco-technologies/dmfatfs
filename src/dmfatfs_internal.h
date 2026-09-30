/**
 * @file dmfatfs_internal.h
 * @brief Shared state and helpers of the dmfatfs implementation
 *
 * Deliberately does not include dmfatfs.h: the module's own API registration
 * objects are emitted by every translation unit that includes it with
 * DMOD_ENABLE_REGISTRATION defined, so only dmfatfs.c includes it.
 */
#ifndef DMFATFS_INTERNAL_H
#define DMFATFS_INTERNAL_H

#include "dmod.h"
#include "dmfsi.h"
#include "ff.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#define DMFATFS_CONTEXT_MAGIC   0x46415443u     /* 'FATC' */
#define DMFATFS_FILE_MAGIC      0x46415446u     /* 'FATF' */
#define DMFATFS_DIR_MAGIC       0x46415444u     /* 'FATD' */
#define DMFATFS_SECTOR_SIZE     512u

/** One mounted FAT volume - the dmfsi context handed out by _init(). */
struct dmfsi_context
{
    uint32_t    magic;
    int         volume;     /**< FatFs logical drive (= physical drive) number */
    char*       device;     /**< Owned copy of the block device path */
    FATFS       fs;         /**< FatFs work area of the volume */
};

/** Open file - the fp handle of the dmfsi file operations. */
typedef struct
{
    uint32_t    magic;
    bool        append;     /**< DMFSI_O_APPEND: every write goes to the end */
    FIL         fil;
} dmfatfs_file_t;

/** Open directory - the dp handle of the dmfsi directory operations. */
typedef struct
{
    uint32_t    magic;
    DIR         dir;
    FILINFO     info;       /**< Entry buffer of _readdir() */
} dmfatfs_dir_t;

/* ---- context (dmfatfs.c) ---- */

bool dmfatfs_context_valid(dmfsi_context_t ctx);

/* ---- disk access (dmfatfs_disk.c) ---- */

/** Bind a free FatFs drive to @p device_path. Drive number, or a negative errno. */
int  dmfatfs_disk_attach(const char* device_path);

/** Close the device of @p drive and free the drive. */
void dmfatfs_disk_detach(int drive);

/* ---- system hooks (dmfatfs_system.c) ---- */

bool dmfatfs_system_init(void);
void dmfatfs_system_deinit(void);

/** Module-wide lock (the drive table). Recursive. */
void dmfatfs_lock(void);
void dmfatfs_unlock(void);

/* ---- helpers (dmfatfs_util.c) ---- */

/** dmfsi error code (DMFSI_*) of a FatFs result. */
int   dmfatfs_to_dmfsi(FRESULT result);

/** Negative errno of a FatFs result (0 for FR_OK). */
int   dmfatfs_to_errno(FRESULT result);

/** "<drive>:<path>" on the heap - the path FatFs expects. NULL on allocation failure. */
char* dmfatfs_make_path(int drive, const char* path);

/** True for the root directory of a mount ("/", "" or NULL). */
bool  dmfatfs_is_root(const char* path);

/** Seconds since 1970-01-01 of a FAT date and time. */
uint32_t dmfatfs_fat_to_unix(WORD fdate, WORD ftime);

/** FAT date (high 16 bits) and time (low 16 bits) of seconds since 1970-01-01. */
uint32_t dmfatfs_unix_to_fat(uint32_t seconds);

/** dmfsi attributes (DMFSI_ATTR_*) of FatFs attributes (AM_*). */
uint32_t dmfatfs_attr_to_dmfsi(BYTE attr);

/**
 * Block device path from the mount configuration: "device=<path>" (also
 * "dev=<path>"), entries separated by ';' or ',', or just "<path>".
 * Heap-owned, NULL if missing or on allocation failure.
 */
char* dmfatfs_parse_device(const char* config);

/**
 * Absolute form of a device path (relative to the caller's working
 * directory), without "//", "." and ".." components. Heap-owned, NULL on
 * failure.
 */
char* dmfatfs_canonical_path(const char* path);

/**
 * True if two canonical device paths can refer to the same sectors: the
 * same node, or a whole device and one of its partitions - dmdevfs names
 * partition nodes "<device>p<number>" (e.g. /dev/dmsdio0/0 and
 * /dev/dmsdio0/0p1). Different partitions of one device do not overlap.
 */
bool  dmfatfs_paths_overlap(const char* a, const char* b);

#endif // DMFATFS_INTERNAL_H
