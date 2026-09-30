#include "dmod.h"
#include "dmfatfs.h"
#include <errno.h>
#include <string.h>

/*
 * mkfatfs [-t fat|fat32|exfat] [-s] [-c <bytes>] <device> - create a FAT
 * file system on <device> (all data on it is lost):
 *
 *   mkfatfs /dev/dmsdio0/0              (partition table + FAT by card size)
 *   mkfatfs -t exfat -s /dev/dmsdio0/0  (exFAT on the whole card)
 */

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage: %s [-t fat|fat32|exfat] [-s] [-c <bytes>] <device>\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Create a FAT file system on a block device (ALL DATA ON IT IS LOST).\n");
    Dmod_Printf("  -t <type>   fat (FAT12/16), fat32 or exfat - default: by device size\n");
    Dmod_Printf("  -s          no partition table, file system on the whole device\n");
    Dmod_Printf("  -c <bytes>  cluster size - default: by device size\n");
}

static bool parse_type(const char* text, dmfatfs_type_t* type)
{
    if (strcmp(text, "fat") == 0)   { *type = dmfatfs_type_fat;   return true; }
    if (strcmp(text, "fat32") == 0) { *type = dmfatfs_type_fat32; return true; }
    if (strcmp(text, "exfat") == 0) { *type = dmfatfs_type_exfat; return true; }
    return false;
}

static bool parse_size(const char* text, uint32_t* value)
{
    uint32_t result = 0;
    if (*text == '\0')
    {
        return false;
    }
    for (; *text != '\0'; text++)
    {
        if (*text < '0' || *text > '9' || result > (UINT32_MAX - 9u) / 10u)
        {
            return false;
        }
        result = result * 10u + (uint32_t)(*text - '0');
    }
    *value = result;
    return true;
}

/* Option at argv[*i] (its value, if any, at argv[*i + 1]). false on a bad option. */
static bool parse_option(int argc, char* argv[], int* i, dmfatfs_mkfs_options_t* options)
{
    const char* option = argv[*i];
    if (strcmp(option, "-s") == 0)
    {
        options->no_partition = true;
        return true;
    }
    if ((strcmp(option, "-t") != 0 && strcmp(option, "-c") != 0) || *i + 1 >= argc)
    {
        return false;
    }
    const char* value = argv[++(*i)];
    return (option[1] == 't') ? parse_type(value, &options->type)
                              : parse_size(value, &options->cluster_size);
}

int main(int argc, char* argv[])
{
    dmfatfs_mkfs_options_t options = { 0 };
    const char*            device  = NULL;
    for (int i = 1; i < argc; i++)
    {
        bool ok = (argv[i][0] == '-') ? parse_option(argc, argv, &i, &options)
                                      : (device == NULL && (device = argv[i]) != NULL);
        if (!ok)
        {
            print_usage(argv[0]);
            return -EINVAL;
        }
    }
    if (device == NULL)
    {
        print_usage(argv[0]);
        return -EINVAL;
    }
    Dmod_Printf("Formatting %s...\n", device);
    int ret = dmfatfs_mkfs(device, &options);
    if (ret != 0)
    {
        Dmod_Printf("%s: cannot create the file system (%d)\n", device, ret);
        return ret;
    }
    Dmod_Printf("%s: done\n", device);
    return 0;
}
