#ifndef _FLUX_CORE_MODULE_H
#define _FLUX_CORE_MODULE_H

#include <json.h>
#include <czmq.h>

#include "handle.h"

/* Flags for module load/unload
 */
enum {
    FLUX_MOD_FLAGS_MANAGED = 1, /* XXX used by modctl, may go away soon */
};

/* Send a request to load/unload a module (use rank = -1 for local).
 * These go to the broker unless the module name is hierarchical, e.g.
 * flux_insmod() of kvs would generate a "cmb.insmod" message, while
 * flux_rmmod() of sched.backfill would generate a "sched.rmmod" message.
 */
int flux_rmmod (flux_t h, int rank, const char *name, int flags);
int flux_insmod (flux_t h, int rank, const char *path, int flags,
                 json_object *args);

/* While the target of an insmod/rmmod message can be determined by
 * parsing the module name, lsmod requires it to be explicity specified
 * in 'target'.
 */
json_object *flux_lsmod (flux_t h, int rank, const char *target);

/* All flux modules must define the "mod_name" symbol with this macro.
 */
#define MOD_NAME(x) const char *mod_name = x

/* Comms modules (loaded by the broker) must define mod_main().
 * (Other types of modules will have their own requirements).
 */
typedef int (mod_main_f)(flux_t h, zhash_t *args);
extern mod_main_f mod_main;

/* Read 'mod_name' from the specified module filename.
 * Caller must free the returned name.
 */
char *flux_modname (const char *filename);

/* Search a colon-separated list of directories (recursively) for a .so file
 * with the requested module name and return its path, or NULL on failure.
 * Caller must free the returned path.
 */
char *flux_modfind (const char *searchpath, const char *modname);


#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
