#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* These functions are soon to be deprecated and probably
 * should not be used in new code.
 */

/* flux_kvs_get() and flux_kvs_get_dir() combine a flux_kvs_lookup()
 * and flux_kvs_lookup_get() call in one function, thus they block
 * the calling thread while RPC's complete, and duplicate the returned
 * result leaving it up to the caller to free.
 *
 * flux_kvs_get_dir() includes printf-style (fmt, ...) varargs for
 * constructing the key argument on the fly.
 */

int flux_kvs_get (flux_t *h, const char *key, char **json_str);
int flux_kvs_get_dir (flux_t *h, flux_kvsdir_t **dirp, const char *fmt, ...)
                      __attribute__ ((format (printf, 3, 4)));

/* These functions are like their counterparts in "kvs_txn.h" except
 * they append operations to an "anonymous" transaction.
 * Use flux_kvs_commit_anon() or flux_kvs_fence_anon() to commit the
 * anonymous transaction.  Generally it is more clear to use an explicit
 * transaction.
 */

int flux_kvs_put (flux_t *h, const char *key, const char *json_str);
int flux_kvs_unlink (flux_t *h, const char *key);
int flux_kvs_symlink (flux_t *h, const char *key, const char *target);
int flux_kvs_mkdir (flux_t *h, const char *key);

/* flux_kvs_commit_anon() and flux_kvs_fence_anon() combine a flux_kvs_commit()
 * and a flux_future_get() call in one function, thus they block the calling
 * thread while RPC's complete.  These functions operate only on the
 * anonymous transaction (see above).
 */

int flux_kvs_commit_anon (flux_t *h, int flags);
int flux_kvs_fence_anon (flux_t *h, const char *name, int nprocs, int flags);

/* flux_kvsdir_get() and flux_kvsdir_get_dir() combine a flux_kvs_lookup()
 * and flux_kvs_get() call in one function, thus they block the calling
 * thread while RPC's complete, and duplicate the returned result leaving
 * it up to the caller to free.
 *
 * The flux_kvsdir_t object acts a container for the flux_t handle used
 * to fetch it; a root snapshot reference, if originally fetched with
 * flux_future_getat(); and the key used to fetch it.  The key supplied
 * to flux_kvsdir_get() or flux_kvsdir_get_dir() is combined with the original
 * key to construct a new key that is passed to flux_kvs_lookup().
 *
 * If the flux_kvsdir_t contains a root snapshot reference, flux_kvsdir_getat()
 * is used to fetch the new value, thus the new values are relative to the
 * snapshot, not the changing root.
 *
 * flux_kvsdir_get_dir() includes printf-style (fmt, ...) varargs for
 * constructing the key argument on the fly.
 */

int flux_kvsdir_get (const flux_kvsdir_t *dir,
                    const char *key, char **json_str);
int flux_kvsdir_get_dir (const flux_kvsdir_t *dir, flux_kvsdir_t **dirp,
                    const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4)));

/* These functions are like their counterparts in "kvs_txn.h" except
 * they append operations to an "anonymous" transaction, and construct
 * the key as a subdirectory of 'dir' as described above for flux_kvsdir_get().
 *
 * Use flux_kvs_commit_anon() or flux_kvs_fence_anon() to commit the
 * default transaction.  Generally it is more clear to use an explicit
 * transaction.  Keys can be constructed by manually combining
 * kvsdir_key_at (dir) + "." + key.
 */

int flux_kvsdir_put (const flux_kvsdir_t *dir, const char *key,
                    const char *json_str);
int flux_kvsdir_pack (const flux_kvsdir_t *dir, const char *key,
                    const char *fmt, ...);
int flux_kvsdir_unlink (const flux_kvsdir_t *dir, const char *key);
int flux_kvsdir_mkdir (const flux_kvsdir_t *dir, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
