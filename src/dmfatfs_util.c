/**
 * @file dmfatfs_util.c
 * @brief Conversions between FatFs and dmfsi/DMOD conventions
 */
#include "dmfatfs_internal.h"
#include <string.h>

#define SECONDS_PER_DAY     86400u
#define FAT_EPOCH_YEAR      1980
#define FAT_MAX_YEAR        2107

int dmfatfs_to_dmfsi(FRESULT result)
{
    switch (result)
    {
        case FR_OK:                 return DMFSI_OK;
        case FR_NO_FILE:
        case FR_NO_PATH:            return DMFSI_ERR_NOT_FOUND;
        case FR_EXIST:              return DMFSI_ERR_EXISTS;
        case FR_INVALID_NAME:
        case FR_INVALID_OBJECT:
        case FR_INVALID_DRIVE:
        case FR_INVALID_PARAMETER:  return DMFSI_ERR_INVALID;
        default:                    return DMFSI_ERR_GENERAL;
    }
}

int dmfatfs_to_errno(FRESULT result)
{
    switch (result)
    {
        case FR_OK:                 return 0;
        case FR_NO_FILE:
        case FR_NO_PATH:            return -ENOENT;
        case FR_EXIST:              return -EEXIST;
        case FR_DENIED:             return -EACCES;
        case FR_WRITE_PROTECTED:    return -EROFS;
        case FR_NOT_ENOUGH_CORE:    return -ENOMEM;
        case FR_MKFS_ABORTED:       return -ENOSPC;
        case FR_NO_FILESYSTEM:      return -ENODEV;
        case FR_TIMEOUT:
        case FR_LOCKED:             return -EBUSY;
        case FR_INVALID_NAME:
        case FR_INVALID_OBJECT:
        case FR_INVALID_DRIVE:
        case FR_INVALID_PARAMETER:  return -EINVAL;
        default:                    return -EIO;
    }
}

bool dmfatfs_is_root(const char* path)
{
    if (path == NULL)
    {
        return true;
    }
    while (*path == '/')
    {
        path++;
    }
    return *path == '\0';
}

char* dmfatfs_make_path(int drive, const char* path)
{
    if (path == NULL || *path == '\0')
    {
        path = "/";
    }
    size_t length = strlen(path);
    /* drive (single digit, FF_VOLUMES <= 10), ':', optional '/', path, terminator */
    char* result = Dmod_Malloc(length + 4);
    if (result == NULL)
    {
        return NULL;
    }
    result[0] = (char)('0' + drive);
    result[1] = ':';
    char* rest = &result[2];
    if (path[0] != '/')
    {
        *rest++ = '/';
    }
    memcpy(rest, path, length + 1);
    return result;
}

/* Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil). */
static int32_t days_from_civil(int32_t year, uint32_t month, uint32_t day)
{
    year -= (month <= 2) ? 1 : 0;
    int32_t  era = year / 400;
    uint32_t yoe = (uint32_t)(year - era * 400);
    uint32_t doy = (153u * (month + (month > 2 ? (uint32_t)-3 : 9u)) + 2u) / 5u + day - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

uint32_t dmfatfs_fat_to_unix(WORD fdate, WORD ftime)
{
    uint32_t month = (fdate >> 5) & 0x0Fu;
    uint32_t day   = fdate & 0x1Fu;
    if (month < 1 || month > 12 || day < 1)
    {
        return 0;
    }
    int32_t  days    = days_from_civil(FAT_EPOCH_YEAR + (fdate >> 9), month, day);
    uint32_t seconds = ((ftime >> 11) * 3600u) + (((ftime >> 5) & 0x3Fu) * 60u) + ((ftime & 0x1Fu) * 2u);
    return (uint32_t)days * SECONDS_PER_DAY + seconds;
}

uint32_t dmfatfs_unix_to_fat(uint32_t seconds)
{
    /* H. Hinnant's civil_from_days */
    uint32_t z   = seconds / SECONDS_PER_DAY + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp  = (5u * doy + 2u) / 153u;
    uint32_t day   = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t month = (mp < 10u) ? mp + 3u : mp - 9u;
    uint32_t year  = yoe + era * 400u + ((month <= 2u) ? 1u : 0u);

    year = (year < FAT_EPOCH_YEAR) ? FAT_EPOCH_YEAR : ((year > FAT_MAX_YEAR) ? FAT_MAX_YEAR : year);
    uint32_t in_day = seconds % SECONDS_PER_DAY;
    uint32_t fdate  = ((year - FAT_EPOCH_YEAR) << 9) | (month << 5) | day;
    uint32_t ftime  = ((in_day / 3600u) << 11) | (((in_day / 60u) % 60u) << 5) | ((in_day % 60u) / 2u);
    return (fdate << 16) | ftime;
}

uint32_t dmfatfs_attr_to_dmfsi(BYTE attr)
{
    uint32_t result = 0;
    result |= (attr & AM_RDO) ? DMFSI_ATTR_READONLY  : 0u;
    result |= (attr & AM_HID) ? DMFSI_ATTR_HIDDEN    : 0u;
    result |= (attr & AM_SYS) ? DMFSI_ATTR_SYSTEM    : 0u;
    result |= (attr & AM_DIR) ? DMFSI_ATTR_DIRECTORY : 0u;
    result |= (attr & AM_ARC) ? DMFSI_ATTR_ARCHIVE   : 0u;
    return result;
}

static bool is_separator(char c)
{
    return c == ';' || c == ',';
}

static bool key_matches(const char* key, size_t key_length, const char* expected)
{
    return strlen(expected) == key_length && strncmp(key, expected, key_length) == 0;
}

/* Value of the "device"/"dev" entry starting at @p entry, NULL for another key. */
static char* device_value(const char* entry, size_t length)
{
    size_t key_length = 0;
    while (key_length < length && entry[key_length] != '=')
    {
        key_length++;
    }
    if (key_length == length ||
        !(key_matches(entry, key_length, "device") || key_matches(entry, key_length, "dev")))
    {
        return NULL;
    }
    return dmfsi_strndup(entry + key_length + 1, length - key_length - 1);
}

char* dmfatfs_parse_device(const char* config)
{
    if (config == NULL || *config == '\0')
    {
        return NULL;
    }
    if (strchr(config, '=') == NULL)
    {
        return Dmod_StrDup(config);
    }
    for (const char* entry = config; *entry != '\0'; )
    {
        size_t length = 0;
        while (entry[length] != '\0' && !is_separator(entry[length]))
        {
            length++;
        }
        char* device = device_value(entry, length);
        if (device != NULL)
        {
            return device;
        }
        entry += length;
        entry += is_separator(*entry) ? 1 : 0;
    }
    return NULL;
}
