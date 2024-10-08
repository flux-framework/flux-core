/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libccan/ccan/base64/base64.h"
#include "src/common/libutil/macros.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libkvs/kvs_commit.h"
#include "src/common/libkvs/kvs_txn_private.h"
#include "src/common/libkvs/kvs_util_private.h"
#include "ccan/str/str.h"

#include "kvstxn.h"

struct kvstxn_mgr {
    struct cache *cache;
    const char *ns_name;
    const char *hash_name;
    int noop_stores;            /* for kvs.stats-get, etc.*/
    zlist_t *ready;
    flux_t *h;
    void *aux;
};

struct kvstxn {
    int errnum;
    int aux_errnum;
    unsigned int blocked:1;
    json_t *ops;
    json_t *keys;
    json_t *names;
    int flags;                  /* kvs flags from request caller */
    int internal_flags;         /* special kvstxn api internal flags */
    json_t *rootcpy;   /* working copy of root dir */
    const json_t *rootdir;      /* source of rootcpy above */
    struct cache_entry *entry;  /* for reference counting rootdir above */
    struct cache_entry *newroot_entry;  /* for reference counting new root */
    char newroot[BLOBREF_MAX_STRING_SIZE];
    zlist_t *missing_refs_list;
    zlist_t *dirty_cache_entries_list;
    flux_future_t *f_sync_content_flush;
    flux_future_t *f_sync_checkpoint;
    bool processing;            /* kvstxn is being processed */
    bool merged;                /* kvstxn is a merger of transactions */
    bool merge_component;       /* kvstxn is member of a merger */
    kvstxn_mgr_t *ktm;
    /* State transitions
     *
     * INIT - perform initializations / checks
     * LOAD_ROOT - load KVS root
     *           - if needed, report missing refs to caller and stall
     * APPLY_OPS - apply changes to KVS
     *           - if needed, report missing refs to caller and stall
     * STORE - generate dirty entries for caller to store
     * GENERATE_KEYS - stall until stores complete
     *               - generate keys modified in txn
     * SYNC_CONTENT_FLUSH - call content.flush (for FLUX_KVS_SYNC)
     * SYNC_CHECKPOINT - call kvs_checkpoint_commit (for FLUX_KVS_SYNC)
     * FINISHED - end state
     *
     * INIT -> LOAD_ROOT
     * LOAD_ROOT -> APPLY_OPS
     * LOAD_ROOT -> GENERATE_KEYS (if no ops)
     * APPLY_OPS -> STORE
     * STORE -> GENERATE_KEYS
     * GENERATE_KEYS -> FINISHED
     * GENERATE_KEYS -> SYNC_CONTENT_FLUSH
     * SYNC_CONTENT_FLUSH -> SYNC_CHECKPOINT
     * SYNC_CHECKPOINT -> FINISHED
     */
    enum {
        KVSTXN_STATE_INIT = 1,
        KVSTXN_STATE_LOAD_ROOT = 2,
        KVSTXN_STATE_APPLY_OPS = 3,
        KVSTXN_STATE_STORE = 4,
        KVSTXN_STATE_GENERATE_KEYS = 5,
        KVSTXN_STATE_SYNC_CONTENT_FLUSH = 6,
        KVSTXN_STATE_SYNC_CHECKPOINT = 7,
        KVSTXN_STATE_FINISHED = 8,
    } state;
};

static void kvstxn_destroy (kvstxn_t *kt)
{
    if (kt) {
        json_decref (kt->ops);
        json_decref (kt->keys);
        json_decref (kt->names);
        json_decref (kt->rootcpy);
        cache_entry_decref (kt->entry);
        cache_entry_decref (kt->newroot_entry);
        if (kt->missing_refs_list)
            zlist_destroy (&kt->missing_refs_list);
        if (kt->dirty_cache_entries_list)
            zlist_destroy (&kt->dirty_cache_entries_list);
        flux_future_destroy (kt->f_sync_content_flush);
        flux_future_destroy (kt->f_sync_checkpoint);
        free (kt);
    }
}

static kvstxn_t *kvstxn_create (kvstxn_mgr_t *ktm,
                                const char *name,
                                json_t *ops,
                                int flags,
                                int internal_flags)
{
    kvstxn_t *kt;

    if (!(kt = calloc (1, sizeof (*kt))))
        goto error_enomem;
    if (ops) {
        if (!(kt->ops = json_copy (ops)))
            goto error_enomem;
    }
    else {
        if (!(kt->ops = json_array ()))
            goto error_enomem;
    }
    if (!(kt->names = json_array ()))
        goto error_enomem;
    if (name) {
        json_t *s;
        if (!(s = json_string (name)))
            goto error_enomem;
        if (json_array_append_new (kt->names, s) < 0) {
            json_decref (s);
            goto error_enomem;
        }
    }
    kt->flags = flags;
    kt->internal_flags = internal_flags;
    if (!(kt->missing_refs_list = zlist_new ()))
        goto error_enomem;
    zlist_autofree (kt->missing_refs_list);
    if (!(kt->dirty_cache_entries_list = zlist_new ()))
        goto error_enomem;
    kt->ktm = ktm;
    kt->state = KVSTXN_STATE_INIT;
    return kt;
 error_enomem:
    kvstxn_destroy (kt);
    errno = ENOMEM;
    return NULL;
}

int kvstxn_get_errnum (kvstxn_t *kt)
{
    return kt->errnum;
}

int kvstxn_get_aux_errnum (kvstxn_t *kt)
{
    return kt->aux_errnum;
}

int kvstxn_set_aux_errnum (kvstxn_t *kt, int errnum)
{
    kt->aux_errnum = errnum;
    return kt->aux_errnum;
}

bool kvstxn_fallback_mergeable (kvstxn_t *kt)
{
    if (kt->merged)
        return true;
    return false;
}

json_t *kvstxn_get_ops (kvstxn_t *kt)
{
    return kt->ops;
}

json_t *kvstxn_get_names (kvstxn_t *kt)
{
    return kt->names;
}

int kvstxn_get_flags (kvstxn_t *kt)
{
    return kt->flags;
}

int kvstxn_get_internal_flags (kvstxn_t *kt)
{
    return kt->internal_flags;
}

const char *kvstxn_get_namespace (kvstxn_t *kt)
{
    return kt->ktm->ns_name;
}

void *kvstxn_get_aux (kvstxn_t *kt)
{
    return kt->ktm->aux;
}

const char *kvstxn_get_newroot_ref (kvstxn_t *kt)
{
    if (kt->state == KVSTXN_STATE_FINISHED)
        return kt->newroot;
    return NULL;
}

json_t *kvstxn_get_keys (kvstxn_t *kt)
{
    if (kt->state == KVSTXN_STATE_FINISHED)
        return kt->keys;
    return NULL;
}

/* On error we should cleanup anything on the dirty cache list
 * that has not yet been passed to the user.  Because this has not
 * been passed to the user, there should be no waiters and the
 * cache_entry_clear_dirty() should always succeed in clearing the
 * bit.
 *
 * As of the writing of this code, it should also be impossible
 * for the cache_remove_entry() to fail.  In the rare case of two
 * callers kvs-get and kvs.put-ing items that end up at the
 * blobref in the cache, any waiters for a valid cache entry would
 * have been satisfied when the dirty cache entry was put onto
 * this dirty cache list (i.e. in store_cache() below when
 * cache_entry_set_raw() was called).
 */
void kvstxn_cleanup_dirty_cache_entry (kvstxn_t *kt, struct cache_entry *entry)
{
    if (kt->state == KVSTXN_STATE_STORE
        || kt->state == KVSTXN_STATE_GENERATE_KEYS) {
        char ref[BLOBREF_MAX_STRING_SIZE];
        const void *data;
        int len;
        __attribute__((unused)) int ret;

        /* special case, must clear */
        if (kt->newroot_entry == entry)
            kt->newroot_entry = NULL;

        cache_entry_decref (entry);
        assert (cache_entry_get_valid (entry) == true);
        assert (cache_entry_get_dirty (entry) == true);
        ret = cache_entry_clear_dirty (entry);
        assert (ret == 0);
        assert (cache_entry_get_dirty (entry) == false);

        ret = cache_entry_get_raw (entry, &data, &len);
        assert (ret == 0);

        blobref_hash (kt->ktm->hash_name, data, len, ref, sizeof (ref));

        ret = cache_remove_entry (kt->ktm->cache, ref);
        assert (ret == 1);
    }
}

static void cleanup_dirty_cache_list (kvstxn_t *kt)
{
    struct cache_entry *entry;

    while ((entry = zlist_pop (kt->dirty_cache_entries_list)))
        kvstxn_cleanup_dirty_cache_entry (kt, entry);
}

static int kvstxn_add_dirty_cache_entry (kvstxn_t *kt, struct cache_entry *entry)
{
    cache_entry_incref (entry);
    if (zlist_push (kt->dirty_cache_entries_list, entry) < 0) {
        /* cache_entry_decref() called in kvstxn_cleanup_dirty_cache_entry() */
        kvstxn_cleanup_dirty_cache_entry (kt, entry);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Store object 'o' under key 'ref' in local cache.
 * Object reference is still owned by the caller.
 * 'is_raw' indicates this data is a json string w/ base64 value and
 * should be flushed to the content store as raw data after it is
 * decoded.  Otherwise, the json object should be a treeobj.
 * Returns -1 on error, 0 on success entry already there, 1 on success
 * entry needs to be flushed to content store
 */
static int store_cache (kvstxn_t *kt, json_t *o,
                        bool is_raw, char *ref, int ref_len,
                        struct cache_entry **entryp)
{
    struct cache_entry *entry;
    int saved_errno, rc;
    const char *xdata;
    char *data = NULL;
    size_t xlen, databuflen;
    ssize_t datalen = 0;

    if (is_raw) {
        xdata = json_string_value (o);
        xlen = strlen (xdata);
        databuflen = base64_decoded_length (xlen);
        if (databuflen > 0) {
            if (!(data = malloc (databuflen))) {
                flux_log_error (kt->ktm->h, "malloc");
                goto error;
            }
            if ((datalen = base64_decode (data, databuflen, xdata, xlen)) < 0) {
                errno = EPROTO;
                goto error;
            }
        }
    }
    else {
        if (treeobj_validate (o) < 0 || !(data = treeobj_encode (o))) {
            flux_log_error (kt->ktm->h, "%s: treeobj_encode", __FUNCTION__);
            goto error;
        }
        datalen = strlen (data);
    }
    if (blobref_hash (kt->ktm->hash_name, data, datalen, ref, ref_len) < 0) {
        flux_log_error (kt->ktm->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (kt->ktm->cache, ref))) {
        if (!(entry = cache_entry_create (ref))) {
            flux_log_error (kt->ktm->h, "%s: cache_entry_create", __FUNCTION__);
            goto error;
        }
        if (cache_insert (kt->ktm->cache, entry) < 0) {
            cache_entry_destroy (entry);
            flux_log_error (kt->ktm->h, "%s: cache_insert", __FUNCTION__);
            goto error;
        }
    }
    if (cache_entry_get_valid (entry)) {
        kt->ktm->noop_stores++;
        rc = 0;
    }
    else {
        if (cache_entry_set_raw (entry, data, datalen) < 0) {
            __attribute__((unused)) int ret;
            ret = cache_remove_entry (kt->ktm->cache, ref);
            assert (ret == 1);
            goto error;
        }
        if (cache_entry_set_dirty (entry, true) < 0) {
            flux_log_error (kt->ktm->h, "%s: cache_entry_set_dirty",__FUNCTION__);
            __attribute__((unused)) int ret;
            ret = cache_remove_entry (kt->ktm->cache, ref);
            assert (ret == 1);
            goto error;
        }
        rc = 1;
    }
    *entryp = entry;
    free (data);
    return rc;

 error:
    saved_errno = errno;
    free (data);
    errno = saved_errno;
    return -1;
}

/* Store DIRVAL objects, converting them to DIRREFs.
 * Store (large) FILEVAL objects, converting them to FILEREFs.
 * Return 0 on success, -1 on error
 */
static int kvstxn_unroll (kvstxn_t *kt, json_t *dir)
{
    json_t *dir_entry;
    json_t *dir_data;
    json_t *ktmp;
    char ref[BLOBREF_MAX_STRING_SIZE];
    int ret;
    struct cache_entry *entry;
    void *iter;

    assert (treeobj_is_dir (dir));

    if (!(dir_data = treeobj_get_data (dir)))
        return -1;

    iter = json_object_iter (dir_data);

    /* Do not use json_object_foreach(), unsafe to modify via
     * json_object_set() while iterating.
     */
    while (iter) {
        dir_entry = json_object_iter_value (iter);
        if (treeobj_is_dir (dir_entry)) {
            if (kvstxn_unroll (kt, dir_entry) < 0) /* depth first */
                return -1;
            if ((ret = store_cache (kt, dir_entry,
                                    false, ref, sizeof (ref), &entry)) < 0)
                return -1;
            if (ret) {
                if (kvstxn_add_dirty_cache_entry (kt, entry) < 0)
                    return -1;
            }
            if (!(ktmp = treeobj_create_dirref (ref)))
                return -1;
            if (json_object_iter_set_new (dir, iter, ktmp) < 0) {
                json_decref (ktmp);
                errno = ENOMEM;
                return -1;
            }
        }
        else if (treeobj_is_val (dir_entry)) {
            json_t *val_data;

            if (!(val_data = treeobj_get_data (dir_entry)))
                return -1;
            if (json_string_length (val_data) > BLOBREF_MAX_STRING_SIZE) {
                if ((ret = store_cache (kt, val_data,
                                        true, ref, sizeof (ref), &entry)) < 0)
                    return -1;
                if (ret) {
                    if (kvstxn_add_dirty_cache_entry (kt, entry) < 0)
                        return -1;
                }
                if (!(ktmp = treeobj_create_valref (ref)))
                    return -1;
                if (json_object_iter_set_new (dir, iter, ktmp) < 0) {
                    json_decref (ktmp);
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
        iter = json_object_iter_next (dir_data, iter);
    }

    return 0;
}

static int kvstxn_val_data_to_cache (kvstxn_t *kt,
                                     json_t *val, char *ref, int ref_len)
{
    struct cache_entry *entry;
    json_t *val_data;
    int ret;

    if (!(val_data = treeobj_get_data (val)))
        return -1;

    if ((ret = store_cache (kt, val_data,
                            true, ref, ref_len, &entry)) < 0)
        return -1;

    if (ret) {
        if (kvstxn_add_dirty_cache_entry (kt, entry) < 0)
            return -1;
    }

    return 0;
}

static int kvstxn_append (kvstxn_t *kt, json_t *dirent,
                          json_t *dir, const char *final_name, bool *append)
{
    json_t *entry;

    if (!treeobj_is_val (dirent)) {
        errno = EPROTO;
        return -1;
    }

    entry = treeobj_get_entry (dir, final_name);

    if (!entry) {
        /* entry not found, treat like normal insertion */
        if (treeobj_insert_entry (dir, final_name, dirent) < 0)
            return -1;
        /* N.B. although this is an "insert", we still treat this as
         * an "append".  If we don't, the "append" could be issued
         * twice, leading to duplicated data.  See issue #6207. */
        (*append) = true;
    }
    else if (treeobj_is_valref (entry)) {
        char ref[BLOBREF_MAX_STRING_SIZE];
        json_t *cpy;

        /* treeobj is valref, so we need to append the new data's
         * blobref to this tree object.  Before doing so, we must save
         * off the new data to the cache and mark it dirty for
         * flushing later (if necessary)
         *
         * Note that we make a copy of the original entry and
         * re-insert it into the directory.  We do not want to
         * accidentally alter any json object pointers that may be
         * sitting in the KVS cache.
         */

        if (kvstxn_val_data_to_cache (kt, dirent, ref,
                                      sizeof (ref)) < 0)
            return -1;

        if (!(cpy = treeobj_deep_copy (entry)))
            return -1;

        if (treeobj_append_blobref (cpy, ref) < 0) {
            json_decref (cpy);
            return -1;
        }

        /* To improve performance, call
         * treeobj_insert_entry_novalidate() instead of
         * treeobj_insert_entry(), as the former will not call
         * treeobj_validate() on the internal valref treeobj array,
         * thus avoiding expensive checks if the array is long.  Since
         * we're appending a blobref to this KVS entry, we really only
         * need to check the new blobref for validity.  The check done
         * by treeobj_append_blobref() should be sufficient. */

        if (treeobj_insert_entry_novalidate (dir, final_name, cpy) < 0) {
            json_decref (cpy);
            return -1;
        }

        json_decref (cpy);
        (*append) = true;
    }
    else if (treeobj_is_val (entry)) {
        json_t *ktmp;
        char ref1[BLOBREF_MAX_STRING_SIZE];
        char ref2[BLOBREF_MAX_STRING_SIZE];

        /* treeobj entry is val, so we need to convert the treeobj
         * into a valref first.  Then the procedure is basically the
         * same as the treeobj valref case above.
         */

        if (kvstxn_val_data_to_cache (kt, entry, ref1, sizeof (ref1)) < 0)
            return -1;

        if (kvstxn_val_data_to_cache (kt, dirent, ref2, sizeof (ref2)) < 0)
            return -1;

        if (!(ktmp = treeobj_create_valref (ref1)))
            return -1;

        if (treeobj_append_blobref (ktmp, ref2) < 0) {
            json_decref (ktmp);
            return -1;
        }

        if (treeobj_insert_entry (dir, final_name, ktmp) < 0) {
            json_decref (ktmp);
            return -1;
        }

        json_decref (ktmp);
        (*append) = true;
    }
    else if (treeobj_is_symlink (entry)) {
        /* Could use EPERM - operation not permitted, but want to
         * avoid confusion with "common" errnos, we'll use this one
         * instead. */
        errno = EOPNOTSUPP;
        return -1;
    }
    else if (treeobj_is_dir (entry)
             || treeobj_is_dirref (entry)) {
        errno = EISDIR;
        return -1;
    }
    else {
        char *s = json_dumps (entry, JSON_ENCODE_ANY);
        flux_log (kt->ktm->h, LOG_ERR, "%s: corrupt treeobj: %p, %s",
                  __FUNCTION__, entry, s);
        free (s);
        errno = ENOTRECOVERABLE;
        return -1;
    }
    return 0;
}

/* link (key, dirent) into directory 'dir'.
 */
static int kvstxn_link_dirent (kvstxn_t *kt,
                               json_t *rootdir, const char *key,
                               json_t *dirent, int flags,
                               const char **missing_ref,
                               bool *append)
{
    char *cpy = NULL;
    char *next, *name;
    json_t *dir = rootdir;
    json_t *subdir = NULL, *dir_entry;
    int saved_errno, rc = -1;

    if (!(cpy = kvs_util_normalize_key (key, NULL))) {
        saved_errno = errno;
        goto done;
    }

    name = cpy;

    /* Special case root
     */
    if (streq (name, ".")) {
        saved_errno = EINVAL;
        goto done;
    }

    /* This is the first part of a key with multiple path components.
     * Make sure that it is a treeobj dir, then recurse on the
     * remaining path components.
     */
    while ((next = strchr (name, '.'))) {
        *next++ = '\0';

        if (!treeobj_is_dir (dir)) {
            saved_errno = ENOTRECOVERABLE;
            goto done;
        }

        if (!(dir_entry = treeobj_get_entry (dir, name))) {
            if (json_is_null (dirent)) /* key deletion - it doesn't exist so return */
                goto success;
            if (!(subdir = treeobj_create_dir ())) {
                saved_errno = errno;
                goto done;
            }
            /* subdir just created above, no need to validate */
            if (treeobj_insert_entry_novalidate (dir, name, subdir) < 0) {
                saved_errno = errno;
                json_decref (subdir);
                goto done;
            }
            json_decref (subdir);
        } else if (treeobj_is_dir (dir_entry)) {
            subdir = dir_entry;
        } else if (treeobj_is_dirref (dir_entry)) {
            struct cache_entry *entry;
            const char *ref;
            const json_t *subdirktmp;
            int refcount;

            if ((refcount = treeobj_get_count (dir_entry)) < 0) {
                saved_errno = errno;
                goto done;
            }

            if (refcount != 1) {
                flux_log (kt->ktm->h, LOG_ERR, "invalid dirref count: %d",
                          refcount);
                saved_errno = ENOTRECOVERABLE;
                goto done;
            }

            if (!(ref = treeobj_get_blobref (dir_entry, 0))) {
                saved_errno = errno;
                goto done;
            }

            if (!(entry = cache_lookup (kt->ktm->cache, ref))
                || !cache_entry_get_valid (entry)) {
                *missing_ref = ref;
                goto success; /* stall */
            }

            if (!(subdirktmp = cache_entry_get_treeobj (entry))) {
                saved_errno = ENOTRECOVERABLE;
                goto done;
            }

            /* do not corrupt store by modifying orig. */
            if (!(subdir = treeobj_deep_copy (subdirktmp))) {
                saved_errno = errno;
                goto done;
            }

            /* copy from entry already in cache, assume novalidate ok */
            if (treeobj_insert_entry_novalidate (dir, name, subdir) < 0) {
                saved_errno = errno;
                json_decref (subdir);
                goto done;
            }
            json_decref (subdir);
        } else if (treeobj_is_symlink (dir_entry)) {
            const char *ns = NULL;
            const char *target = NULL;
            char *nkey = NULL;

            if (treeobj_get_symlink (dir_entry, &ns, &target) < 0) {
                saved_errno = EINVAL;
                goto done;
            }
            assert (target);

            /* can't cross into a new namespace */
            if (ns && !streq (ns, kt->ktm->ns_name)) {
                saved_errno = EINVAL;
                goto done;
            }

            if (asprintf (&nkey, "%s.%s", target, next) < 0) {
                saved_errno = errno;
                goto done;
            }
            if (kvstxn_link_dirent (kt,
                                    rootdir,
                                    nkey,
                                    dirent,
                                    flags,
                                    missing_ref,
                                    append) < 0) {
                saved_errno = errno;
                free (nkey);
                goto done;
            }
            free (nkey);
            goto success;
        } else {
            if (json_is_null (dirent)) /* key deletion - it doesn't exist so return */
                goto success;
            if (!(subdir = treeobj_create_dir ())) {
                saved_errno = errno;
                goto done;
            }
            /* subdir just created above, no need to validate */
            if (treeobj_insert_entry_novalidate (dir, name, subdir) < 0) {
                saved_errno = errno;
                json_decref (subdir);
                goto done;
            }
            json_decref (subdir);
        }
        name = next;
        dir = subdir;
    }
    /* This is the final path component of the key.  Add/modify/delete
     * it in the directory.
     */
    if (!json_is_null (dirent)) {
        if (flags & FLUX_KVS_APPEND) {
            if (kvstxn_append (kt, dirent, dir, name, append) < 0) {
                saved_errno = errno;
                goto done;
            }
        }
        else {
            /* if not append, it's a normal insertion
             *
             * N.B. this is the primary insertion and what is being
             * inserted must be checked.  So we cannot use the
             * novalidate alternative function.
             */
            if (treeobj_insert_entry (dir, name, dirent) < 0) {
                saved_errno = errno;
                goto done;
            }
        }
    }
    else {
        if (treeobj_delete_entry (dir, name) < 0) {
            /* if ENOENT, it's ok since we're deleting */
            if (errno != ENOENT) {
                saved_errno = errno;
                goto done;
            }
        }
    }
 success:
    rc = 0;
 done:
    free (cpy);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static int add_missing_ref (kvstxn_t *kt, const char *ref)
{
    if (zlist_push (kt->missing_refs_list, (void *)ref) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* normalize key for setroot, and add it to keys dict, if unique */
static int normalize_and_add_unique (json_t *keys, const char *key)
{
    char *key_norm;
    int rc = -1;

    if ((key_norm = kvs_util_normalize_key (key, NULL)) == NULL)
        return -1;
    /* we don't need the value, just use a json null */
    if (json_object_set_new (keys, key_norm, json_null ()) < 0)
        goto error;
    rc = 0;
error:
    free (key_norm);
    return rc;
}

/* Create dict of keys (strings) from array of operations ({ "key":s ... })
 * The keys are for inclusion in the kvs.setroot event, so we can
 * notify watchers of keys that their key may have changed.  The value in
 * the dict is unneeded, so we set it to a json null.
 */
static json_t *keys_from_ops (json_t *ops)
{
    json_t *keys;
    size_t index;
    json_t *op;

    if (!(keys = json_object ()))
        return NULL;
    json_array_foreach (ops, index, op) {
        const char *key;
        if (json_unpack (op, "{s:s}", "key", &key) < 0)
            goto error;
        if (normalize_and_add_unique (keys, key) < 0)
            goto error;
    }
    return keys;
error:
    json_decref (keys);
    return NULL;
}

kvstxn_process_t kvstxn_process (kvstxn_t *kt,
                                 const char *root_ref,
                                 int root_seq)
{
    /* Incase user calls kvstxn_process() again */
    if (kt->errnum)
        return KVSTXN_PROCESS_ERROR;

    if (!kt->processing) {
        kt->errnum = EINVAL;
        return KVSTXN_PROCESS_ERROR;
    }

    /* Only exit the loop by returning from the function */
    while (1) {
        if (kt->state == KVSTXN_STATE_INIT) {
            /* Do some initial checks */
            if (kt->flags & FLUX_KVS_SYNC
                && !streq (kt->ktm->ns_name, KVS_PRIMARY_NAMESPACE)) {
                kt->errnum = EINVAL;
                return KVSTXN_PROCESS_ERROR;
            }
            kt->state = KVSTXN_STATE_LOAD_ROOT;
        }
        else if (kt->state == KVSTXN_STATE_LOAD_ROOT) {
            /* Make a copy of the root directory.
             */
            struct cache_entry *entry;

            /* Caller didn't call kvstxn_iter_missing_refs() */
            if (zlist_first (kt->missing_refs_list)) {
                kt->blocked = 1;
                return KVSTXN_PROCESS_LOAD_MISSING_REFS;
            }

            kt->state = KVSTXN_STATE_LOAD_ROOT;

            if (!(entry = cache_lookup (kt->ktm->cache, root_ref))
                || !cache_entry_get_valid (entry)) {

                if (add_missing_ref (kt, root_ref) < 0) {
                    kt->errnum = errno;
                    return KVSTXN_PROCESS_ERROR;
                }
                kt->blocked = 1;
                return KVSTXN_PROCESS_LOAD_MISSING_REFS;
            }

            if (!(kt->rootdir = cache_entry_get_treeobj (entry))) {
                kt->errnum = ENOTRECOVERABLE;
                return KVSTXN_PROCESS_ERROR;
            }

            /* Special optimization, continue to APPLY_OPS state only
             * if there are operations to process, otherwise we can
             * skip to the GENERATE_KEYS state.  Sometimes operations
             * can be zero length when using FLUX_KVS_SYNC or other
             * flags.
             */
            if (json_array_size (kt->ops)) {
                /* take reference because we're storing rootdir */
                cache_entry_incref (entry);
                kt->entry = entry;

                if (!(kt->rootcpy = treeobj_deep_copy (kt->rootdir))) {
                    kt->errnum = errno;
                    return KVSTXN_PROCESS_ERROR;
                }

                kt->state = KVSTXN_STATE_APPLY_OPS;
            }
            else {
                /* place current rootref into newroot, it won't change */
                strcpy (kt->newroot, root_ref);
                kt->state = KVSTXN_STATE_GENERATE_KEYS;
            }
        }
        else if (kt->state == KVSTXN_STATE_APPLY_OPS) {
            /* Apply each op (e.g. key = val) in sequence to the root
             * copy.  A side effect of walking key paths is to convert
             * dirref objects to dir objects in the copy.  This allows
             * the transaction to be self-contained in the rootcpy
             * until it is unrolled later on.
             *
             * Note that it is possible for multiple identical missing
             * references to be added to the missing_refs_list list.
             * Callers must deal with this appropriately.
             */
            json_t *op, *dirent;
            const char *missing_ref = NULL;
            int i, len = json_array_size (kt->ops);
            const char *key;
            int flags;
            bool append = false;

            /* Caller didn't call kvstxn_iter_missing_refs() */
            if (zlist_first (kt->missing_refs_list)) {
                kt->blocked = 1;
                return KVSTXN_PROCESS_LOAD_MISSING_REFS;
            }

            for (i = 0; i < len; i++) {
                missing_ref = NULL;
                op = json_array_get (kt->ops, i);
                assert (op != NULL);
                if (txn_decode_op (op, &key, &flags, &dirent) < 0) {
                    kt->errnum = errno;
                    break;
                }
                if (kvstxn_link_dirent (kt,
                                        kt->rootcpy,
                                        key,
                                        dirent,
                                        flags,
                                        &missing_ref,
                                        &append) < 0) {
                    kt->errnum = errno;
                    break;
                }
                if (missing_ref) {
                    if (add_missing_ref (kt, missing_ref) < 0) {
                        kt->errnum = errno;
                        break;
                    }
                }
            }

            if (kt->errnum != 0) {
                /* empty missing_refs_list to prevent mistakes later */
                zlist_purge (kt->missing_refs_list);
                return KVSTXN_PROCESS_ERROR;
            }

            if (zlist_first (kt->missing_refs_list)) {
                /* if we are stalling and an append has been done on the
                 * rootcpy, we cannot re-apply the operations on the
                 * replay of this transaction.  It would result in
                 * duplicate appends on a key.  We'll start over with a
                 * fresh rootcpy on the replay. */
                if (append) {
                    json_decref (kt->rootcpy);
                    if (!(kt->rootcpy = treeobj_deep_copy (kt->rootdir))) {
                        kt->errnum = errno;
                        return KVSTXN_PROCESS_ERROR;
                    }
                }
                kt->blocked = 1;
                return KVSTXN_PROCESS_LOAD_MISSING_REFS;
            }

            kt->state = KVSTXN_STATE_STORE;
        }
        else if (kt->state == KVSTXN_STATE_STORE) {
            /* Unroll the root copy.
             * When a dir is found, store an object and replace it
             * with a dirref.  Finally, store the unrolled root copy
             * as an object and keep its reference in kt->newroot.
             * Flushes to content cache are asynchronous but we don't
             * proceed until they are completed.
             */
            struct cache_entry *entry;
            int sret;

            if (kvstxn_unroll (kt, kt->rootcpy) < 0)
                kt->errnum = errno;
            else if ((sret = store_cache (kt,
                                          kt->rootcpy,
                                          false,
                                          kt->newroot,
                                          sizeof (kt->newroot),
                                          &entry)) < 0)
                kt->errnum = errno;
            else if (sret) {
                if (kvstxn_add_dirty_cache_entry (kt, entry) < 0)
                    kt->errnum = errno;
            }

            if (kt->errnum) {
                cleanup_dirty_cache_list (kt);
                return KVSTXN_PROCESS_ERROR;
            }

            /* cache now has ownership of rootcpy, we don't need our
             * rootcpy anymore.  But we may still need to stall user.
             */
            kt->state = KVSTXN_STATE_GENERATE_KEYS;
            json_decref (kt->rootcpy);
            kt->rootcpy = NULL;

            /* the cache entry for the new root has the chance to expire
             * in between the processing of dirty cache entries and the
             * user being done with the transaction.  Therefore a call
             * later to kvstxn_get_newroot_ref() could result in a
             * reference that no longer has its cache entry valid.  We'll
             * take an additional reference on the cache entry to ensure
             * it can't expire until the transaction is completed.
             */
            kt->newroot_entry = entry;
            cache_entry_incref (kt->newroot_entry);
        }
        else if (kt->state == KVSTXN_STATE_GENERATE_KEYS) {
            /* Caller didn't call kvstxn_iter_dirty_cache_entries() */
            if (zlist_first (kt->dirty_cache_entries_list)) {
                kt->blocked = 1;
                return KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES;
            }

            /* now generate keys for setroot */
            if (!(kt->keys = keys_from_ops (kt->ops))) {
                kt->errnum = ENOMEM;
                return KVSTXN_PROCESS_ERROR;
            }

            if (kt->flags & FLUX_KVS_SYNC)
                kt->state = KVSTXN_STATE_SYNC_CONTENT_FLUSH;
            else
                kt->state = KVSTXN_STATE_FINISHED;
        }
        else if (kt->state == KVSTXN_STATE_SYNC_CONTENT_FLUSH) {
            if (!(kt->f_sync_content_flush)) {
                kt->f_sync_content_flush = flux_rpc (kt->ktm->h,
                                                     "content.flush",
                                                     NULL,
                                                     0,
                                                     0);
                if (!kt->f_sync_content_flush) {
                    kt->errnum = errno;
                    return KVSTXN_PROCESS_ERROR;
                }
                kt->blocked = 1;
                return KVSTXN_PROCESS_SYNC_CONTENT_FLUSH;
            }

            /* user did not wait for future to complex */
            if (!flux_future_is_ready (kt->f_sync_content_flush)) {
                kt->blocked = 1;
                return KVSTXN_PROCESS_SYNC_CONTENT_FLUSH;
            }

            if (flux_rpc_get (kt->f_sync_content_flush, NULL) < 0) {
                kt->errnum = errno;
                return KVSTXN_PROCESS_ERROR;
            }

            kt->state = KVSTXN_STATE_SYNC_CHECKPOINT;
        }
        else if (kt->state == KVSTXN_STATE_SYNC_CHECKPOINT) {

            if (!(kt->f_sync_checkpoint)) {
                int newseq = root_seq;

                /* if we're publishing, seq will be the seq after
                 * the current one.
                 */
                if (!(kt->internal_flags & KVSTXN_INTERNAL_FLAG_NO_PUBLISH))
                    newseq++;

                kt->f_sync_checkpoint = kvs_checkpoint_commit (kt->ktm->h,
                                                               NULL,
                                                               kt->newroot,
                                                               newseq,
                                                               0,
                                                               0);
                if (!kt->f_sync_checkpoint) {
                    kt->errnum = errno;
                    return KVSTXN_PROCESS_ERROR;
                }
                kt->blocked = 1;
                return KVSTXN_PROCESS_SYNC_CHECKPOINT;
            }

            /* user did not wait for future to complex */
            if (!flux_future_is_ready (kt->f_sync_checkpoint)) {
                kt->blocked = 1;
                return KVSTXN_PROCESS_SYNC_CHECKPOINT;
            }

            if (flux_rpc_get (kt->f_sync_checkpoint, NULL) < 0) {
                kt->errnum = errno;
                return KVSTXN_PROCESS_ERROR;
            }

            /* N.B. After confirmation that a checkpoint is
             * successful, immediately goto the FINISHED state so the
             * kvs can transition to the new root reference.  We
             * cannot do anything else that can lead to an error.  If
             * an error would occur, we would have checkpointed a root
             * reference that has never been the actual root reference
             * of the KVS.
             */
            kt->state = KVSTXN_STATE_FINISHED;
        }
        else if (kt->state == KVSTXN_STATE_FINISHED) {
            return KVSTXN_PROCESS_FINISHED;
        }
        else {
            flux_log (kt->ktm->h, LOG_ERR, "invalid kvstxn state: %d", kt->state);
            kt->errnum = ENOTRECOVERABLE;
            return KVSTXN_PROCESS_ERROR;
        }
    }

    /* UNREACHABLE */
    return KVSTXN_PROCESS_ERROR;
}

int kvstxn_iter_missing_refs (kvstxn_t *kt, kvstxn_ref_f cb, void *data)
{
    char *ref;

    if (kt->state != KVSTXN_STATE_LOAD_ROOT
        && kt->state != KVSTXN_STATE_APPLY_OPS) {
        errno = EINVAL;
        return -1;
    }

    while ((ref = zlist_pop (kt->missing_refs_list))) {
        if (cb (kt, ref, data) < 0) {
            int saved_errno = errno;
            free (ref);
            zlist_purge (kt->missing_refs_list);
            errno = saved_errno;
            return -1;
        }
        free (ref);
    }
    return 0;
}

int kvstxn_iter_dirty_cache_entries (kvstxn_t *kt,
                                     kvstxn_cache_entry_f cb,
                                     void *data)
{
    struct cache_entry *entry;

    if (kt->state != KVSTXN_STATE_GENERATE_KEYS) {
        errno = EINVAL;
        return -1;
    }

    while ((entry = zlist_pop (kt->dirty_cache_entries_list))) {
        cache_entry_decref (entry);
        if (cb (kt, entry, data) < 0) {
            int saved_errno = errno;
            cleanup_dirty_cache_list (kt);
            errno = saved_errno;
            return -1;
        }
    }
    return 0;
}

flux_future_t *kvstxn_sync_content_flush (kvstxn_t *kt)
{
    if (kt->state != KVSTXN_STATE_SYNC_CONTENT_FLUSH) {
        errno = EINVAL;
        return NULL;
    }

    return kt->f_sync_content_flush;
}

flux_future_t *kvstxn_sync_checkpoint (kvstxn_t *kt)
{
    if (kt->state != KVSTXN_STATE_SYNC_CHECKPOINT) {
        errno = EINVAL;
        return NULL;
    }

    return kt->f_sync_checkpoint;
}

kvstxn_mgr_t *kvstxn_mgr_create (struct cache *cache,
                                 const char *ns,
                                 const char *hash_name,
                                 flux_t *h,
                                 void *aux)
{
    kvstxn_mgr_t *ktm = NULL;
    int saved_errno;

    if (!cache || !ns || !hash_name) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(ktm = calloc (1, sizeof (*ktm)))) {
        saved_errno = errno;
        goto error;
    }
    ktm->cache = cache;
    ktm->ns_name = ns;
    ktm->hash_name = hash_name;
    if (!(ktm->ready = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    ktm->h = h;
    ktm->aux = aux;
    return ktm;

 error:
    kvstxn_mgr_destroy (ktm);
    errno = saved_errno;
    return NULL;
}

void kvstxn_mgr_destroy (kvstxn_mgr_t *ktm)
{
    if (ktm) {
        if (ktm->ready)
            zlist_destroy (&ktm->ready);
        free (ktm);
    }
}

int kvstxn_mgr_add_transaction (kvstxn_mgr_t *ktm,
                                const char *name,
                                json_t *ops,
                                int flags,
                                int internal_flags)
{
    kvstxn_t *kt;
    int valid_internal_flags = KVSTXN_INTERNAL_FLAG_NO_PUBLISH;

    if (!name
        || !ops
        || (internal_flags & ~valid_internal_flags)) {
        errno = EINVAL;
        return -1;
    }

    if (!(kt = kvstxn_create (ktm,
                              name,
                              ops,
                              flags,
                              internal_flags)))
        return -1;

    if (zlist_append (ktm->ready, kt) < 0) {
        kvstxn_destroy (kt);
        errno = ENOMEM;
        return -1;
    }
    zlist_freefn (ktm->ready, kt, (zlist_free_fn *)kvstxn_destroy, true);

    return 0;
}

bool kvstxn_mgr_transaction_ready (kvstxn_mgr_t *ktm)
{
    kvstxn_t *kt;

    if ((kt = zlist_first (ktm->ready)) && !kt->blocked)
        return true;
    return false;
}

kvstxn_t *kvstxn_mgr_get_ready_transaction (kvstxn_mgr_t *ktm)
{
    if (kvstxn_mgr_transaction_ready (ktm)) {
        kvstxn_t *kt = zlist_first (ktm->ready);
        kt->processing = true;
        return kt;
    }
    return NULL;
}

void kvstxn_mgr_remove_transaction (kvstxn_mgr_t *ktm, kvstxn_t *kt,
                                    bool fallback)
{
    if (kt->processing) {
        bool kvstxn_is_merged = false;

        if (kt->merged)
            kvstxn_is_merged = true;

        zlist_remove (ktm->ready, kt);

        if (kvstxn_is_merged) {
            kvstxn_t *kt_tmp = zlist_first (ktm->ready);
            while (kt_tmp && kt_tmp->merge_component) {
                if (fallback) {
                    kt_tmp->merge_component = false;
                    kt_tmp->flags |= FLUX_KVS_NO_MERGE;
                }
                else
                    zlist_remove (ktm->ready, kt_tmp);

                kt_tmp = zlist_next (ktm->ready);
            }
        }
    }
}

int kvstxn_mgr_get_noop_stores (kvstxn_mgr_t *ktm)
{
    return ktm->noop_stores;
}

void kvstxn_mgr_clear_noop_stores (kvstxn_mgr_t *ktm)
{
    ktm->noop_stores = 0;
}

int kvstxn_mgr_ready_transaction_count (kvstxn_mgr_t *ktm)
{
    return zlist_size (ktm->ready);
}

/* N.B. FLUX_KVS_SYNC implies FLUX_KVS_NO_MERGE, as we checkpoint
 * after the specific commit completes.  So FLUX_KVS_SYNC is
 * treated identically to FLUX_KVS_NO_MERGE in merge logic.
 */
static bool kvstxn_no_merge (kvstxn_t *kt)
{
    if ((kt->flags & FLUX_KVS_NO_MERGE)
        || (kt->flags & FLUX_KVS_SYNC))
        return true;
    return false;
}

static int kvstxn_merge (kvstxn_t *dest, kvstxn_t *src)
{
    int i, len;

    if (kvstxn_no_merge (src)
        || dest->flags != src->flags)
        return 0;

    if ((len = json_array_size (src->names))) {
        for (i = 0; i < len; i++) {
            json_t *name;
            if ((name = json_array_get (src->names, i))) {
                if (json_array_append (dest->names, name) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
    }
    if ((len = json_array_size (src->ops))) {
        for (i = 0; i < len; i++) {
            json_t *op;
            if ((op = json_array_get (src->ops, i))) {
                if (json_array_append (dest->ops, op) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
    }

    return 1;
}

/* Merge ready transactions that are mergeable, where merging consists
 * creating a new kvstxn_t, and merging the other transactions in the
 * ready queue and appending their ops/names to the new transaction.
 * After merging, push the new kvstxn_t onto the head of the ready
 * queue.  Merging can occur if the top transaction hasn't started, or
 * is still building the rootcpy, e.g. stalled walking the namespace.
 *
 * Break when an unmergeable transaction is discovered.  We do not
 * wish to merge non-adjacent transactions, as it can create
 * undesirable out of order scenarios.  e.g.
 *
 * transaction #1 is mergeable:     set A=1
 * transaction #2 is non-mergeable: set A=2
 * transaction #3 is mergeable:     set A=3
 *
 * If we were to merge transaction #1 and transaction #3, A=2 would be
 * set after A=3.
 */

int kvstxn_mgr_merge_ready_transactions (kvstxn_mgr_t *ktm)
{
    kvstxn_t *first, *second, *new;
    kvstxn_t *nextkt;
    int count = 0;

    /* transaction must still be in state where merged in ops can be
     * applied. */
    first = zlist_first (ktm->ready);
    if (!first
        || first->errnum != 0
        || first->aux_errnum != 0
        || first->state > KVSTXN_STATE_APPLY_OPS
        || kvstxn_no_merge (first)
        || first->merged)
        return 0;

    second = zlist_next (ktm->ready);
    if (!second
        || kvstxn_no_merge (second)
        || (first->flags != second->flags)
        || (first->internal_flags != second->internal_flags))
        return 0;

    if (!(new = kvstxn_create (ktm,
                               NULL,
                               NULL,
                               first->flags,
                               first->internal_flags)))
        return -1;
    new->merged = true;

    nextkt = zlist_first (ktm->ready);
    do {
        int ret;

        if ((ret = kvstxn_merge (new, nextkt)) < 0) {
            kvstxn_destroy (new);
            return -1;
        }

        if (!ret)
            break;

        count++;
    } while ((nextkt = zlist_next (ktm->ready)));

    /* if count is zero, checks at beginning of function are invalid */
    assert (count);

    if (zlist_push (ktm->ready, new) < 0) {
        kvstxn_destroy (new);
        return -1;
    }
    zlist_freefn (ktm->ready, new, (zlist_free_fn *)kvstxn_destroy, false);

    /* first is the new merged kvstxn_t, so we want to start our loop with
     * the second kvstxn_t
     */
    nextkt = zlist_first (ktm->ready);
    nextkt = zlist_next (ktm->ready);
    do {
        /* reset processing flag if user previously got the kvstxn_t
         */
        nextkt->processing = false;
        nextkt->merge_component = true;
    } while (--count && (nextkt = zlist_next (ktm->ready)));

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
