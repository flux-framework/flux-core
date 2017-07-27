#ifndef _KVS_TXN_PRIVATE_H
#define _KVS_TXN_PRIVATE_H

enum {
    TXN_GET_FIRST,
    TXN_GET_NEXT,
    TXN_GET_ALL
};
int txn_get (flux_kvs_txn_t *txn, int request, void *arg);

#endif /* !_KVS_TXN_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
