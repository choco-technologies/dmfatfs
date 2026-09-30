/**
 * @file dmfatfs.c
 * @brief dmfatfs module: mount contexts (dmfsi _init/_deinit) and dmfatfs_mkfs()
 *
 * The dmfsi file and directory operations live in dmfatfs_file.c and
 * dmfatfs_dir.c. This is the only translation unit that emits the DIF
 * signatures (ENABLE_DIF_REGISTRATIONS) and the module's own API
 * registrations (dmfatfs.h).
 */
#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmfatfs_internal.h"
#include "dmfatfs.h"
#include <string.h>

/* f_mkfs() working buffer - bigger is faster, FF_MAX_SS is the minimum */
#define MKFS_WORK_SIZE      (8u * DMFATFS_SECTOR_SIZE)

/* FatFs volume prefix: "<drive>:" */
#define DRIVE_PREFIX_SIZE   3u

static void drive_prefix(int drive, char prefix[DRIVE_PREFIX_SIZE])
{
    prefix[0] = (char)('0' + drive);
    prefix[1] = ':';
    prefix[2] = '\0';
}

static const char* fat_type_name(BYTE fs_type)
{
    switch (fs_type)
    {
        case FS_FAT12:  return "FAT12";
        case FS_FAT16:  return "FAT16";
        case FS_FAT32:  return "FAT32";
        case FS_EXFAT:  return "exFAT";
        default:        return "unknown";
    }
}

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    if (!dmfatfs_system_init())
    {
        DMOD_LOG_ERROR("Cannot create the dmfatfs lock\n");
        return -1;
    }
    return 0;
}

int dmod_deinit(void)
{
    dmfatfs_system_deinit();
    return 0;
}

bool dmfatfs_context_valid(dmfsi_context_t ctx)
{
    return ctx != NULL && ctx->magic == DMFATFS_CONTEXT_MAGIC;
}

/* Register the FatFs work area of the context and mount the volume right away. */
static FRESULT mount_volume(dmfsi_context_t ctx)
{
    char prefix[DRIVE_PREFIX_SIZE];
    drive_prefix(ctx->volume, prefix);
    FRESULT result = f_mount(&ctx->fs, prefix, 1);
    if (result != FR_OK)
    {
        f_mount(NULL, prefix, 0);
    }
    return result;
}

static void unmount_volume(dmfsi_context_t ctx)
{
    char prefix[DRIVE_PREFIX_SIZE];
    drive_prefix(ctx->volume, prefix);
    f_mount(NULL, prefix, 0);
}

static dmfsi_context_t context_create(char* device)
{
    dmfsi_context_t ctx = Dmod_Malloc(sizeof(struct dmfsi_context));
    if (ctx == NULL)
    {
        DMOD_LOG_ERROR("Cannot allocate the dmfatfs context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->device = device;
    ctx->volume = dmfatfs_disk_attach(device);
    if (ctx->volume < 0)
    {
        Dmod_Free(ctx);
        return NULL;
    }
    FRESULT result = mount_volume(ctx);
    if (result != FR_OK)
    {
        DMOD_LOG_ERROR("No FAT file system on '%s' (FatFs error %d)\n", device, (int)result);
        dmfatfs_disk_detach(ctx->volume);
        Dmod_Free(ctx);
        return NULL;
    }
    ctx->magic = DMFATFS_CONTEXT_MAGIC;
    return ctx;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, dmfsi_context_t, _init, (const char* config) )
{
    char* device = dmfatfs_parse_device(config);
    if (device == NULL)
    {
        DMOD_LOG_ERROR("dmfatfs needs a block device: \"device=/dev/<node>\" (got '%s')\n",
                       (config != NULL) ? config : "");
        return NULL;
    }
    dmfsi_context_t ctx = context_create(device);
    if (ctx == NULL)
    {
        Dmod_Free(device);
        return NULL;
    }
    DMOD_LOG_INFO("Mounted %s volume from '%s'\n", fat_type_name(ctx->fs.fs_type), device);
    return ctx;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _deinit, (dmfsi_context_t ctx) )
{
    if (!dmfatfs_context_valid(ctx))
    {
        return DMFSI_ERR_INVALID;
    }
    unmount_volume(ctx);
    dmfatfs_disk_detach(ctx->volume);
    ctx->magic = 0;
    Dmod_Free(ctx->device);
    Dmod_Free(ctx);
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return dmfatfs_context_valid(ctx) ? 1 : 0;
}

static bool mkfs_parameters(const dmfatfs_mkfs_options_t* options, MKFS_PARM* parm)
{
    memset(parm, 0, sizeof(*parm));
    switch (options->type)
    {
        case dmfatfs_type_auto:     parm->fmt = FM_ANY;     break;
        case dmfatfs_type_fat:      parm->fmt = FM_FAT;     break;
        case dmfatfs_type_fat32:    parm->fmt = FM_FAT32;   break;
        case dmfatfs_type_exfat:    parm->fmt = FM_EXFAT;   break;
        default:                    return false;
    }
    parm->fmt    |= options->no_partition ? FM_SFD : 0;
    parm->au_size = options->cluster_size;
    return true;
}

static int format_drive(int drive, const MKFS_PARM* parm)
{
    void* work = Dmod_Malloc(MKFS_WORK_SIZE);
    if (work == NULL)
    {
        return -ENOMEM;
    }
    char prefix[DRIVE_PREFIX_SIZE];
    drive_prefix(drive, prefix);
    FRESULT result = f_mkfs(prefix, parm, work, MKFS_WORK_SIZE);
    Dmod_Free(work);
    if (result != FR_OK)
    {
        DMOD_LOG_ERROR("Formatting failed (FatFs error %d)\n", (int)result);
    }
    return dmfatfs_to_errno(result);
}

dmod_dmfatfs_api_declaration(1.0, int, _mkfs, ( const char* device_path, const dmfatfs_mkfs_options_t* options ))
{
    static const dmfatfs_mkfs_options_t defaults = { 0 };
    MKFS_PARM parm;
    if (device_path == NULL || !mkfs_parameters((options != NULL) ? options : &defaults, &parm))
    {
        return -EINVAL;
    }
    int drive = dmfatfs_disk_attach(device_path);
    if (drive < 0)
    {
        return drive;
    }
    int ret = format_drive(drive, &parm);
    dmfatfs_disk_detach(drive);
    return ret;
}
