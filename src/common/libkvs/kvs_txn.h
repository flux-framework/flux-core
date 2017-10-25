#ifndef _FLUX_CORE_KVS_TXN_H
#define _FLUX_CORE_KVS_TXN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

typedef struct flux_kvs_txn flux_kvs_txn_t;

flux_kvs_txn_t *flux_kvs_txn_create (void);
void flux_kvs_txn_destroy (flux_kvs_txn_t *txn);

int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                      const char *key, const char *value);

/* N.B. splitting at 80 columns confuses python cffi parser */
int flux_kvs_txn_vpack (flux_kvs_txn_t *txn, int flags, const char *key, const char *fmt, va_list ap);

int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags, const char *key,
                       const char *fmt, ...);

int flux_kvs_txn_put_raw (flux_kvs_txn_t *txn, int flags,
                          const char *key, const void *data, int len);

int flux_kvs_txn_put_treeobj (flux_kvs_txn_t *txn, int flags,
                              const char *key, const char *treeobj);

int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                        const char *key);

int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                         const char *key);

int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *target);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_TXN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
