#ifndef _FLUX_CORE_MODULE_H
#define _FLUX_CORE_MODULE_H

/* Module management messages are constructed according to Flux RFC 5.
 * https://github.com/flux-framework/rfc/blob/master/spec_5.adoc
 */

#include <stdint.h>

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Module states, for embedding in keepalive messages (rfc 5)
 */
enum {
    FLUX_MODSTATE_INIT           = 0,
    FLUX_MODSTATE_SLEEPING       = 1,
    FLUX_MODSTATE_RUNNING        = 2,
    FLUX_MODSTATE_FINALIZING     = 3,
    FLUX_MODSTATE_EXITED         = 4,
};

/**
 ** High level module management functions
 **/

/* lsmod callback - return 0 on success, -1 to stop iteration and
 * have flux_lsmod() return error.
 * Note: 'nodeset' will be NULL when called from flux_lsmod().
 */
typedef int (flux_lsmod_f)(const char *name, int size, const char *digest,
                           int idle, int status,
                           const char *nodeset, void *arg);

/* Send a request to 'service' to list loaded modules (null for comms mods).
 * On success, the 'cb' function is called for each module with 'arg'.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_lsmod (flux_t *h, uint32_t nodeid, const char *service,
                flux_lsmod_f cb, void *arg);

/* Send a request to remove a module 'name'.
 * The request is sent to a service determined by parsing 'name'.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_rmmod (flux_t *h, uint32_t nodeid, const char *name);

/* Send a request to insert a module 'path'.
 * The request is sent to a service determined by parsing the module's name,
 * as defined by its symbol 'mod_name' (found by opening 'path').  Pass args
 * described by 'argc' and 'argv' to the module's 'mod_main' function.
 * Returns 0 on success, -1 with errno set on failure.
 */
int flux_insmod (flux_t *h, uint32_t nodeid, const char *path,
                int argc, char **argv);


/**
 ** Mandatory symbols for modules
 **/

#define MOD_NAME(x) const char *mod_name = x
typedef int (mod_main_f)(flux_t *h, int argc, char *argv[]);


/**
 ** Convenience functions for services implementing module extensions
 **/

/* Read the value of 'mod_name' from the specified module filename.
 * Caller must free the returned name.  Returns NULL on failure.
 */
char *flux_modname (const char *filename);

/* Search a colon-separated list of directories (recursively) for a .so file
 * with the requested module name and return its path, or NULL on failure.
 * Caller must free the returned path.
 */
char *flux_modfind (const char *searchpath, const char *modname);

/* Encode/decode lsmod payload
 * 'flux_modlist_t' is an intermediate object that can encode/decode
 * to/from a JSON string, and provides accessors for module list entries.
 */
typedef struct flux_modlist_struct flux_modlist_t;

flux_modlist_t *flux_modlist_create (void);
void flux_modlist_destroy (flux_modlist_t *mods);
int flux_modlist_append (flux_modlist_t *mods, const char *name, int size,
                            const char *digest, int idle, int status);
int flux_modlist_count (flux_modlist_t *mods);
int flux_modlist_get (flux_modlist_t *mods, int idx, const char **name,
                                int *size, const char **digest, int *idle,
                                int *status);

char *flux_lsmod_json_encode (flux_modlist_t *mods);
flux_modlist_t *flux_lsmod_json_decode (const char *json_str);


/* Encode/decode rmmod payload.
 * Caller must free the string returned by encode.
 */
char *flux_rmmod_json_encode (const char *name);
int flux_rmmod_json_decode (const char *json_str, char **name);

/* Encode/decode insmod payload.
 * Caller must free the string returned by encode.
 */
char *flux_insmod_json_encode (const char *path, int argc, char **argv);
int flux_insmod_json_decode (const char *json_str, char **path,
                             char **argz, size_t *argz_len);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
