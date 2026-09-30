# mkfatfs

Command line front end of [dmfatfs](../../README.md)'s `dmfatfs_mkfs()`:
creates a FAT or exFAT file system on a block device. **All data on the
device is lost.**

```
mkfatfs [-t fat|fat32|exfat] [-s] [-c <bytes>] <device>
```

| Option | Meaning |
|--------|---------|
| `-t <type>` | `fat` (FAT12/FAT16), `fat32` or `exfat`. Default: chosen by device size (exFAT from 32 GiB up) |
| `-s` | No partition table - the file system covers the whole device |
| `-c <bytes>` | Cluster size. Default: chosen by device size |

By default a partition table with one partition is created, like on a
factory formatted SD card. The whole-disk node can be mounted either way:

```
> mkfatfs /dev/dmsdio0/0
Formatting /dev/dmsdio0/0...
/dev/dmsdio0/0: done
> mount -t dmfatfs /dev/dmsdio0/0 /mnt/sd
```

The device must not be mounted. On failure the negative errno is printed and
returned as the exit status (see `dmfatfs_mkfs()` in
[docs/api-reference.md](../../docs/api-reference.md)).
