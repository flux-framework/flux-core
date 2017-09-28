#ifndef _FLUX_CORE_KVS_LOOKUP_H
#define _FLUX_CORE_KVS_LOOKUP_H

enum kvs_lookup_flags {
    FLUX_KVS_READDIR = 1,
    FLUX_KVS_READLINK = 2,
    FLUX_KVS_TREEOBJ = 16,
};

flux_future_t *flux_kvs_lookup (flux_t *h, int flags, const char *key);
flux_future_t *flux_kvs_lookupat (flux_t *h, int flags, const char *key,
                                  const char *treeobj);

int flux_kvs_lookup_get (flux_future_t *f, const char **json_str);
int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...);
int flux_kvs_lookup_get_raw (flux_future_t *f, const void **data, int *len);

#endif /* !_FLUX_CORE_KVS_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
