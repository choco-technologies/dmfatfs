# dmfatfs Documentation

dmfatfs is a FAT12/FAT16/FAT32/exFAT file system for DMOD (FatFs based),
mounted through dmvfs:

```
mount -t dmfatfs /dev/dmsdio0/0 /mnt/sd
```

## Contents

- **[api-reference.md](api-reference.md)** - Mount configuration, behavior of
  the file operations on FAT, and the module API (`dmfatfs_mkfs()`)

## Quick Reference

```c
#include "dmfatfs.h"

dmvfs_mount_fs("dmfatfs", "/mnt/sd", "device=/dev/dmsdio0/0");
dmfatfs_mkfs("/dev/dmsdio0/0", NULL);   /* format - destroys all data */
```

View documentation using `dmf-man`:

```bash
dmf-man dmfatfs          # Main documentation
dmf-man dmfatfs api      # API reference
```
