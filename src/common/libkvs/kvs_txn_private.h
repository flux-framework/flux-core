#ifndef _KVS_TXN_PRIVATE_H
#define _KVS_TXN_PRIVATE_H

int txn_get_op_count (flux_kvs_txn_t *txn);

json_t *txn_get_ops (flux_kvs_txn_t *txn);

int txn_get_op (flux_kvs_txn_t *txn, int index, json_t **op);

#endif /* !_KVS_TXN_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
