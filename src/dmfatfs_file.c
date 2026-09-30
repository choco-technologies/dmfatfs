/**
 * @file dmfatfs_file.c
 * @brief dmfsi file operations of dmfatfs
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmfatfs_internal.h"
#include <string.h>

#define MAX_TRANSFER    ((size_t)(UINT)-1)

static dmfatfs_file_t* get_file(dmfsi_context_t ctx, void* fp)
{
    dmfatfs_file_t* file = (dmfatfs_file_t*)fp;
    if (!dmfatfs_context_valid(ctx) || file == NULL || file->magic != DMFATFS_FILE_MAGIC)
    {
        return NULL;
    }
    return file;
}

static BYTE open_mode(int mode)
{
    BYTE access;
    switch (mode & DMFSI_O_RDWR)
    {
        case DMFSI_O_WRONLY:    access = FA_WRITE;              break;
        case DMFSI_O_RDWR:      access = FA_READ | FA_WRITE;    break;
        default:                access = FA_READ;               break;
    }
    if (mode & DMFSI_O_CREAT)
    {
        access |= (mode & DMFSI_O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    }
    return access;
}

/* O_TRUNC without O_CREAT and O_APPEND are not open modes of FatFs. */
static FRESULT apply_open_flags(dmfatfs_file_t* file, int mode)
{
    FRESULT result = FR_OK;
    if ((mode & DMFSI_O_TRUNC) && !(mode & DMFSI_O_CREAT) && (mode & DMFSI_O_RDWR) != DMFSI_O_RDONLY)
    {
        result = f_truncate(&file->fil);
    }
    file->append = (mode & DMFSI_O_APPEND) != 0;
    if (result == FR_OK && file->append)
    {
        result = f_lseek(&file->fil, f_size(&file->fil));
    }
    return result;
}

static FRESULT open_file(dmfsi_context_t ctx, dmfatfs_file_t* file, const char* path, int mode)
{
    char* fat_path = dmfatfs_make_path(ctx->volume, path);
    if (fat_path == NULL)
    {
        return FR_NOT_ENOUGH_CORE;
    }
    FRESULT result = f_open(&file->fil, fat_path, open_mode(mode));
    Dmod_Free(fat_path);
    if (result != FR_OK)
    {
        return result;
    }
    result = apply_open_flags(file, mode);
    if (result != FR_OK)
    {
        f_close(&file->fil);
    }
    return result;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    (void)attr;
    if (!dmfatfs_context_valid(ctx) || fp == NULL || path == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    dmfatfs_file_t* file = Dmod_Malloc(sizeof(dmfatfs_file_t));
    if (file == NULL)
    {
        return DMFSI_ERR_GENERAL;
    }
    memset(file, 0, sizeof(*file));
    FRESULT result = open_file(ctx, file, path, mode);
    if (result != FR_OK)
    {
        Dmod_Free(file);
        return dmfatfs_to_dmfsi(result);
    }
    file->magic = DMFATFS_FILE_MAGIC;
    *fp = file;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    FRESULT result = f_close(&file->fil);
    file->magic = 0;
    Dmod_Free(file);
    return dmfatfs_to_dmfsi(result);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL || (buffer == NULL && size != 0))
    {
        return DMFSI_ERR_INVALID;
    }
    UINT    done   = 0;
    FRESULT result = f_read(&file->fil, buffer, (UINT)((size < MAX_TRANSFER) ? size : MAX_TRANSFER), &done);
    if (read != NULL)
    {
        *read = done;
    }
    return dmfatfs_to_dmfsi(result);
}

static int write_file(dmfatfs_file_t* file, const void* buffer, size_t size, size_t* written)
{
    UINT    done   = 0;
    FRESULT result = file->append ? f_lseek(&file->fil, f_size(&file->fil)) : FR_OK;
    if (result == FR_OK)
    {
        result = f_write(&file->fil, buffer, (UINT)((size < MAX_TRANSFER) ? size : MAX_TRANSFER), &done);
    }
    if (written != NULL)
    {
        *written = done;
    }
    return dmfatfs_to_dmfsi(result);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL || (buffer == NULL && size != 0))
    {
        return DMFSI_ERR_INVALID;
    }
    return write_file(file, buffer, size, written);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmfatfs, dmfsi_offset_t, _lseek, (dmfsi_context_t ctx, void* fp, dmfsi_offset_t offset, int whence) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    dmfsi_offset_t base;
    switch (whence)
    {
        case DMFSI_SEEK_SET:    base = 0;                                   break;
        case DMFSI_SEEK_CUR:    base = (dmfsi_offset_t)f_tell(&file->fil);  break;
        case DMFSI_SEEK_END:    base = (dmfsi_offset_t)f_size(&file->fil);  break;
        default:                return DMFSI_ERR_INVALID;
    }
    if (offset < 0 && -offset > base)
    {
        return DMFSI_ERR_INVALID;
    }
    /* Beyond the end, a file open for writing is expanded, one open for reading is not. */
    FRESULT result = f_lseek(&file->fil, (FSIZE_t)(base + offset));
    return (result == FR_OK) ? (dmfsi_offset_t)f_tell(&file->fil) : dmfatfs_to_dmfsi(result);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    (void)request;
    (void)arg;
    return (get_file(ctx, fp) == NULL) ? DMFSI_ERR_INVALID : DMFSI_ERR_GENERAL;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? DMFSI_ERR_INVALID : dmfatfs_to_dmfsi(f_sync(&file->fil));
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? DMFSI_ERR_INVALID : dmfatfs_to_dmfsi(f_sync(&file->fil));
}

/* Next byte, -1 at the end of the file (or on error) */
dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    unsigned char c    = 0;
    UINT          done = 0;
    FRESULT result = f_read(&file->fil, &c, 1, &done);
    return (result == FR_OK && done == 1) ? (int)c : -1;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    if (file == NULL)
    {
        return DMFSI_ERR_INVALID;
    }
    unsigned char byte    = (unsigned char)c;
    size_t        written = 0;
    int result = write_file(file, &byte, 1, &written);
    if (result != DMFSI_OK)
    {
        return result;
    }
    return (written == 1) ? (int)byte : DMFSI_ERR_NO_SPACE;
}

dmod_dmfsi_dif_api_declaration( 2.0, dmfatfs, dmfsi_offset_t, _tell, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? DMFSI_ERR_INVALID : (dmfsi_offset_t)f_tell(&file->fil);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? DMFSI_ERR_INVALID : (f_eof(&file->fil) ? 1 : 0);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, dmfsi_size_t, _size, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? 0 : (dmfsi_size_t)f_size(&file->fil);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmfatfs, int, _error, (dmfsi_context_t ctx, void* fp) )
{
    dmfatfs_file_t* file = get_file(ctx, fp);
    return (file == NULL) ? DMFSI_ERR_INVALID : (f_error(&file->fil) ? DMFSI_ERR_GENERAL : DMFSI_OK);
}
