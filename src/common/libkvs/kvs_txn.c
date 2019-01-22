/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <string.h>

#include "kvs_txn_private.h"
#include "treeobj.h"

/* A transaction is an ordered list of operations.
 * Each operation contains key, flags, and a "dirent" (RFC 11 tree object).
 * The operation assigns a new dirent to the key.  A NULL dirent removes
 * the key.  A commit operation accepts a transaction and applies the
 * whole thing, in order.  If any operation fails, the transaction is
 * not finalized, thus either all or none of the operations are applied.
 *
 * Raw versus JSON values:
 * All values are base64 encoded per RFC 11, even values that
 * are themselves JSON.  This is a change from the original design,
 * which stored only JSON values.
 *
 * NULL or empty values:
 * A zero-length value may be stored in the KVS via
 * flux_kvs_txn_put (value=NULL) or flux_kvs_txn_put_raw (data=NULL,len=0).
 * A NULL format string passed to flux_kvs_txn_pack() is invalid.
 */
struct flux_kvs_txn {
    json_t *ops;
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

/* Add an operation on dirent to the transaction.
 * Takes a reference on dirent so caller retains ownership.
 */
static int append_op_to_txn (flux_kvs_txn_t *txn, int flags,
                             const char *key, json_t *dirent)
{
    json_t *op = NULL;
    int saved_errno;

    if (txn_encode_op (key, flags, dirent, &op) < 0)
        goto error;
    if (json_array_append_new (txn->ops, op) < 0) {
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    saved_errno = errno;
    json_decref (op);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_put_raw (flux_kvs_txn_t *txn, int flags,
                          const char *key, const void *data, int len)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, FLUX_KVS_APPEND) < 0)
        goto error;
    if (!(dirent = treeobj_create_val (data, len)))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;

error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_put_treeobj (flux_kvs_txn_t *txn, int flags,
                              const char *key, const char *treeobj)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key || !treeobj) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(dirent = treeobj_decode (treeobj)))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;

error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_put (flux_kvs_txn_t *txn, int flags,
                      const char *key, const char *value)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, FLUX_KVS_APPEND) < 0)
        goto error;
    if (!(dirent = treeobj_create_val (value, value ? strlen (value) : 0)))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;

error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_vpack (flux_kvs_txn_t *txn, int flags,
                        const char *key, const char *fmt, va_list ap)
{
    json_t *val, *dirent = NULL;
    int saved_errno;
    char *s;

    if (!txn || !key || !fmt) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    val = json_vpack_ex (NULL, 0, fmt, ap);
    if (!val) {
        errno = EINVAL;
        goto error;
    }
    if (!(s = json_dumps (val, JSON_ENCODE_ANY))) {
        errno = ENOMEM;
        json_decref (val);
        goto error;
    }
    json_decref (val);
    if (!(dirent = treeobj_create_val (s, strlen (s)))) {
        free (s);
        goto error;
    }
    free (s);
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;

error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_pack (flux_kvs_txn_t *txn, int flags,
                       const char *key, const char *fmt, ...)
{
    va_list ap;
    int rc;

    if (!txn || !key || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    rc = flux_kvs_txn_vpack (txn, flags, key, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_kvs_txn_mkdir (flux_kvs_txn_t *txn, int flags,
                        const char *key)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(dirent = treeobj_create_dir ()))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;

error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_unlink (flux_kvs_txn_t *txn, int flags,
                         const char *key)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(dirent = json_null ()))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;
error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

int flux_kvs_txn_symlink (flux_kvs_txn_t *txn, int flags,
                          const char *key, const char *target)
{
    json_t *dirent = NULL;
    int saved_errno;

    if (!txn || !key || !target) {
        errno = EINVAL;
        goto error;
    }
    if (validate_flags (flags, 0) < 0)
        goto error;
    if (!(dirent = treeobj_create_symlink (target)))
        goto error;
    if (append_op_to_txn (txn, flags, key, dirent) < 0)
        goto error;
    json_decref (dirent);
    return 0;
error:
    saved_errno = errno;
    json_decref (dirent);
    errno = saved_errno;
    return -1;
}

/* kvs_txn_private.h */

int txn_get_op_count (flux_kvs_txn_t *txn)
{
    return json_array_size (txn->ops);
}

json_t *txn_get_ops (flux_kvs_txn_t *txn)
{
    return txn->ops;
}

int txn_get_op (flux_kvs_txn_t *txn, int index, json_t **op)
{
    json_t *entry = json_array_get (txn->ops, index);
    if (!entry) {
        errno = EINVAL;
        return -1;
    }
    if (op)
        *op = entry;
    return 0;
}

int txn_decode_op (json_t *op, const char **keyp, int *flagsp, json_t **direntp)
{
    const char *key;
    int flags;
    json_t *dirent;

    if (json_unpack (op, "{s:s s:i s:o !}",
                         "key", &key,
                         "flags", &flags,
                         "dirent", &dirent) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (keyp)
        *keyp = key;
    if (flagsp)
        *flagsp = flags;
    if (direntp)
        *direntp = dirent;
    return 0;
}

int txn_encode_op (const char *key, int flags, json_t *dirent, json_t **opp)
{
    json_t *op;

    if (!key || strlen (key) == 0 || !dirent
             || (!json_is_null (dirent) && treeobj_validate (dirent) < 0)) {
        errno = EINVAL;
        return -1;
    }
    if (validate_flags (flags, FLUX_KVS_APPEND) < 0)
        return -1;
    if (!(op = json_pack ("{s:s s:i s:O}",
                          "key", key,
                          "flags", flags,
                          "dirent", dirent))) {
        errno = ENOMEM;
        return -1;
    }
    *opp = op;
    return 0;

}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
