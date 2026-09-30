# =====================================================================
#               mkfatfs - command line front end of dmfatfs_mkfs()
# =====================================================================
# Creates a FAT/exFAT file system on the device given as an argument.
set(DMOD_MODULE_NAME        mkfatfs)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         2048)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/mkfatfs.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/main.c
)

target_link_libraries(${DMOD_MODULE_NAME} dmfatfs_if)
