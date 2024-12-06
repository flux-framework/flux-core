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

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "kvs_txn_private.h"
#include "treeobj.h"

/* Some KVS entries, such as event logs, can have many appends.
 * Internally, a treeobject stores these appends as blobrefs in an
 * array.  Over time, these arrays can get very long, leading to
 * performance issues.
 *
 * One way to improve performance is to "compact" these appends in KVS
 * transactions before they are committed to the KVS.  If multiple
 * appends to the same key exist in a KVS transaction, combine them
 * into a single append.  For example, an append of "A" to the key
 * "foo", followed by an append of "B" to the key "foo", could be
 * combined into a single append of "AB".
 *
 * By combining these appends, we can decrease the number of blobrefs
 * stored in the blobref array.
 *
 * There can be complications with this approach, most notably if a
 * user overwrites an appended value in a single transaction.  For
 * example, if a user performs these operations to the same key in the
 * same KVS transaction:
 *
 * append "A"
 * write "B"
 * append "c"
 *
 * we cannot combine the appends of "A" and "C".  In this scenario, we
 * generate an EINVAL error to the caller, indicating that the
 * transaction will not allow compaction.
 */

struct append_data {
    void *data;
    size_t len;
};

struct compact_key {
    zlist_t *appends;
    int total_len;
    int index;
};

static void append_data_destroy (void *data)
{
    struct append_data *ad = data;
    if (ad) {
        free (ad->data);
        free (ad);
    }
}

static int append_data_save (struct compact_key *ck, json_t *dirent)
{
    struct append_data *ad = NULL;
    int saved_errno;

    if (!(ad = calloc (1, sizeof (*ad))))
        goto error;

    if (treeobj_decode_val (dirent, &ad->data, &ad->len) < 0)
        goto error;

    if (zlist_append (ck->appends, ad) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (ck->appends, ad, append_data_destroy, true);

    ck->total_len += ad->len;
    return 0;

error:
    saved_errno = errno;
    append_data_destroy (ad);
    errno = saved_errno;
    return -1;
}

static void compact_key_destroy (void *data)
{
    struct compact_key *ck = data;
    if (ck) {
        zlist_destroy (&ck->appends);
        free (ck);
    }
}

static struct compact_key *compact_key_create (int index)
{
    struct compact_key *ck = NULL;
    int saved_errno;

    if (!(ck = calloc (1, sizeof (*ck))))
        goto error;

    if (!(ck->appends = zlist_new ()))
        goto error;

    ck->index = index;
    return ck;

error:
    saved_errno = errno;
    compact_key_destroy (ck);
    errno = saved_errno;
    return NULL;
}

static int append_compact (struct compact_key *ck, json_t *ops_new)
{
    struct append_data *ad;
    json_t *dst;
    const char *dst_key;
    int dst_flags;
    json_t *dst_dirent;
    char *buf = NULL;
    int saved_errno, rc = -1;
    json_t *op = NULL;
    json_t *new_dirent = NULL;
    int offset = 0;

    /* if there is only one append total, it's just the original, we
     * don't have to do anything */
    if (zlist_size (ck->appends) == 1)
        return 0;

    /* zero length appends are legal, if all are zero length, no
     * modification necessary */
    if (!ck->total_len)
        return 0;

    if (!(dst = json_array_get (ops_new, ck->index))) {
        errno = EINVAL;
        goto error;
    }

    if (txn_decode_op (dst, &dst_key, &dst_flags, &dst_dirent) < 0) {
        errno = EINVAL;
        goto error;
    }

    if (!treeobj_is_val (dst_dirent)) {
        errno = EINVAL;
        goto error;
    }

    if (!(buf = malloc (ck->total_len)))
        goto error;

    ad = zlist_first (ck->appends);
    while (ad) {
        memcpy (buf + offset, ad->data, ad->len);
        offset += ad->len;
        ad = zlist_next (ck->appends);
    }

    if (!(new_dirent = treeobj_create_val (buf, offset)))
        goto error;

    if (txn_encode_op (dst_key, dst_flags, new_dirent, &op) < 0)
        goto error;

    if (json_array_set_new (ops_new, ck->index, op) < 0) {
        errno = ENOMEM;
        goto error;
    }
    op = NULL;

    rc = 0;
error:
    saved_errno = errno;
    json_decref (op);
    json_decref (new_dirent);
    free (buf);
    errno = saved_errno;
    return rc;
}

int txn_compact (flux_kvs_txn_t *txn)
{
    struct compact_key *ck;
    zhash_t *append_keys = NULL;
    json_t *ops_new;
    size_t len;
    int saved_errno, i;

    if (!txn) {
        errno = EINVAL;
        return -1;
    }

    len = json_array_size (txn->ops);
    if (len < 2)
        return 0;

    if (!(ops_new = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    if (!(append_keys = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }

    for (i = 0; i < len; i++) {
        json_t *entry;
        const char *key;
        int flags;
        json_t *dirent;

        if (!(entry = json_array_get (txn->ops, i))) {
            errno = EINVAL;
            goto error;
        }

        if (txn_decode_op (entry, &key, &flags, &dirent) < 0)
            goto error;

        ck = zhash_lookup (append_keys, key);
        if (ck) {
            /* If ops array has a non-append after an append, we
             * consider this an error.  We will not allow
             * consolidation under these special cases */
            if (flags != FLUX_KVS_APPEND) {
                errno = EINVAL;
                goto error;
            }
            else {
                if (append_data_save (ck, dirent) < 0)
                    goto error;
            }
        }
        else {
            if (flags == FLUX_KVS_APPEND) {
                json_t *cpy;
                int index;

                /* create copy of object, don't want to modify
                 * original
                 */
                if (!(cpy = json_copy (entry))) {
                    errno = ENOMEM;
                    goto error;
                }
                if (json_array_append_new (ops_new, cpy) < 0) {
                    json_decref (cpy);
                    errno = ENOMEM;
                    goto error;
                }
                index = json_array_size (ops_new) - 1;

                if (!(ck = compact_key_create (index))) {
                    errno = ENOMEM;
                    goto error;
                }
                if (zhash_insert (append_keys, key, ck) < 0) {
                    errno = EEXIST;
                    goto error;
                }
                zhash_freefn (append_keys, key, compact_key_destroy);

                if (append_data_save (ck, dirent) < 0)
                    goto error;
            }
            else {
                if (json_array_append (ops_new, entry) < 0) {
                    errno = ENOMEM;
                    goto error;
                }
            }
        }
    }

    ck = zhash_first (append_keys);
    while (ck) {
        if (append_compact (ck, ops_new) < 0)
            goto error;
        ck = zhash_next (append_keys);
    }

    json_decref (txn->ops);
    txn->ops = ops_new;
    zhash_destroy (&append_keys);
    return 0;

error:
    saved_errno = errno;
    json_decref (ops_new);
    zhash_destroy (&append_keys);
    errno = saved_errno;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
