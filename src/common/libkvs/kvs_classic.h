#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

/* These interfaces are on their way to being deprecated */

int kvs_get (flux_t *h, const char *key, char **json_str);
int kvs_get_symlink (flux_t *h, const char *key, char **valp);
int kvs_get_treeobj (flux_t *h, const char *key, char **valp);
int kvs_get_dir (flux_t *h, kvsdir_t **dirp, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

int kvs_getat (flux_t *h, const char *treeobj,
               const char *key, char **json_str);
int kvs_get_dirat (flux_t *h, const char *treeobj,
                   const char *key, kvsdir_t **dirp);
int kvs_get_symlinkat (flux_t *h, const char *treeobj,
                       const char *key, char **val);

int kvsdir_get (kvsdir_t *dir, const char *key, char **json_str);
int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4)));
int kvsdir_get_string (kvsdir_t *dir, const char *key, char **valp);
int kvsdir_get_int (kvsdir_t *dir, const char *key, int *valp);
int kvsdir_get_int64 (kvsdir_t *dir, const char *key, int64_t *valp);
int kvsdir_get_double (kvsdir_t *dir, const char *key, double *valp);
int kvsdir_get_symlink (kvsdir_t *dir, const char *key, char **valp);


#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
