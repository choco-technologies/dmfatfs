# dmfatfs

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmfatfs/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmfatfs/actions/workflows/ci.yml)

FAT12/FAT16/FAT32/exFAT file system for DMOD, based on
[FatFs](https://elm-chan.org/fsw/ff/) R0.16 by ChaN.

## Description

dmfatfs implements the [dmfsi](https://github.com/choco-technologies/dmfsi)
file system interface, so it is mounted through dmvfs like `dmramfs`,
`dmffs` or `dmdevfs`. It keeps the file system on a block device node
published by dmdevfs (an SD card through `dmsdio`, one of its partitions, ...)
or, on the host, on an image file.

- FAT12, FAT16, FAT32 and exFAT, long file names (UTF-8)
- whole disks with an MBR/GPT partition table, partition nodes and
  "superfloppy" devices
- several volumes mounted at once, thread-safe (FatFs re-entrancy on DMOD mutexes)
- `dmfatfs_mkfs()` and the `mkfatfs` tool to format a device

## Usage

```
mkfatfs /dev/dmsdio0/0                      # only once - destroys all data!
mkdir /mnt
mkdir /mnt/sd
mount -t dmfatfs /dev/dmsdio0/0 /mnt/sd
ls /mnt/sd
umount /mnt/sd
```

`mount`/`umount` are dmell commands. From C:

```c
dmvfs_mount_fs("dmfatfs", "/mnt/sd", "device=/dev/dmsdio0/0");
```

The configuration string is the device path, either alone or as
`device=<path>`. Once mounted, everything goes through the regular file API
(`Dmod_FileOpen()`, `Dmod_ReadDir()`, ...).

Formatting from C:

```c
#include "dmfatfs.h"

dmfatfs_mkfs_options_t options = { .type = dmfatfs_type_fat32 };
int ret = dmfatfs_mkfs("/dev/dmsdio0/0", &options);
```

See [docs/api-reference.md](docs/api-reference.md) for the configuration
string, the behavior of every file operation on FAT and the module API.

## Configuration

| CMake cache variable | Default | Meaning |
|----------------------|---------|---------|
| `DMFATFS_MAX_VOLUMES` | 4 | Volumes mounted (or formatted) at the same time, 1-10 |
| `DMFATFS_MAX_LOCKED_FILES` | 16 | Files open at the same time, on all volumes |

The remaining FatFs options are fixed in [src/ffconf.h](src/ffconf.h):
code page 437 for short names, 512-byte sectors, exFAT and 64-bit LBA
enabled, no RTC (new files get a fixed time stamp).

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub, and
`-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7` (or another architecture) to build
for a target.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

The tests format an image file with `dmfatfs_mkfs()` and drive dmfatfs
through its dmfsi interface on the host. Once built, run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmfatfs-local.dmd -y
dmod_loader build/dmf/test_dmfatfs.dmf
```

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmfatfs`.

## Project Structure

```
dmfatfs/
├── apps/
│   └── mkfatfs/           # mkfatfs command (front end of dmfatfs_mkfs())
├── docs/                  # Documentation (markdown format)
├── include/
│   └── dmfatfs.h          # Module API (dmfatfs_mkfs)
├── lib/
│   └── fatfs/             # FatFs R0.16, unmodified upstream sources
├── src/
│   ├── dmfatfs.c          # Mount contexts (dmfsi _init/_deinit), dmfatfs_mkfs()
│   ├── dmfatfs_file.c     # dmfsi file operations
│   ├── dmfatfs_dir.c      # dmfsi directory and path operations
│   ├── dmfatfs_disk.c     # FatFs disk I/O on top of the DMOD file API
│   ├── dmfatfs_system.c   # FatFs OS hooks (heap, mutexes)
│   ├── dmfatfs_util.c     # Error, path, time and config conversions
│   ├── dmfatfs_libc.c     # memcmp() for FatFs (missing in the module runtime)
│   ├── dmfatfs_internal.h
│   └── ffconf.h           # FatFs configuration
├── tests/
│   ├── CMakeLists.txt
│   └── dmfatfs_test.c
├── CMakeLists.txt
├── Makefile
├── dmfatfs.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT. FatFs is distributed under its own BSD-style license, see
[lib/fatfs/LICENSE.txt](lib/fatfs/LICENSE.txt).
