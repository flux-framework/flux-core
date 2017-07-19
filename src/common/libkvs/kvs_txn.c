#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_txn_private.h"
#include "jansson_dirent.h"

#include "src/common/libutil/blobref.h"

struct flux_kvs_txn {
    json_t *ops;
    int cursor;
};

void flux_kvs_txn_destroy (flux_kvs_txn_t *txn)
{
    if (txn) {
        int saved_errno = errno;
        json_decref (txn->ops);
        free (txn);
        errno = saved_errno;
    }
}

flux_kvs_txn_t *flux_kvs_txn_create (void)
{
    flux_kvs_txn_t *txn = calloc (1, sizeof (*txn));
    if (!txn) {
        errno = ENOMEM;
        goto error;
    }
    if (!(txn->ops = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    return txn;
error:
    flux_kvs_txn_destroy (txn);
    return NULL;
}

static int validate_flags (int flags, int allowed)
{
    if ((flags & allowed) != flags) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int validate_op (json_t *op)
{
    const char *key;
    json_t *dirent = NULL;

    if (json_unpack (op, "{s:s}", "key", &key) < 0)
        goto error;
    if (strlen (key) == 0)
        goto error;
    if (json_unpack (op, "{s:n}", "dirent") == 0)
        ; // unlink sets dirent NULL
    else if (json_unpack (op, "{s:o}", "dirent", &dirent) == 0) {
        if (j_dirent_validate (dirent) < 0)
            goto error;
    } else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                      const char *key, const char *json_str)
{
    json_t *val;
    json_t *op = NULL;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, FLUX_KVS_TREEOBJ) < 0)
        goto error;
    if (!json_str)
        return flux_kvs_txn_unlink (txn, flags, key);
    if (!(val = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
        errno = EINVAL;
        goto error;
    }
    if ((flags & FLUX_KVS_TREEOBJ))
        op = json_pack ("{s:s s:o}", "key", key,
                                     "dirent", val);
    else
        op = json_pack ("{s:s s:{s:o}}", "key", key,
                                         "dirent", "FILEVAL", val);
    if (!op) {
        json_decref (val);
        errno = ENOMEM;
        goto error;
    }
    if (validate_op (op) < 0) {
        json_decref (op);
        goto error;
    }
    if (json_array_append_new (txn->ops, op) < 0) {
        json_decref (op);
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    return -1;
}

int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags,
                       const char *key, const char *fmt, ...)
{
    va_list ap;
    json_t *val;
    json_t *op = NULL;

    if (!txn || !key | !fmt) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, FLUX_KVS_TREEOBJ) < 0)
        goto error;
    va_start (ap, fmt);
    val = json_vpack_ex (NULL, 0, fmt, ap);
    va_end (ap);
    if (!val) {
        errno = EINVAL;
        goto error;
    }
    if ((flags & FLUX_KVS_TREEOBJ))
        op = json_pack ("{s:s s:o}", "key", key,
                                     "dirent", val);
    else
        op = json_pack ("{s:s s:{s:o}}", "key", key,
                                         "dirent", "FILEVAL", val);
    if (!op) {
        json_decref (val);
        errno = ENOMEM;
        goto error;
    }
    if (validate_op (op) < 0) {
        json_decref (op);
        goto error;
    }
    if (json_array_append_new (txn->ops, op) < 0) {
        json_decref (op);
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    return -1;
}

int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                        const char *key)
{
    json_t *op;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(op = json_pack ("{s:s s:{s:{}}}", "key", key,
                                            "dirent", "DIRVAL"))) {
        errno = ENOMEM;
        goto error;
    }
    if (validate_op (op) < 0) {
        json_decref (op);
        goto error;
    }
    if (json_array_append_new (txn->ops, op) < 0) {
        json_decref (op);
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    return -1;
}

int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                         const char *key)
{
    json_t *op;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(op = json_pack ("{s:s s:n}", "key", key,
                                       "dirent"))) {
        errno = ENOMEM;
        goto error;
    }
    if (validate_op (op) < 0) {
        json_decref (op);
        goto error;
    }
    if (json_array_append_new (txn->ops, op) < 0) {
        json_decref (op);
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    return -1;
}

int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *target)
{
    json_t *op;

    if (!txn || !key | !target) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(op = json_pack ("{s:s s:{s:s}}", "key", key,
                                           "dirent", "LINKVAL", target))) {
        errno = EINVAL;
        goto error;
    }
    if (validate_op (op) < 0) {
        json_decref (op);
        goto error;
    }
    if (json_array_append_new (txn->ops, op) < 0) {
        json_decref (op);
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    return -1;
}

/* for unit tests
 */
int txn_get (flux_kvs_txn_t *txn, int request, void *arg)
{
    switch (request) {
        case TXN_GET_FIRST:
            txn->cursor = 0;
            if (arg)
                *(json_t **)arg = json_array_get (txn->ops, txn->cursor);
            txn->cursor++;
            break;
        case TXN_GET_NEXT:
            if (arg)
                *(json_t **)arg = json_array_get (txn->ops, txn->cursor);
            txn->cursor++;
            break;
        case TXN_GET_ALL:
            if (arg)
                *(json_t **)arg = txn->ops;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    return 0;
}

flux_future_t *flux_kvs_fence (flux_t *h, int flags, const char *name,
                               int nprocs, flux_kvs_txn_t *txn)
{
    flux_kvs_txn_t *empty_txn = NULL;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!txn && !(txn = empty_txn = flux_kvs_txn_create ()))
        goto done;
    if (!(f = flux_rpc_pack (h, "kvs.fence", FLUX_NODEID_ANY, 0,
                             "{s:s s:i s:i s:O}",
                             "name", name,
                             "nprocs", nprocs,
                             "flags", flags,
                             "ops", txn->ops)))
        goto done;
done:
    saved_errno = errno;
    flux_kvs_txn_destroy (empty_txn);
    errno = saved_errno;
    return f;
}

flux_future_t *flux_kvs_commit (flux_t *h, int flags, flux_kvs_txn_t *txn)
{
    zuuid_t *uuid;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(uuid = zuuid_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (!(f = flux_kvs_fence (h, flags, zuuid_str (uuid), 1, txn)))
        goto done;
done:
    saved_errno = errno;
    zuuid_destroy (&uuid);
    errno = saved_errno;
    return f;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
