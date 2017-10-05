#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* These interfaces are on their way to being deprecated */

int flux_kvs_get (flux_t *h, const char *key, char **json_str);
int flux_kvs_get_dir (flux_t *h, flux_kvsdir_t **dirp, const char *fmt, ...)
                      __attribute__ ((format (printf, 3, 4)));

int flux_kvs_put (flux_t *h, const char *key, const char *json_str);
int flux_kvs_unlink (flux_t *h, const char *key);
int flux_kvs_symlink (flux_t *h, const char *key, const char *target);
int flux_kvs_mkdir (flux_t *h, const char *key);

int flux_kvs_commit_anon (flux_t *h, int flags);
int flux_kvs_fence_anon (flux_t *h, const char *name, int nprocs, int flags);

int flux_kvsdir_get (flux_kvsdir_t *dir, const char *key, char **json_str);
int flux_kvsdir_get_dir (flux_kvsdir_t *dir, flux_kvsdir_t **dirp,
                    const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4)));

int flux_kvsdir_put (flux_kvsdir_t *dir, const char *key, const char *json_str);
int flux_kvsdir_pack (flux_kvsdir_t *dir, const char *key,
                      const char *fmt, ...);
int flux_kvsdir_put_string (flux_kvsdir_t *dir, const char *key,
                            const char *val);
int flux_kvsdir_put_int (flux_kvsdir_t *dir, const char *key, int val);
int flux_kvsdir_put_int64 (flux_kvsdir_t *dir, const char *key, int64_t val);
int flux_kvsdir_put_double (flux_kvsdir_t *dir, const char *key, double val);
int flux_kvsdir_put_boolean (flux_kvsdir_t *dir, const char *key, bool val);

int flux_kvsdir_unlink (flux_kvsdir_t *dir, const char *key);
int flux_kvsdir_mkdir (flux_kvsdir_t *dir, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
