/**
 * @file dmfatfs_libc.c
 * @brief C library functions FatFs needs and the DMOD module runtime lacks
 *
 * Modules are not linked against a C library - dmod provides a small set of
 * string functions (dmod/src/module/string.c), which has no memcmp(). The
 * definition is weak so that a future dmod implementation takes precedence.
 */
#include <stddef.h>

__attribute__((weak)) int memcmp(const void* s1, const void* s2, size_t n)
{
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i])
        {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}
