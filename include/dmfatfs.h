#ifndef DMFATFS_H
#define DMFATFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmfatfs_defs.h"

/**
 * dmfatfs - FAT12/FAT16/FAT32/exFAT file system for DMOD, based on FatFs.
 *
 * The file system itself is used through dmvfs - dmfatfs implements the
 * dmfsi DIF, so it is mounted like any other file system:
 *
 *     dmvfs_mount_fs("dmfatfs", "/mnt/sd", "device=/dev/dmsdio0/0");
 *
 * The functions below are the module's own API, for work that happens
 * outside of a mount (e.g. formatting a device before it can be mounted).
 */

/** File system created by dmfatfs_mkfs(). */
typedef enum
{
    dmfatfs_type_auto = 0,  /**< FAT12/16/32 by volume size, exFAT from 32 GiB up */
    dmfatfs_type_fat,       /**< FAT12 or FAT16, by volume size */
    dmfatfs_type_fat32,     /**< FAT32 */
    dmfatfs_type_exfat,     /**< exFAT */
} dmfatfs_type_t;

/** Options of dmfatfs_mkfs(). Zero-initialized options are the defaults. */
typedef struct
{
    dmfatfs_type_t  type;           /**< File system type */
    bool            no_partition;   /**< true: file system directly on the device (no MBR/GPT) */
    uint32_t        cluster_size;   /**< Cluster size in bytes, 0 for the default of the volume size */
} dmfatfs_mkfs_options_t;

/**
 * Create a new FAT file system on a block device (or an image file).
 *
 * All data on the device is lost. The device must not be mounted. By
 * default a partition table with a single partition is created (like a
 * factory formatted SD card); either way, the device node itself can be
 * mounted afterwards, dmfatfs finds the file system in the first partition.
 *
 * @param device_path Path of the device, e.g. "/dev/dmsdio0/0"
 * @param options     Options, or NULL for the defaults
 * @return 0 on success, negative errno on failure
 */
dmod_dmfatfs_api(1.0, int, _mkfs, ( const char* device_path, const dmfatfs_mkfs_options_t* options ));

#endif // DMFATFS_H
