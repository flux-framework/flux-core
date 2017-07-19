#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

/* These interfaces are on their way to being deprecated */

int kvs_get (flux_t *h, const char *key, char **json_str);
int kvs_get_dir (flux_t *h, kvsdir_t **dirp, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

int kvs_put (flux_t *h, const char *key, const char *json_str);
int kvs_put_string (flux_t *h, const char *key, const char *val);
int kvs_put_int (flux_t *h, const char *key, int val);
int kvs_put_int64 (flux_t *h, const char *key, int64_t val);
int kvs_put_boolean (flux_t *h, const char *key, bool val);
int kvs_unlink (flux_t *h, const char *key);
int kvs_symlink (flux_t *h, const char *key, const char *target);
int kvs_mkdir (flux_t *h, const char *key);
int kvs_put_treeobj (flux_t *h, const char *key, const char *treeobj);
int kvs_copy (flux_t *h, const char *from, const char *to);
int kvs_move (flux_t *h, const char *from, const char *to);

int kvs_commit (flux_t *h, int flags);
int kvs_fence (flux_t *h, const char *name, int nprocs, int flags);

int kvsdir_get (kvsdir_t *dir, const char *key, char **json_str);
int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4)));

int kvsdir_put (kvsdir_t *dir, const char *key, const char *json_str);
int kvsdir_put_string (kvsdir_t *dir, const char *key, const char *val);
int kvsdir_put_int (kvsdir_t *dir, const char *key, int val);
int kvsdir_put_int64 (kvsdir_t *dir, const char *key, int64_t val);
int kvsdir_put_double (kvsdir_t *dir, const char *key, double val);
int kvsdir_put_boolean (kvsdir_t *dir, const char *key, bool val);

int kvsdir_unlink (kvsdir_t *dir, const char *key);
int kvsdir_symlink (kvsdir_t *dir, const char *key, const char *target);
int kvsdir_mkdir (kvsdir_t *dir, const char *key);


#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
