#ifndef _FLUX_CORE_KVS_TXN_H
#define _FLUX_CORE_KVS_TXN_H

#include <flux/core.h>

enum kvs_commit_flags {
    FLUX_KVS_NO_MERGE = 1, /* disallow commits to be mergeable with others */
};

typedef struct flux_kvs_txn flux_kvs_txn_t;

flux_kvs_txn_t *flux_kvs_txn_create (void);
void flux_kvs_txn_destroy (flux_kvs_txn_t *txn);

int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                      const char *key, const char *json_str);

int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags,
                       const char *key, const char *fmt, ...);

int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                        const char *key);

int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                         const char *key);

int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *target);

flux_future_t *flux_kvs_commit (flux_t *h, int flags, flux_kvs_txn_t *txn);

flux_future_t *flux_kvs_fence (flux_t *h, int flags, const char *name,
                               int nprocs, flux_kvs_txn_t *txn);

#endif /* !_FLUX_CORE_KVS_TXN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
