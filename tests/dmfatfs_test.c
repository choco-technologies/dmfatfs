#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod_test.h"
#include "dmfsi.h"
#include "dmfatfs.h"
#include <errno.h>
#include <string.h>

/*
 * dmfatfs on an image file: every step formats a fresh image with
 * dmfatfs_mkfs() and mounts it through the dmfsi DIF of the loaded module,
 * the same way dmvfs does on target (where the image is a dmdevfs node).
 */

#define IMAGE_PATH      "dmfatfs_test.img"
#define IMAGE_SIZE      (8u * 1024u * 1024u)
#define CHUNK_SIZE      4096u
#define BIG_FILE_SIZE   (200u * 1024u + 123u)
#define SMALL_IMAGE_SIZE (1024u * 1024u)
#define MAX_VOLUMES     4       /* DMFATFS_MAX_VOLUMES default */

typedef struct
{
    dmod_dmfsi_init_t       init;
    dmod_dmfsi_deinit_t     deinit;
    dmod_dmfsi_fopen_t      fopen;
    dmod_dmfsi_fclose_t     fclose;
    dmod_dmfsi_fread_t      fread;
    dmod_dmfsi_fwrite_t     fwrite;
    dmod_dmfsi_lseek_t      lseek;
    dmod_dmfsi_tell_t       tell;
    dmod_dmfsi_eof_t        eof;
    dmod_dmfsi_size_t       size;
    dmod_dmfsi_getc_t       getc;
    dmod_dmfsi_putc_t       putc;
    dmod_dmfsi_opendir_t    opendir;
    dmod_dmfsi_readdir_t    readdir;
    dmod_dmfsi_closedir_t   closedir;
    dmod_dmfsi_stat_t       stat;
    dmod_dmfsi_unlink_t     unlink;
    dmod_dmfsi_rename_t     rename;
    dmod_dmfsi_chmod_t      chmod;
    dmod_dmfsi_utime_t      utime;
    dmod_dmfsi_mkdir_t      mkdir;
    dmod_dmfsi_direxists_t  direxists;
} fatfs_t;

static fatfs_t          g_fs;
static bool             g_loaded;
static dmfsi_context_t  g_ctx;

#define GET_DIF(module, name)   (g_fs.name = Dmod_GetDifFunction(module, dmod_dmfsi_##name##_sig)) != NULL

static bool load_module(void)
{
    if (Dmod_LoadModuleByName("dmfatfs") == NULL || !Dmod_EnableModule("dmfatfs", false, NULL))
    {
        return false;
    }
    Dmod_Context_t* m = Dmod_GetModuleContext("dmfatfs");
    return GET_DIF(m, init) && GET_DIF(m, deinit) && GET_DIF(m, fopen) && GET_DIF(m, fclose) &&
           GET_DIF(m, fread) && GET_DIF(m, fwrite) && GET_DIF(m, lseek) && GET_DIF(m, tell) &&
           GET_DIF(m, eof) && GET_DIF(m, size) && GET_DIF(m, getc) && GET_DIF(m, putc) &&
           GET_DIF(m, opendir) && GET_DIF(m, readdir) && GET_DIF(m, closedir) && GET_DIF(m, stat) &&
           GET_DIF(m, unlink) && GET_DIF(m, rename) && GET_DIF(m, chmod) && GET_DIF(m, utime) &&
           GET_DIF(m, mkdir) && GET_DIF(m, direxists);
}

static bool create_image_file(const char* path, uint32_t size)
{
    static uint8_t zeros[CHUNK_SIZE];
    void* file = Dmod_FileOpen(path, "wb");
    if (file == NULL)
    {
        return false;
    }
    bool ok = true;
    for (uint32_t written = 0; ok && written < size; written += CHUNK_SIZE)
    {
        ok = Dmod_FileWrite(zeros, 1, CHUNK_SIZE, file) == CHUNK_SIZE;
    }
    Dmod_FileClose(file);
    return ok;
}

static bool create_image(void)
{
    return create_image_file(IMAGE_PATH, IMAGE_SIZE);
}

/* Small image with a file system and no partition table, for extra volumes. */
static bool create_volume_image(const char* path)
{
    dmfatfs_mkfs_options_t options = { .no_partition = true };
    return create_image_file(path, SMALL_IMAGE_SIZE) && dmfatfs_mkfs(path, &options) == 0;
}

void dmod_test_setup(void)
{
    g_loaded = g_loaded || load_module();
    g_ctx    = NULL;
    if (g_loaded && create_image() && dmfatfs_mkfs(IMAGE_PATH, NULL) == 0)
    {
        g_ctx = g_fs.init("device=" IMAGE_PATH);
    }
}

void dmod_test_teardown(void)
{
    if (g_ctx != NULL)
    {
        g_fs.deinit(g_ctx);
        g_ctx = NULL;
    }
    Dmod_FileRemove(IMAGE_PATH);
}

/* Write @p text to @p path (created/truncated), true on success. */
static bool write_text(const char* path, const char* text, int extra_mode)
{
    void*  fp      = NULL;
    size_t written = 0;
    int mode = DMFSI_O_WRONLY | DMFSI_O_CREAT | extra_mode;
    if (g_fs.fopen(g_ctx, &fp, path, mode, 0) != DMFSI_OK)
    {
        return false;
    }
    int ret = g_fs.fwrite(g_ctx, fp, text, strlen(text), &written);
    return (g_fs.fclose(g_ctx, fp) == DMFSI_OK) && ret == DMFSI_OK && written == strlen(text);
}

/* Read the whole of @p path into @p buffer (terminated), true on success. */
static bool read_text(const char* path, char* buffer, size_t size)
{
    void*  fp   = NULL;
    size_t read = 0;
    if (g_fs.fopen(g_ctx, &fp, path, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return false;
    }
    int ret = g_fs.fread(g_ctx, fp, buffer, size - 1, &read);
    buffer[read] = '\0';
    return (g_fs.fclose(g_ctx, fp) == DMFSI_OK) && ret == DMFSI_OK;
}

DMOD_TEST_STEP(mount_formatted_image)
{
    DMOD_TEST_EXPECT_TRUE(g_loaded);
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
}

DMOD_TEST_STEP(missing_device_is_refused)
{
    DMOD_TEST_EXPECT_NULL(g_fs.init(NULL));
    DMOD_TEST_EXPECT_NULL(g_fs.init("flash_addr=0x0"));
    DMOD_TEST_EXPECT_NULL(g_fs.init("device=no_such_image.img"));
}

DMOD_TEST_STEP(device_cannot_be_used_twice)
{
    DMOD_TEST_EXPECT_NULL(g_fs.init(IMAGE_PATH));
    DMOD_TEST_EXPECT_NE(dmfatfs_mkfs(IMAGE_PATH, NULL), 0);
}

DMOD_TEST_STEP(same_node_spelled_differently_is_refused)
{
    DMOD_TEST_EXPECT_NULL(g_fs.init("./" IMAGE_PATH));
    DMOD_TEST_EXPECT_NULL(g_fs.init("no_dir/../" IMAGE_PATH));
    DMOD_TEST_EXPECT_NULL(g_fs.init(".//./" IMAGE_PATH));
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs("./" IMAGE_PATH, NULL), -EBUSY);
}

DMOD_TEST_STEP(partition_of_mounted_device_is_refused)
{
    /* Checked before the node is opened - it does not even have to exist. */
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs(IMAGE_PATH "p1", NULL), -EBUSY);
    DMOD_TEST_EXPECT_TRUE(create_volume_image(IMAGE_PATH "x"));
    DMOD_TEST_EXPECT_NULL(g_fs.init(IMAGE_PATH "p1"));
    /* Not partition names of the mounted device */
    dmfsi_context_t other = g_fs.init(IMAGE_PATH "x");
    DMOD_TEST_EXPECT_NOT_NULL(other);
    if (other != NULL)
    {
        g_fs.deinit(other);
    }
    Dmod_FileRemove(IMAGE_PATH "x");
}

DMOD_TEST_STEP(device_of_mounted_partition_is_refused)
{
    g_fs.deinit(g_ctx);
    g_ctx = NULL;
    DMOD_TEST_EXPECT_TRUE(create_volume_image(IMAGE_PATH "p1"));
    DMOD_TEST_EXPECT_TRUE(create_volume_image(IMAGE_PATH "p2"));
    dmfsi_context_t p1 = g_fs.init(IMAGE_PATH "p1");
    dmfsi_context_t p2 = g_fs.init(IMAGE_PATH "p2");
    /* Sibling partitions do not overlap, the whole device overlaps both. */
    DMOD_TEST_EXPECT_NOT_NULL(p1);
    DMOD_TEST_EXPECT_NOT_NULL(p2);
    DMOD_TEST_EXPECT_NULL(g_fs.init(IMAGE_PATH));
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs(IMAGE_PATH, NULL), -EBUSY);
    g_fs.deinit(p1);
    DMOD_TEST_EXPECT_NULL(g_fs.init(IMAGE_PATH));
    g_fs.deinit(p2);
    g_ctx = g_fs.init(IMAGE_PATH);
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    Dmod_FileRemove(IMAGE_PATH "p1");
    Dmod_FileRemove(IMAGE_PATH "p2");
}

DMOD_TEST_STEP(volumes_are_independent)
{
    char buffer[16];
    DMOD_TEST_EXPECT_TRUE(create_volume_image("second.img"));
    dmfsi_context_t first  = g_ctx;
    dmfsi_context_t second = g_fs.init("second.img");
    DMOD_TEST_EXPECT_NOT_NULL(second);
    if (second != NULL)
    {
        DMOD_TEST_EXPECT_TRUE(write_text("/same.txt", "first", DMFSI_O_TRUNC));
        g_ctx = second;
        DMOD_TEST_EXPECT_TRUE(write_text("/same.txt", "second", DMFSI_O_TRUNC));
        DMOD_TEST_EXPECT_TRUE(read_text("/same.txt", buffer, sizeof(buffer)));
        DMOD_TEST_EXPECT_EQ(strcmp(buffer, "second"), 0);
        g_ctx = first;
        DMOD_TEST_EXPECT_TRUE(read_text("/same.txt", buffer, sizeof(buffer)));
        DMOD_TEST_EXPECT_EQ(strcmp(buffer, "first"), 0);
        g_fs.deinit(second);
    }
    Dmod_FileRemove("second.img");
}

DMOD_TEST_STEP(volume_limit)
{
    /* Names built at run time: the loader does not relocate pointer tables in data. */
    char            names[MAX_VOLUMES][8];
    dmfsi_context_t extra[MAX_VOLUMES] = { 0 };
    for (int i = 0; i < MAX_VOLUMES; i++)
    {
        memcpy(names[i], "vN.img", sizeof("vN.img"));
        names[i][1] = (char)('1' + i);
        DMOD_TEST_EXPECT_TRUE(create_volume_image(names[i]));
    }
    /* g_ctx holds one volume already */
    for (int i = 0; i < MAX_VOLUMES - 1; i++)
    {
        extra[i] = g_fs.init(names[i]);
        DMOD_TEST_EXPECT_NOT_NULL(extra[i]);
    }
    DMOD_TEST_EXPECT_NULL(g_fs.init(names[MAX_VOLUMES - 1]));
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs(names[MAX_VOLUMES - 1], NULL), -EBUSY);
    for (int i = 0; i < MAX_VOLUMES; i++)
    {
        if (extra[i] != NULL)
        {
            g_fs.deinit(extra[i]);
        }
        Dmod_FileRemove(names[i]);
    }
}

DMOD_TEST_STEP(unformatted_device_is_refused)
{
    g_fs.deinit(g_ctx);
    g_ctx = NULL;
    DMOD_TEST_EXPECT_TRUE(create_image());
    DMOD_TEST_EXPECT_NULL(g_fs.init(IMAGE_PATH));
}

DMOD_TEST_STEP(write_and_read_back)
{
    char buffer[64];
    DMOD_TEST_EXPECT_TRUE(write_text("/hello.txt", "Hello FAT", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_TRUE(read_text("/hello.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "Hello FAT"), 0);
}

DMOD_TEST_STEP(truncate_and_append)
{
    char buffer[64];
    DMOD_TEST_EXPECT_TRUE(write_text("/log.txt", "first", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_TRUE(write_text("/log.txt", "+second", DMFSI_O_APPEND));
    DMOD_TEST_EXPECT_TRUE(read_text("/log.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "first+second"), 0);
    DMOD_TEST_EXPECT_TRUE(write_text("/log.txt", "new", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_TRUE(read_text("/log.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "new"), 0);
}

DMOD_TEST_STEP(open_missing_file_fails)
{
    void* fp = NULL;
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_ctx, &fp, "/missing.txt", DMFSI_O_RDONLY, 0), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_ctx, &fp, "/no_dir/file.txt", DMFSI_O_WRONLY | DMFSI_O_CREAT, 0), DMFSI_ERR_NOT_FOUND);
}

DMOD_TEST_STEP(seek_tell_eof_size)
{
    void* fp = NULL;
    DMOD_TEST_EXPECT_TRUE(write_text("/seek.txt", "0123456789", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_ctx, &fp, "/seek.txt", DMFSI_O_RDONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.size(g_ctx, fp), 10u);
    DMOD_TEST_EXPECT_EQ(g_fs.lseek(g_ctx, fp, 4, DMFSI_SEEK_SET), 4);
    DMOD_TEST_EXPECT_EQ(g_fs.getc(g_ctx, fp), '4');
    DMOD_TEST_EXPECT_EQ(g_fs.lseek(g_ctx, fp, 2, DMFSI_SEEK_CUR), 7);
    DMOD_TEST_EXPECT_EQ(g_fs.getc(g_ctx, fp), '7');
    DMOD_TEST_EXPECT_EQ(g_fs.lseek(g_ctx, fp, -1, DMFSI_SEEK_END), 9);
    DMOD_TEST_EXPECT_EQ(g_fs.tell(g_ctx, fp), 9);
    DMOD_TEST_EXPECT_EQ(g_fs.eof(g_ctx, fp), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.getc(g_ctx, fp), '9');
    DMOD_TEST_EXPECT_EQ(g_fs.eof(g_ctx, fp), 1);
    DMOD_TEST_EXPECT_EQ(g_fs.getc(g_ctx, fp), -1);
    DMOD_TEST_EXPECT_TRUE(g_fs.lseek(g_ctx, fp, -11, DMFSI_SEEK_END) < 0);
    g_fs.fclose(g_ctx, fp);
}

DMOD_TEST_STEP(putc_writes_bytes)
{
    void* fp = NULL;
    char  buffer[8];
    DMOD_TEST_EXPECT_EQ(g_fs.fopen(g_ctx, &fp, "/putc.txt", DMFSI_O_WRONLY | DMFSI_O_CREAT, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.putc(g_ctx, fp, 'o'), 'o');
    DMOD_TEST_EXPECT_EQ(g_fs.putc(g_ctx, fp, 'k'), 'k');
    g_fs.fclose(g_ctx, fp);
    DMOD_TEST_EXPECT_TRUE(read_text("/putc.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "ok"), 0);
}

static uint8_t pattern_byte(uint32_t offset)
{
    return (uint8_t)((offset * 31u) ^ (offset >> 8));
}

static bool write_pattern(const char* path, uint8_t* chunk)
{
    void* fp = NULL;
    if (g_fs.fopen(g_ctx, &fp, path, DMFSI_O_WRONLY | DMFSI_O_CREAT | DMFSI_O_TRUNC, 0) != DMFSI_OK)
    {
        return false;
    }
    bool ok = true;
    for (uint32_t offset = 0; ok && offset < BIG_FILE_SIZE; offset += CHUNK_SIZE)
    {
        size_t length  = (BIG_FILE_SIZE - offset < CHUNK_SIZE) ? BIG_FILE_SIZE - offset : CHUNK_SIZE;
        size_t written = 0;
        for (size_t i = 0; i < length; i++)
        {
            chunk[i] = pattern_byte(offset + (uint32_t)i);
        }
        ok = g_fs.fwrite(g_ctx, fp, chunk, length, &written) == DMFSI_OK && written == length;
    }
    return (g_fs.fclose(g_ctx, fp) == DMFSI_OK) && ok;
}

static bool verify_pattern(const char* path, uint8_t* chunk)
{
    void* fp = NULL;
    if (g_fs.fopen(g_ctx, &fp, path, DMFSI_O_RDONLY, 0) != DMFSI_OK)
    {
        return false;
    }
    bool     ok     = true;
    uint32_t offset = 0;
    size_t   read   = 0;
    while (ok && g_fs.fread(g_ctx, fp, chunk, CHUNK_SIZE, &read) == DMFSI_OK && read != 0)
    {
        for (size_t i = 0; ok && i < read; i++)
        {
            ok = chunk[i] == pattern_byte(offset + (uint32_t)i);
        }
        offset += (uint32_t)read;
    }
    g_fs.fclose(g_ctx, fp);
    return ok && offset == BIG_FILE_SIZE;
}

DMOD_TEST_STEP(big_file_round_trip)
{
    uint8_t* chunk = Dmod_Malloc(CHUNK_SIZE);
    DMOD_TEST_EXPECT_NOT_NULL(chunk);
    if (chunk != NULL)
    {
        DMOD_TEST_EXPECT_TRUE(write_pattern("/big.bin", chunk));
        DMOD_TEST_EXPECT_TRUE(verify_pattern("/big.bin", chunk));
        Dmod_Free(chunk);
    }
}

/* Names found in @p path, '|'-separated, in directory order. */
static void list_dir(const char* path, char* names, size_t size)
{
    void*             dp = NULL;
    dmfsi_dir_entry_t entry;
    names[0] = '\0';
    if (g_fs.opendir(g_ctx, &dp, path) != DMFSI_OK)
    {
        return;
    }
    while (g_fs.readdir(g_ctx, dp, &entry) == DMFSI_OK)
    {
        strncat(names, entry.name, size - strlen(names) - 2);
        strcat(names, "|");
    }
    g_fs.closedir(g_ctx, dp);
}

DMOD_TEST_STEP(directories_and_long_names)
{
    char names[256];
    DMOD_TEST_EXPECT_EQ(g_fs.mkdir(g_ctx, "/Some Directory", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.mkdir(g_ctx, "/Some Directory", 0), DMFSI_ERR_EXISTS);
    DMOD_TEST_EXPECT_TRUE(write_text("/Some Directory/A rather long file name.txt", "x", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_TRUE(write_text("/Some Directory/b.TXT", "y", DMFSI_O_TRUNC));
    list_dir("/Some Directory", names, sizeof(names));
    DMOD_TEST_EXPECT_EQ(strcmp(names, "A rather long file name.txt|b.TXT|"), 0);
    list_dir("/", names, sizeof(names));
    DMOD_TEST_EXPECT_EQ(strcmp(names, "Some Directory|"), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.direxists(g_ctx, "/"), 1);
    DMOD_TEST_EXPECT_EQ(g_fs.direxists(g_ctx, "/Some Directory"), 1);
    DMOD_TEST_EXPECT_EQ(g_fs.direxists(g_ctx, "/Some Directory/b.TXT"), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.direxists(g_ctx, "/nothing"), 0);
}

DMOD_TEST_STEP(stat_reports_size_and_type)
{
    dmfsi_stat_t st;
    DMOD_TEST_EXPECT_TRUE(write_text("/file.txt", "12345", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.mkdir(g_ctx, "/dir", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/file.txt", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(st.size, 5u);
    DMOD_TEST_EXPECT_EQ(st.attr & DMFSI_ATTR_DIRECTORY, 0u);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/dir", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_NE(st.attr & DMFSI_ATTR_DIRECTORY, 0u);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_NE(st.attr & DMFSI_ATTR_DIRECTORY, 0u);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/missing", &st), DMFSI_ERR_NOT_FOUND);
}

DMOD_TEST_STEP(rename_moves_and_replaces)
{
    char buffer[16];
    DMOD_TEST_EXPECT_EQ(g_fs.mkdir(g_ctx, "/dir", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE(write_text("/dir/a.txt", "A", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_TRUE(write_text("/b.txt", "B", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.rename(g_ctx, "/dir/a.txt", "/moved.txt"), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE(read_text("/moved.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "A"), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.rename(g_ctx, "/moved.txt", "/b.txt"), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE(read_text("/b.txt", buffer, sizeof(buffer)));
    DMOD_TEST_EXPECT_EQ(strcmp(buffer, "A"), 0);
    DMOD_TEST_EXPECT_EQ(g_fs.rename(g_ctx, "/b.txt", "/dir"), DMFSI_ERR_EXISTS);
    DMOD_TEST_EXPECT_EQ(g_fs.rename(g_ctx, "/missing", "/x"), DMFSI_ERR_NOT_FOUND);
}

DMOD_TEST_STEP(unlink_files_and_directories)
{
    dmfsi_stat_t st;
    DMOD_TEST_EXPECT_EQ(g_fs.mkdir(g_ctx, "/dir", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE(write_text("/dir/f.txt", "f", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.unlink(g_ctx, "/dir"), DMFSI_ERR_NOT_EMPTY);
    DMOD_TEST_EXPECT_EQ(g_fs.unlink(g_ctx, "/dir/f.txt"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.unlink(g_ctx, "/dir"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/dir", &st), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_fs.unlink(g_ctx, "/dir"), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_fs.unlink(g_ctx, "/"), DMFSI_ERR_INVALID);
}

DMOD_TEST_STEP(chmod_sets_read_only)
{
    void*        fp = NULL;
    dmfsi_stat_t st;
    DMOD_TEST_EXPECT_TRUE(write_text("/ro.txt", "r", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.chmod(g_ctx, "/ro.txt", 0444), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/ro.txt", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_NE(st.attr & DMFSI_ATTR_READONLY, 0u);
    DMOD_TEST_EXPECT_NE(g_fs.fopen(g_ctx, &fp, "/ro.txt", DMFSI_O_WRONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.chmod(g_ctx, "/ro.txt", 0644), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/ro.txt", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(st.attr & DMFSI_ATTR_READONLY, 0u);
}

DMOD_TEST_STEP(utime_round_trip)
{
    /* 2024-02-29 13:37:42 UTC - FAT stores seconds with a 2 s resolution */
    const uint32_t mtime = 1709213862u;
    dmfsi_stat_t   st;
    DMOD_TEST_EXPECT_TRUE(write_text("/t.txt", "t", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.utime(g_ctx, "/t.txt", mtime, mtime), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fs.stat(g_ctx, "/t.txt", &st), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(st.mtime, mtime);
}

DMOD_TEST_STEP(data_survives_remount)
{
    char buffer[32];
    DMOD_TEST_EXPECT_TRUE(write_text("/persist.txt", "still here", DMFSI_O_TRUNC));
    DMOD_TEST_EXPECT_EQ(g_fs.deinit(g_ctx), DMFSI_OK);
    g_ctx = g_fs.init("dev=" IMAGE_PATH ";other=1");
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    if (g_ctx != NULL)
    {
        DMOD_TEST_EXPECT_TRUE(read_text("/persist.txt", buffer, sizeof(buffer)));
        DMOD_TEST_EXPECT_EQ(strcmp(buffer, "still here"), 0);
    }
}

static void expect_format(const dmfatfs_mkfs_options_t* options)
{
    char buffer[16];
    g_fs.deinit(g_ctx);
    g_ctx = NULL;
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs(IMAGE_PATH, options), 0);
    g_ctx = g_fs.init(IMAGE_PATH);
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    if (g_ctx != NULL)
    {
        DMOD_TEST_EXPECT_TRUE(write_text("/f.txt", "ok", DMFSI_O_TRUNC));
        DMOD_TEST_EXPECT_TRUE(read_text("/f.txt", buffer, sizeof(buffer)));
        DMOD_TEST_EXPECT_EQ(strcmp(buffer, "ok"), 0);
    }
}

DMOD_TEST_STEP(mkfs_types)
{
    dmfatfs_mkfs_options_t options = { 0 };
    options.type = dmfatfs_type_exfat;
    expect_format(&options);
    options.type         = dmfatfs_type_fat;
    options.no_partition = true;
    options.cluster_size = 1024;
    expect_format(&options);
    options.type = (dmfatfs_type_t)42;
    DMOD_TEST_EXPECT_EQ(dmfatfs_mkfs(IMAGE_PATH "x", &options), -EINVAL);
}
