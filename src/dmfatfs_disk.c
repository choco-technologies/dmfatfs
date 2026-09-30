/**
 * @file dmfatfs_disk.c
 * @brief FatFs media access layer (diskio.h) on top of the DMOD file API
 *
 * Every FatFs drive is backed by a file opened with Dmod_FileOpen(): on
 * target a dmdevfs block node (e.g. /dev/dmsdio0/0 or a partition node such
 * as /dev/dmsdio0/0p1), on the host an image file. FatFs drive numbers are
 * handed out by dmfatfs_disk_attach() and map 1:1 to logical volumes.
 *
 * FatFs locks a volume for the whole duration of each of its calls, so the
 * disk_* functions of one drive are never entered concurrently - only the
 * drive table itself needs the module lock.
 */
#include "dmfatfs_internal.h"
#include "diskio.h"
#include "dmdrvi_ioctl.h"
#include <string.h>

typedef struct
{
    void*       file;           /**< Device file, NULL for a free drive */
    char*       path;           /**< Owned copy of the device path */
    LBA_t       sector_count;
    DWORD       erase_sectors;  /**< Erase block in sectors, 1 if unknown */
    bool        read_only;
} disk_t;

static disk_t g_disks[FF_VOLUMES];

static disk_t* get_disk(BYTE pdrv)
{
    return (pdrv < FF_VOLUMES && g_disks[pdrv].file != NULL) ? &g_disks[pdrv] : NULL;
}

/* Block nodes report their geometry, image files (and other nodes) fall back to the file size. */
static int read_geometry(disk_t* disk, const char* device_path)
{
    dmdrvi_block_info_t info = { 0 };
    if (Dmod_Ioctl(disk->file, DMDRVI_IOCTL_BLOCK_GET_INFO, &info) == 0 && info.block_count != 0)
    {
        if (info.logical_block_size != DMFATFS_SECTOR_SIZE)
        {
            DMOD_LOG_ERROR("'%s': block size %u is not supported (only %u)\n",
                           device_path, (unsigned)info.logical_block_size, DMFATFS_SECTOR_SIZE);
            return -ENOTSUP;
        }
        uint32_t erase_sectors = info.erase_block_size / DMFATFS_SECTOR_SIZE;
        bool     power_of_2    = erase_sectors != 0 && (erase_sectors & (erase_sectors - 1u)) == 0;
        disk->sector_count  = (LBA_t)info.block_count;
        disk->erase_sectors = (power_of_2 && erase_sectors <= 32768u) ? erase_sectors : 1u;
        disk->read_only    |= (info.flags & DMDRVI_BLOCK_FLAG_READ_ONLY) != 0;
        return 0;
    }
    disk->erase_sectors = 1u;
    disk->sector_count  = (LBA_t)(Dmod_FileSize(disk->file) / DMFATFS_SECTOR_SIZE);
    return (disk->sector_count != 0) ? 0 : -EIO;
}

static int open_device(disk_t* disk, const char* device_path)
{
    disk->read_only = false;
    disk->file      = Dmod_FileOpen(device_path, "r+b");
    if (disk->file == NULL)
    {
        disk->read_only = true;
        disk->file      = Dmod_FileOpen(device_path, "rb");
    }
    if (disk->file == NULL)
    {
        DMOD_LOG_ERROR("Cannot open block device '%s'\n", device_path);
        return -ENOENT;
    }
    int ret = read_geometry(disk, device_path);
    if (ret != 0)
    {
        Dmod_FileClose(disk->file);
        disk->file = NULL;
    }
    return ret;
}

static void log_in_use(const char* device_path, const char* attached_path)
{
    if (strcmp(device_path, attached_path) == 0)
    {
        DMOD_LOG_ERROR("'%s' is already mounted or being formatted\n", device_path);
    }
    else
    {
        DMOD_LOG_ERROR("'%s' overlaps '%s', which is already mounted or being formatted\n",
                       device_path, attached_path);
    }
}

/*
 * Two FatFs drives writing the same sectors (the same node twice, or a whole
 * device and one of its partitions) would corrupt the medium: such a device
 * is refused while the other one is attached.
 */
static int reserve_drive(const char* device_path)
{
    int free_drive = -EBUSY;
    for (int drive = FF_VOLUMES - 1; drive >= 0; drive--)
    {
        if (g_disks[drive].file == NULL)
        {
            free_drive = drive;
        }
        else if (dmfatfs_paths_overlap(g_disks[drive].path, device_path))
        {
            log_in_use(device_path, g_disks[drive].path);
            return -EBUSY;
        }
    }
    if (free_drive < 0)
    {
        DMOD_LOG_ERROR("All %d FAT volumes are in use\n", FF_VOLUMES);
    }
    return free_drive;
}

int dmfatfs_disk_attach(const char* device_path)
{
    if (device_path == NULL)
    {
        return -EINVAL;
    }
    char* path = dmfatfs_canonical_path(device_path);
    if (path == NULL)
    {
        return (*device_path == '\0') ? -EINVAL : -ENOMEM;
    }
    dmfatfs_lock();
    int drive = reserve_drive(path);
    int ret   = (drive >= 0) ? open_device(&g_disks[drive], path) : drive;
    if (ret == 0)
    {
        g_disks[drive].path = path;
    }
    dmfatfs_unlock();
    if (ret != 0)
    {
        Dmod_Free(path);
    }
    return (ret == 0) ? drive : ret;
}

void dmfatfs_disk_detach(int drive)
{
    dmfatfs_lock();
    disk_t* disk = (drive >= 0) ? get_disk((BYTE)drive) : NULL;
    if (disk != NULL)
    {
        Dmod_FileClose(disk->file);
        Dmod_Free(disk->path);
        disk->file         = NULL;
        disk->path         = NULL;
        disk->sector_count = 0;
    }
    dmfatfs_unlock();
}

static bool seek_sector(disk_t* disk, LBA_t sector, UINT count)
{
    if (sector >= disk->sector_count || count > disk->sector_count - sector)
    {
        return false;
    }
    return Dmod_FileSeek(disk->file, (Dmod_FileOffset_t)sector * DMFATFS_SECTOR_SIZE, DMOD_SEEK_SET) == 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv);
}

DSTATUS disk_status(BYTE pdrv)
{
    disk_t* disk = get_disk(pdrv);
    if (disk == NULL)
    {
        return STA_NOINIT;
    }
    return disk->read_only ? STA_PROTECT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    disk_t* disk = get_disk(pdrv);
    if (disk == NULL)
    {
        return RES_NOTRDY;
    }
    if (!seek_sector(disk, sector, count))
    {
        return RES_PARERR;
    }
    size_t size = (size_t)count * DMFATFS_SECTOR_SIZE;
    return (Dmod_FileRead(buff, 1, size, disk->file) == size) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    disk_t* disk = get_disk(pdrv);
    if (disk == NULL)
    {
        return RES_NOTRDY;
    }
    if (disk->read_only)
    {
        return RES_WRPRT;
    }
    if (!seek_sector(disk, sector, count))
    {
        return RES_PARERR;
    }
    size_t size = (size_t)count * DMFATFS_SECTOR_SIZE;
    return (Dmod_FileWrite(buff, 1, size, disk->file) == size) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    disk_t* disk = get_disk(pdrv);
    if (disk == NULL)
    {
        return RES_NOTRDY;
    }
    switch (cmd)
    {
        /* Block nodes write through to the medium - nothing is cached below FatFs. */
        case CTRL_SYNC:         return RES_OK;
        case GET_SECTOR_COUNT:  *(LBA_t*)buff = disk->sector_count;         return RES_OK;
        case GET_SECTOR_SIZE:   *(WORD*)buff  = DMFATFS_SECTOR_SIZE;        return RES_OK;
        /* f_mkfs() aligns the data area to it */
        case GET_BLOCK_SIZE:    *(DWORD*)buff = disk->erase_sectors;        return RES_OK;
        default:                return RES_PARERR;
    }
}
