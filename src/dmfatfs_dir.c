/**
 * @file dmfatfs_dir.c
 * @brief dmfsi directory and path operations of dmfatfs
 *
 * FILINFO holds a full long file name, so it is kept on the heap (or in the
 * directory handle) rather than on the caller's stack.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmfatfs_internal.h"
#include <string.h>

#define WRITE_PERMISSIONS   0222

static dmfatfs_dir_t* get_dir(dmfsi_context_t ctx, void* dp)
{
    dmfatfs_dir_t* dir = (dmfatfs_dir_t*)dp;
    if (!dmfatfs_context_valid(ctx) || dir == NULL || dir->magic != DMFATFS_DIR_MAGIC)
    {
        return NULL;
    }
    return dir;
}

/* Run a FatFs call taking one path on the "<drive>:<path>" form of @p path. */
typedef FRESULT (*path_call_t)(const TCHAR* path, void* arg);

static FRESULT with_path(dmfsi_context_t ctx, const char* path, path_call_t call, void* arg)
{
    char* fat_path = dmfatfs_make_path(ctx->volume, path);
    if (fat_path == NULL)
    {
        return FR_NOT_ENOUGH_CORE;
    }
    FRESULT result = call(fat_path, arg);
    Dmod_Free(fat_path);
    return result;
}

static FRESULT call_stat(const TCHAR* path, void* arg)      { return f_stat(path, (FILINFO*)arg); }
static FRESULT call_opendir(const TCHAR* path, void* arg)   { return f_opendir((DIR*)arg, path); }
static FRESULT call_unlink(const TCHAR* path, void* arg)    { (void)arg; return f_unlink(path); }
static FRESULT call_mkdir(const TCHAR* path, void* arg)     { (void)arg; return f_mkdir(path); }
static FRESULT call_utime(const TCHAR* path, void* arg)     { return f_utime(path, (const FILINFO*)arg); }
static FRESULT call_chmod(const TCHAR* path, void* arg)     { return f_chmod(path, *(BYTE*)arg, AM_RDO); }

/* Heap-allocated FILINFO of @p path, NULL (and *result set) when it cannot be read. */
static FILINFO* stat_path(dmfsi_context_t ctx, const char* path, FRESULT* result)
{
    FILINFO* info = Dmod_Malloc(sizeof(FILINFO));
    if (info == NULL)
    {
        *result = FR_NOT_ENOUGH_CORE;
        return NULL;
    }
    *result = with_path(ctx, path, call_stat, info);
    if (*result != FR_OK)
    {
        Dmod_Free(info);
        return NULL;
    }
    return info;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    if (!dmfatfs_context_valid(ctx) || dp == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    dmfatfs_dir_t* dir = Dmod_Malloc(sizeof(dmfatfs_dir_t));
    if (dir == NULL)
    {
        return DMFSI_ERR_GENERAL;
    }
    memset(dir, 0, sizeof(*dir));
    FRESULT result = with_path(ctx, path, call_opendir, &dir->dir);
    if (result != FR_OK)
    {
        Dmod_Free(dir);
        return dmfatfs_to_dmfsi(result);
    }
    dir->magic = DMFATFS_DIR_MAGIC;
    *dp = dir;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    dmfatfs_dir_t* dir = get_dir(ctx, dp);
    if (dir == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    FRESULT result = f_closedir(&dir->dir);
    dir->magic = 0;
    Dmod_Free(dir);
    return dmfatfs_to_dmfsi(result);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmfatfs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    dmfatfs_dir_t* dir = get_dir(ctx, dp);
    if (dir == NULL || entry == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    FRESULT result = f_readdir(&dir->dir, &dir->info);
    if (result != FR_OK)
    {
        return dmfatfs_to_dmfsi(result);
    }
    if (dir->info.fname[0] == '\0')
    {
        return DMFSI_ERR_NOT_FOUND;
    }
    strncpy(entry->name, dir->info.fname, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->size = (dmfsi_size_t)dir->info.fsize;
    entry->attr = dmfatfs_attr_to_dmfsi(dir->info.fattrib);
    entry->time = dmfatfs_fat_to_unix(dir->info.fdate, dir->info.ftime);
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 2.0, dmfatfs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    if (!dmfatfs_context_valid(ctx) || stat == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    memset(stat, 0, sizeof(*stat));
    if (dmfatfs_is_root(path))
    {
        /* The root directory has no directory entry of its own. */
        stat->attr = DMFSI_ATTR_DIRECTORY;
        return DMFSI_OK;
    }
    FRESULT  result;
    FILINFO* info = stat_path(ctx, path, &result);
    if (info == NULL)
    {
        return dmfatfs_to_dmfsi(result);
    }
    stat->size  = (dmfsi_size_t)info->fsize;
    stat->attr  = dmfatfs_attr_to_dmfsi(info->fattrib);
    stat->mtime = dmfatfs_fat_to_unix(info->fdate, info->ftime);
    stat->ctime = stat->mtime;
    stat->atime = stat->mtime;
    Dmod_Free(info);
    return DMFSI_OK;
}

/* Removes files and empty directories (dmvfs routes rmdir here as well). */
dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    if (!dmfatfs_context_valid(ctx) || dmfatfs_is_root(path))
    {
        return DMFSI_ERR_INVALID;
    }
    FRESULT result = with_path(ctx, path, call_unlink, NULL);
    /* FR_DENIED: read-only, or a directory that is not empty */
    return (result == FR_DENIED) ? DMFSI_ERR_NOT_EMPTY : dmfatfs_to_dmfsi(result);
}

/* Replace an existing file at the destination, like POSIX rename(). */
static FRESULT rename_path(const char* old_path, const char* new_path)
{
    FRESULT result = f_rename(old_path, new_path);
    if (result != FR_EXIST)
    {
        return result;
    }
    FILINFO* info = Dmod_Malloc(sizeof(FILINFO));
    if (info == NULL)
    {
        return FR_NOT_ENOUGH_CORE;
    }
    result = f_stat(new_path, info);
    if (result == FR_OK)
    {
        result = (info->fattrib & AM_DIR) ? FR_EXIST : f_unlink(new_path);
    }
    Dmod_Free(info);
    return (result == FR_OK) ? f_rename(old_path, new_path) : result;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    if (!dmfatfs_context_valid(ctx) || oldpath == NULL || newpath == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    char* old_path = dmfatfs_make_path(ctx->volume, oldpath);
    char* new_path = dmfatfs_make_path(ctx->volume, newpath);
    FRESULT result = (old_path != NULL && new_path != NULL) ? rename_path(old_path, new_path) : FR_NOT_ENOUGH_CORE;
    if (old_path != NULL)
    {
        Dmod_Free(old_path);
    }
    if (new_path != NULL)
    {
        Dmod_Free(new_path);
    }
    return dmfatfs_to_dmfsi(result);
}

/* FAT only knows the read-only attribute: set when @p mode grants no write permission. */
dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _chmod, (dmfsi_context_t ctx, const char* path, int mode) )
{
    if (!dmfatfs_context_valid(ctx) || dmfatfs_is_root(path))
    {
        return DMFSI_ERR_INVALID;
    }
    BYTE attr = (mode & WRITE_PERMISSIONS) ? 0 : AM_RDO;
    return dmfatfs_to_dmfsi(with_path(ctx, path, call_chmod, &attr));
}

/* FAT keeps a single modification time - atime is not stored. */
dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _utime, (dmfsi_context_t ctx, const char* path, uint32_t atime, uint32_t mtime) )
{
    (void)atime;
    if (!dmfatfs_context_valid(ctx) || dmfatfs_is_root(path))
    {
        return DMFSI_ERR_INVALID;
    }
    FILINFO* info = Dmod_Malloc(sizeof(FILINFO));
    if (info == NULL)
    {
        return DMFSI_ERR_GENERAL;
    }
    uint32_t fat_time = dmfatfs_unix_to_fat(mtime);
    info->fdate = (WORD)(fat_time >> 16);
    info->ftime = (WORD)(fat_time & 0xFFFFu);
    FRESULT result = with_path(ctx, path, call_utime, info);
    Dmod_Free(info);
    return dmfatfs_to_dmfsi(result);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _mkdir, (dmfsi_context_t ctx, const char* path, int mode) )
{
    (void)mode;
    if (!dmfatfs_context_valid(ctx) || dmfatfs_is_root(path))
    {
        return DMFSI_ERR_INVALID;
    }
    return dmfatfs_to_dmfsi(with_path(ctx, path, call_mkdir, NULL));
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    if (!dmfatfs_context_valid(ctx))
    {
        return DMFSI_ERR_INVALID;
    }
    if (dmfatfs_is_root(path))
    {
        return 1;
    }
    FRESULT  result;
    FILINFO* info = stat_path(ctx, path, &result);
    if (info == NULL)
    {
        return (result == FR_NO_FILE || result == FR_NO_PATH) ? 0 : dmfatfs_to_dmfsi(result);
    }
    int exists = (info->fattrib & AM_DIR) ? 1 : 0;
    Dmod_Free(info);
    return exists;
}
