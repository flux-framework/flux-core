#ifndef _FLUX_CORE_KVS_DIR_H
#define _FLUX_CORE_KVS_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_kvsdir flux_kvsdir_t;
typedef struct kvsdir_iterator kvsitr_t;

/* Destroy a kvsdir object returned from kvs_get_dir() or kvsdir_get_dir()
 */
flux_kvsdir_t *kvsdir_create (flux_t *handle, const char *rootref,
                              const char *key, const char *json_str);
void kvsdir_destroy (flux_kvsdir_t *dir);
void kvsdir_incref (flux_kvsdir_t *dir);
const char *kvsdir_tostring (flux_kvsdir_t *dir);

/* An iterator interface for walking the list of names in a flux_kvsdir_t
 * returned by kvs_get_dir().  kvsitr_create() always succeeds.
 * kvsitr_next() returns NULL when the last item is reached.
 */
kvsitr_t *kvsitr_create (flux_kvsdir_t *dir);
void kvsitr_destroy (kvsitr_t *itr);
const char *kvsitr_next (kvsitr_t *itr);
void kvsitr_rewind (kvsitr_t *itr);

/* Test attributes of 'name', relative to kvsdir object.
 * This is intended for testing names returned by kvsitr_next (no recursion).
 * Symlinks are not dereferenced, i.e. symlink pointing to dir will read
 * issymlink=true, isdir=false.
 */
bool kvsdir_exists (flux_kvsdir_t *dir, const char *name);
bool kvsdir_isdir (flux_kvsdir_t *dir, const char *name);
bool kvsdir_issymlink (flux_kvsdir_t *dir, const char *name);

/* Get key associated with a directory or directory entry.
 * Both functions always succeed.
 */
const char *kvsdir_key (flux_kvsdir_t *dir);
char *kvsdir_key_at (flux_kvsdir_t *dir, const char *key); /* caller frees result */
void *kvsdir_handle (flux_kvsdir_t *dir);
const char *kvsdir_rootref (flux_kvsdir_t *dir);

/* Get the number of keys in a directory.
 */
int kvsdir_get_size (flux_kvsdir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_DIR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
