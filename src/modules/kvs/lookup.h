#include "cache.h"

struct lookup {
    /* inputs from user */
    struct cache *cache;
    int current_epoch;

    char *root_dir;
    char *root_ref;
    char *root_ref_copy;

    char *path;
    int flags;

    /* potential return values from lookup */
    json_object *val;           /* value of lookup */
    const char *missing_ref;    /* on stall, missing ref to load */
    int errnum;                 /* errnum if error */

    /* API internal */
    json_object *root_dirent;
};

typedef struct lookup lookup_t;

/* Initialize a lookup handle
 * - If root_ref is same as root_dir, can be set to NULL.
 */
lookup_t *lookup_create (struct cache *cache,
                         int current_epoch,
                         const char *root_dir,
                         const char *root_ref,
                         const char *path,
                         int flags);

/* Destroy a lookup handle */
void lookup_destroy (lookup_t *lh);

/* Lookup the key path in the KVS cache starting at root.
 *
 * Return true on success or error, error code is returned in errnum
 * and should be checked upon return.
 *
 * Return false if key name cannot be resolved.  Return missing
 * reference in missing_ref, which caller should then use to load missing
 * reference into KVS cache via rpc or otherwise.
 */
bool lookup (lookup_t *lh);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
