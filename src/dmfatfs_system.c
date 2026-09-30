/**
 * @file dmfatfs_system.c
 * @brief OS dependent functions required by FatFs (ffsystem.c replacement)
 *
 * Memory for the LFN working buffer comes from the DMOD heap, the volume
 * mutexes (FF_FS_REENTRANT) and the module lock are DMOD SAL mutexes.
 */
#include "dmfatfs_internal.h"

/* Mutex of each volume, plus the FatFs system mutex at index FF_VOLUMES (FF_FS_LOCK). */
static void* g_ff_mutexes[FF_VOLUMES + 1];
static void* g_module_mutex;

bool dmfatfs_system_init(void)
{
    g_module_mutex = Dmod_Mutex_New(true);
    return g_module_mutex != NULL;
}

void dmfatfs_system_deinit(void)
{
    /* The system mutex is never deleted by FatFs itself. */
    for (int i = 0; i <= FF_VOLUMES; i++)
    {
        ff_mutex_delete(i);
    }
    if (g_module_mutex != NULL)
    {
        Dmod_Mutex_Delete(g_module_mutex);
        g_module_mutex = NULL;
    }
}

void dmfatfs_lock(void)
{
    if (g_module_mutex != NULL)
    {
        Dmod_Mutex_Lock(g_module_mutex);
    }
}

void dmfatfs_unlock(void)
{
    if (g_module_mutex != NULL)
    {
        Dmod_Mutex_Unlock(g_module_mutex);
    }
}

void* ff_memalloc(UINT msize)
{
    return Dmod_Malloc((size_t)msize);
}

void ff_memfree(void* mblock)
{
    if (mblock != NULL)
    {
        Dmod_Free(mblock);
    }
}

int ff_mutex_create(int vol)
{
    if (g_ff_mutexes[vol] == NULL)
    {
        g_ff_mutexes[vol] = Dmod_Mutex_New(false);
    }
    return g_ff_mutexes[vol] != NULL;
}

void ff_mutex_delete(int vol)
{
    if (g_ff_mutexes[vol] != NULL)
    {
        Dmod_Mutex_Delete(g_ff_mutexes[vol]);
        g_ff_mutexes[vol] = NULL;
    }
}

/* Waits without the FF_FS_TIMEOUT limit - the SAL mutex has no timed lock. */
int ff_mutex_take(int vol)
{
    return g_ff_mutexes[vol] != NULL && Dmod_Mutex_Lock(g_ff_mutexes[vol]) == 0;
}

void ff_mutex_give(int vol)
{
    if (g_ff_mutexes[vol] != NULL)
    {
        Dmod_Mutex_Unlock(g_ff_mutexes[vol]);
    }
}
