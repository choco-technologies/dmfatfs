# dmfatfs API Reference

dmfatfs has two faces:

- the **dmfsi file system interface** (DIF) - how dmvfs uses it once it is
  mounted; applications never call these functions directly, they use the
  regular file API (`Dmod_FileOpen()`, `Dmod_ReadDir()`, ...) on paths below
  the mount point,
- the **module API** in `dmfatfs.h` - currently `dmfatfs_mkfs()`, used to
  create a file system on a device before it can be mounted.

## Mounting

```c
dmvfs_mount_fs("dmfatfs", "/mnt/sd", "device=/dev/dmsdio0/0");
```

or from the shell (`mount` command of dmell):

```
mount -t dmfatfs /dev/dmsdio0/0 /mnt/sd
```

### Configuration string

| Form | Meaning |
|------|---------|
| `device=<path>` | Block device (or image file) holding the file system |
| `dev=<path>` | Same as `device=` |
| `<path>` | The whole string is the device path (no `=` in it) |

Entries are separated with `;` or `,`; unknown keys are ignored.

The device can be:

- a partition node, e.g. `/dev/dmsdio0/0p1` - the file system in that partition,
- a whole-disk node, e.g. `/dev/dmsdio0/0` - a file system directly on the
  disk ("superfloppy"), or, if the disk has an MBR/GPT partition table, the
  first FAT/exFAT partition on it,
- an image file (e.g. on the host, with `dmod_loader`).

The sector size must be 512 bytes. Block nodes report their size through
`DMDRVI_IOCTL_BLOCK_GET_INFO`; other files use their file size. A node that
cannot be opened for writing is mounted read-only.

At most `DMFATFS_MAX_VOLUMES` (default 4) volumes can be mounted (or
formatted) at once, each on its own device.

### Protection against overlapping volumes

Two FatFs volumes writing the same sectors would corrupt the medium, so a
mount or `dmfatfs_mkfs()` is refused (`-EBUSY`) while an *overlapping*
device is mounted or being formatted:

- the same node, however the path is written (relative paths are resolved
  against the working directory, `//`, `.` and `..` are collapsed),
- a whole device and any of its partitions, in either direction - dmdevfs
  names partition nodes `<device>p<number>`, e.g. `/dev/dmsdio0/0` and
  `/dev/dmsdio0/0p1`.

Different partitions of one device (`0p1`, `0p2`) do not overlap and can be
mounted at the same time.

Not covered: writes that bypass dmfatfs (e.g. copying raw data onto the
device node while it is mounted), the same medium exposed under two
unrelated paths (e.g. dmdevfs mounted twice), and removing the medium
while it is mounted - unmount first.

### Supported operations

| dmfsi operation | Behavior on FAT |
|-----------------|-----------------|
| `_fopen` | `DMFSI_O_RDONLY/WRONLY/RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND` (every write goes to the end). A file can be open for writing only once at a time. |
| `_fread`, `_fwrite`, `_getc`, `_putc` | Regular reads/writes. `_getc` returns -1 at the end of the file. |
| `_lseek`, `_tell`, `_eof`, `_size` | 64-bit offsets. Seeking past the end expands a file open for writing. |
| `_sync`, `_fflush` | Write cached data and directory entry to the medium. |
| `_opendir`, `_readdir`, `_closedir` | Long file names (UTF-8). `.`/`..` are not reported. |
| `_stat` | Size, attributes, modification time (also returned as ctime/atime). |
| `_mkdir`, `_direxists` | |
| `_unlink` | Files and empty directories (dmvfs uses it for rmdir). `DMFSI_ERR_NOT_EMPTY` for a non-empty directory or a read-only entry. |
| `_rename` | Also moves between directories; an existing file at the destination is replaced, an existing directory is not (`DMFSI_ERR_EXISTS`). |
| `_chmod` | FAT only has a read-only attribute: it is set when the mode has no write permission (`mode & 0222 == 0`), cleared otherwise. |
| `_utime` | Sets the modification time (`atime` is not stored by FAT). 2 s resolution. |
| `_ioctl` | Not supported (`DMFSI_ERR_GENERAL`). |

Times are seconds since 1970-01-01. There is no real-time clock available to
modules yet, so new and modified files get the fixed time 2026-01-01 00:00.

## Types

### `dmfatfs_type_t`

| Value | File system |
|-------|-------------|
| `dmfatfs_type_auto` | FAT12/FAT16/FAT32 by volume size, exFAT from 32 GiB up (or for clusters over 64 KiB) |
| `dmfatfs_type_fat` | FAT12 or FAT16, by volume size |
| `dmfatfs_type_fat32` | FAT32 |
| `dmfatfs_type_exfat` | exFAT |

### `dmfatfs_mkfs_options_t`

| Field | Meaning |
|-------|---------|
| `dmfatfs_type_t type` | File system type |
| `bool no_partition` | `true`: file system directly on the device, no partition table |
| `uint32_t cluster_size` | Cluster size in bytes, `0` for the default of the volume size |

A zero-initialized structure (or `NULL`) means: automatic type, with a
partition table, default cluster size.

## Functions

### `dmfatfs_mkfs`

```c
int dmfatfs_mkfs(const char* device_path, const dmfatfs_mkfs_options_t* options);
```

Creates a new file system on `device_path`. **All data on the device is
lost.** The device must not be mounted.

By default a partition table with one partition covering the device is
created (like a factory formatted SD card); the whole-disk node can still be
mounted directly, dmfatfs finds the file system in the first partition.

**Returns** `0` on success or a negative errno: `-EINVAL` (bad arguments),
`-ENOENT` (device cannot be opened), `-EBUSY` (device mounted, or no free
volume), `-ENOTSUP` (sector size other than 512), `-ENOSPC` (device too
small for the requested type/cluster size), `-ENOMEM`, `-EIO`.

The `mkfatfs` command line tool (see
[apps/mkfatfs/README.md](../apps/mkfatfs/README.md)) is a front end of this
function.
