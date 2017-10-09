#ifndef _FLUX_CORE_KVS_DIR_H
#define _FLUX_CORE_KVS_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_kvsdir flux_kvsdir_t;
typedef struct flux_kvsitr flux_kvsitr_t;

flux_kvsdir_t *flux_kvsdir_create (flux_t *handle, const char *rootref,
                                   const char *key, const char *json_str);
flux_kvsdir_t *flux_kvsdir_copy (const flux_kvsdir_t *dir);
void flux_kvsdir_destroy (flux_kvsdir_t *dir);
void flux_kvsdir_incref (flux_kvsdir_t *dir);
const char *flux_kvsdir_tostring (const flux_kvsdir_t *dir);

/* An iterator interface for walking the list of names in a flux_kvsdir_t
 * returned by kvs_get_dir().  flux_kvsitr_create() always succeeds.
 * flux_kvsitr_next() returns NULL when the last item is reached.
 */
flux_kvsitr_t *flux_kvsitr_create (const flux_kvsdir_t *dir);
void flux_kvsitr_destroy (flux_kvsitr_t *itr);
const char *flux_kvsitr_next (flux_kvsitr_t *itr);
void flux_kvsitr_rewind (flux_kvsitr_t *itr);

/* Test attributes of 'name', relative to kvsdir object.
 * Intended for testing names returned by flux_kvsitr_next (no recursion).
 * Symlinks are not dereferenced, i.e. symlink pointing to dir will read
 * issymlink=true, isdir=false.
 */
bool flux_kvsdir_exists (const flux_kvsdir_t *dir, const char *name);
bool flux_kvsdir_isdir (const flux_kvsdir_t *dir, const char *name);
bool flux_kvsdir_issymlink (const flux_kvsdir_t *dir, const char *name);

/* Get key associated with a directory or directory entry.
 * Both functions always succeed.
 */
const char *flux_kvsdir_key (const flux_kvsdir_t *dir);

/* caller frees result */
char *flux_kvsdir_key_at (const flux_kvsdir_t *dir, const char *key);

void *flux_kvsdir_handle (const flux_kvsdir_t *dir);
const char *flux_kvsdir_rootref (const flux_kvsdir_t *dir);

/* Get the number of keys in a directory.
 */
int flux_kvsdir_get_size (const flux_kvsdir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_DIR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
