/*---------------------------------------------------------------------------/
/  Configuration of FatFs R0.16 for the dmfatfs DMOD module
/
/  This file replaces the stock ffconf.h shipped with FatFs (lib/fatfs keeps
/  the upstream sources untouched). Only the values differ from the upstream
/  template - see lib/fatfs/00readme.txt and the FatFs documentation at
/  https://elm-chan.org/fsw/ff/ for the meaning of each option.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80386	/* Revision ID */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		1	/* dmfatfs_mkfs() */
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	1	/* dmfsi _chmod/_utime */
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437
#define FF_USE_LFN		3	/* LFN working buffer on the heap (ff_memalloc) */
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	2	/* UTF-8 on the API - names pass through dmvfs unchanged */
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
#define FF_FS_RPATH		0	/* dmvfs resolves relative paths itself */
#define FF_PATH_DEPTH	10

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

/* One volume per mount (or per dmfatfs_mkfs() in progress) */
#ifndef DMFATFS_MAX_VOLUMES
#   define DMFATFS_MAX_VOLUMES  4
#endif
#define FF_VOLUMES		DMFATFS_MAX_VOLUMES
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION	0	/* dmdevfs exposes each partition as its own node */
#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		1	/* Required by exFAT */
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0
#define FF_FS_EXFAT		1	/* SDXC cards come formatted with exFAT */
#define FF_FS_NORTC		1	/* No RTC available to modules yet */
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
#define FF_FS_CRTIME	0
#define FF_FS_NOFSINFO	0
#ifndef DMFATFS_MAX_LOCKED_FILES
#   define DMFATFS_MAX_LOCKED_FILES 16
#endif
#define FF_FS_LOCK		DMFATFS_MAX_LOCKED_FILES
#define FF_FS_REENTRANT	1	/* dmvfs calls file systems without holding its own lock */
#define FF_FS_TIMEOUT	1000

/*--- End of configuration options ---*/
