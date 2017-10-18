#ifndef _FLUX_CORE_KVS_DIR_H
#define _FLUX_CORE_KVS_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

/* The flux_kvsdir_t represents an unordered list of directory entries.
 * It is also overloaded as a container for a flux_t handle, a snapshot
 * reference, and a namespace placeholder, in support of legacy
 * flux_kvsdir_get() and flux_kvsdir_put() series of functions in
 * kvs_classic.h.  Those features may be deprecated in the future.
 */

typedef struct flux_kvsdir flux_kvsdir_t;
typedef struct flux_kvsitr flux_kvsitr_t;

/* Create/destroy/copy a flux_kvsdir_t.
 *
 * flux_kvsdir_create() creates a new object from 'json_str', an encoded
 * RFC 11 dir object.  'key' is the full key path associated with the
 * directory.  Optional: 'handle', is a flux_t handle, and 'rootref' is
 * a snapshot reference.  The optional arguments are  used to support the
 * legacy flux_kvsdir_get() and flux_kvsdir_put() series of functions.
 *
 * flux_kvsdir_incref() increments an internal reference count that is
 * decremented by each call to flux_kvsdir_destroy().  Resources
 * are not freed until the reference count reaches zero.  The object is
 * initially created with a reference count of one.
 */
flux_kvsdir_t *flux_kvsdir_create (flux_t *handle, const char *rootref,
                                   const char *key, const char *json_str);
void flux_kvsdir_destroy (flux_kvsdir_t *dir);

flux_kvsdir_t *flux_kvsdir_copy (const flux_kvsdir_t *dir);
void flux_kvsdir_incref (flux_kvsdir_t *dir);


/* flux_kvsitr_t is an iterator for walking the list of names in a
 * flux_kvsdir_t.  flux_kvsitr_next() returns NULL when the last
 * item is reached.
 */
flux_kvsitr_t *flux_kvsitr_create (const flux_kvsdir_t *dir);
void flux_kvsitr_destroy (flux_kvsitr_t *itr);
const char *flux_kvsitr_next (flux_kvsitr_t *itr);
void flux_kvsitr_rewind (flux_kvsitr_t *itr);

/* Get the number of keys in a directory.
 */
int flux_kvsdir_get_size (const flux_kvsdir_t *dir);

/* Test whether 'name' exists in 'dir'.
 * It is expected to be a single name, not fully qualified key path.
 */
bool flux_kvsdir_exists (const flux_kvsdir_t *dir, const char *name);

/* Test whether 'name' exists in 'dir' and is itself a directory.
 */
bool flux_kvsdir_isdir (const flux_kvsdir_t *dir, const char *name);

/* Test whether 'name' exists in 'dir' and is a symbolic link.
 */
bool flux_kvsdir_issymlink (const flux_kvsdir_t *dir, const char *name);

/* Access the original 'key', 'json_str', 'handle', and 'rootref' parameters
 * passed to flux_kvsdir_create ()
 */
const char *flux_kvsdir_key (const flux_kvsdir_t *dir);
const char *flux_kvsdir_tostring (const flux_kvsdir_t *dir);
void *flux_kvsdir_handle (const flux_kvsdir_t *dir);
const char *flux_kvsdir_rootref (const flux_kvsdir_t *dir);

/* Construct a fully-qualified key from flux_kvsdir_key() + "." + key.
 * Caller must free.
 */
char *flux_kvsdir_key_at (const flux_kvsdir_t *dir, const char *key);


#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_DIR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
