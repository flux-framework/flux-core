#ifndef _FLUX_CORE_KVS_LOOKUP_H
#define _FLUX_CORE_KVS_LOOKUP_H

#ifdef __cplusplus
extern "C" {
#endif

flux_future_t *flux_kvs_lookup (flux_t *h, int flags, const char *key);
flux_future_t *flux_kvs_lookupat (flux_t *h, int flags, const char *key,
                                  const char *treeobj);

int flux_kvs_lookup_get (flux_future_t *f, const char **value);
int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...);
int flux_kvs_lookup_get_raw (flux_future_t *f, const void **data, int *len);
int flux_kvs_lookup_get_treeobj (flux_future_t *f, const char **treeobj);
int flux_kvs_lookup_get_dir (flux_future_t *f, const flux_kvsdir_t **dir);
int flux_kvs_lookup_get_symlink (flux_future_t *f, const char **target);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
