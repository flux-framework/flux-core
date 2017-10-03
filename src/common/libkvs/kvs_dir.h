#ifndef _FLUX_CORE_KVS_DIR_H
#define _FLUX_CORE_KVS_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kvsdir kvsdir_t;
typedef struct kvsdir_iterator kvsitr_t;

/* Destroy a kvsdir object returned from kvs_get_dir() or kvsdir_get_dir()
 */
kvsdir_t *kvsdir_create (flux_t *handle, const char *rootref,
                         const char *key, const char *json_str);
void kvsdir_destroy (kvsdir_t *dir);
void kvsdir_incref (kvsdir_t *dir);
const char *kvsdir_tostring (kvsdir_t *dir);

/* An iterator interface for walking the list of names in a kvsdir_t
 * returned by kvs_get_dir().  kvsitr_create() always succeeds.
 * kvsitr_next() returns NULL when the last item is reached.
 */
kvsitr_t *kvsitr_create (kvsdir_t *dir);
void kvsitr_destroy (kvsitr_t *itr);
const char *kvsitr_next (kvsitr_t *itr);
void kvsitr_rewind (kvsitr_t *itr);

/* Test attributes of 'name', relative to kvsdir object.
 * This is intended for testing names returned by kvsitr_next (no recursion).
 * Symlinks are not dereferenced, i.e. symlink pointing to dir will read
 * issymlink=true, isdir=false.
 */
bool kvsdir_exists (kvsdir_t *dir, const char *name);
bool kvsdir_isdir (kvsdir_t *dir, const char *name);
bool kvsdir_issymlink (kvsdir_t *dir, const char *name);

/* Get key associated with a directory or directory entry.
 * Both functions always succeed.
 */
const char *kvsdir_key (kvsdir_t *dir);
char *kvsdir_key_at (kvsdir_t *dir, const char *key); /* caller frees result */
void *kvsdir_handle (kvsdir_t *dir);
const char *kvsdir_rootref (kvsdir_t *dir);

/* Get the number of keys in a directory.
 */
int kvsdir_get_size (kvsdir_t *dir);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_DIR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
