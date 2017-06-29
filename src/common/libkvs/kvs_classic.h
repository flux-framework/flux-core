#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

/* These interfaces are on their way to being deprecated */

int kvs_get (flux_t *h, const char *key, char **json_str);
int kvs_get_string (flux_t *h, const char *key, char **valp);
int kvs_get_int (flux_t *h, const char *key, int *valp);
int kvs_get_int64 (flux_t *h, const char *key, int64_t *valp);
int kvs_get_double (flux_t *h, const char *key, double *valp);
int kvs_get_boolean (flux_t *h, const char *key, bool *valp);
int kvs_get_symlink (flux_t *h, const char *key, char **valp);
int kvs_get_treeobj (flux_t *h, const char *key, char **valp);

int kvs_getat (flux_t *h, const char *treeobj,
               const char *key, char **json_str);
int kvs_get_symlinkat (flux_t *h, const char *treeobj,
                       const char *key, char **val);

#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
