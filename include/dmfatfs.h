#ifndef DMFATFS_H
#define DMFATFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmfatfs_defs.h"

/**
 * Public API for the dmfatfs module.
 *
 * Functions are declared with the dmod_dmfatfs_api(...) macro - dmod's
 * standard pattern for functions callable from other modules (or from this
 * module's own tests/), resolved dynamically by the loader rather than
 * through normal static linkage. See dm_sw_ring/include/dm_sw_ring.h for a
 * fully worked real-world example of the same shape.
 *
 * Definitions in src/dmfatfs.c use the matching
 * dmod_dmfatfs_api_declaration(...) macro - a plain C function
 * definition here will NOT satisfy these declarations at link time.
 *
 * This is an example interface using the usual "opaque handle" pattern -
 * replace the handle, functions, and struct definition in
 * src/dmfatfs.c with your module's real API.
 */

/* Opaque handle - the real struct is defined in src/dmfatfs.c */
typedef struct dmfatfs* dmfatfs_t;

/**
 * Create a new dmfatfs instance.
 *
 * @return A valid handle on success, or NULL on allocation failure.
 */
dmod_dmfatfs_api(1.0, dmfatfs_t, _create, ( void ));

/**
 * Destroy an instance created by dmfatfs_create(). Safe to call with
 * NULL.
 */
dmod_dmfatfs_api(1.0, void, _destroy, ( dmfatfs_t handle ));

/**
 * Example accessor - replace with your module's real API.
 *
 * @return true if handle is a valid, non-NULL instance.
 */
dmod_dmfatfs_api(1.0, bool, _is_valid, ( dmfatfs_t handle ));

#endif // DMFATFS_H
