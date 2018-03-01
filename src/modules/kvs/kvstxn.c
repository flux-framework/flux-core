/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/base64.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_txn_private.h"

#include "kvstxn.h"
#include "kvs_util.h"

struct kvstxn_mgr {
    struct cache *cache;
    const char *namespace;
    const char *hash_name;
    int noop_stores;            /* for kvs.stats.get, etc.*/
    zlist_t *ready;
    flux_t *h;
    void *aux;
};

struct kvstxn {
    int errnum;
    int aux_errnum;
    int blocked:1;
    json_t *ops;
    json_t *names;
    int flags;
    json_t *rootcpy;   /* working copy of root dir */
    blobref_t newroot;
    zlist_t *missing_refs_list;
    zlist_t *dirty_cache_entries_list;
    kvstxn_mgr_t *ktm;
    enum {
        KVSTXN_STATE_INIT = 1,
        KVSTXN_STATE_LOAD_ROOT = 2,
        KVSTXN_STATE_APPLY_OPS = 3,
        KVSTXN_STATE_STORE = 4,
        KVSTXN_STATE_PRE_FINISHED = 5,
        KVSTXN_STATE_FINISHED = 6,
    } state;
};

static void kvstxn_destroy (kvstxn_t *kt)
{
    if (kt) {
        json_decref (kt->ops);
        json_decref (kt->names);
        json_decref (kt->rootcpy);
        if (kt->missing_refs_list)
            zlist_destroy (&kt->missing_refs_list);
        if (kt->dirty_cache_entries_list)
            zlist_destroy (&kt->dirty_cache_entries_list);
        free (kt);
    }
}

static kvstxn_t *kvstxn_create (treq_t *tr, kvstxn_mgr_t *ktm)
{
    kvstxn_t *kt;
    const char *name;
    int saved_errno;

    if (!(kt = calloc (1, sizeof (*kt)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(kt->ops = json_copy (treq_get_ops (tr)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(kt->names = json_array ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if ((name = treq_get_name (tr))) {
        json_t *s;
        if (!(s = json_string (name))) {
            saved_errno = ENOMEM;
            goto error;
        }
        if (json_array_append_new (kt->names, s) < 0) {
            json_decref (s);
            saved_errno = ENOMEM;
            goto error;
        }
    }
    kt->flags = treq_get_flags (tr);
    if (!(kt->missing_refs_list = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(kt->dirty_cache_entries_list = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    kt->ktm = ktm;
    kt->state = KVSTXN_STATE_INIT;
    return kt;
 error:
    kvstxn_destroy (kt);
    errno = saved_errno;
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

const char *kvstxn_get_namespace (kvstxn_t *kt)
{
    return kt->ktm->namespace;
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
        || kt->state == KVSTXN_STATE_PRE_FINISHED) {
        blobref_t ref;
        const void *data;
        int len;
        int ret;

        assert (cache_entry_get_valid (entry) == true);
        assert (cache_entry_get_dirty (entry) == true);
        ret = cache_entry_clear_dirty (entry);
        assert (ret == 0);
        assert (cache_entry_get_dirty (entry) == false);

        ret = cache_entry_get_raw (entry, &data, &len);
        assert (ret == 0);

        blobref_hash (kt->ktm->hash_name, data, len, ref);

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

/* Store object 'o' under key 'ref' in local cache.
 * Object reference is still owned by the caller.
 * 'is_raw' indicates this data is a json string w/ base64 value and
 * should be flushed to the content store as raw data after it is
 * decoded.  Otherwise, the json object should be a treeobj.
 * Returns -1 on error, 0 on success entry already there, 1 on success
 * entry needs to be flushed to content store
 */
static int store_cache (kvstxn_t *kt, int current_epoch, json_t *o,
                        bool is_raw, blobref_t ref, struct cache_entry **entryp)
{
    struct cache_entry *entry;
    int saved_errno, rc;
    const char *xdata;
    char *data = NULL;
    int xlen, len;

    if (is_raw) {
        xdata = json_string_value (o);
        xlen = strlen (xdata);
        len = base64_decode_length (xlen);
        if (!(data = malloc (len))) {
            flux_log_error (kt->ktm->h, "malloc");
            goto error;
        }
        if (base64_decode_block (data, &len, xdata, xlen) < 0) {
            flux_log_error (kt->ktm->h, "base64_decode_block");
            errno = EPROTO;
            goto error;
        }
        /* len from base64_decode_length() always > 0 b/c of NUL byte,
         * but len after base64_decode_block() can be zero.  Adjust if
         * necessary. */
        if (!len) {
            free (data);
            data = NULL;
        }
    }
    else {
        if (treeobj_validate (o) < 0 || !(data = treeobj_encode (o))) {
            flux_log_error (kt->ktm->h, "%s: treeobj_encode", __FUNCTION__);
            goto error;
        }
        len = strlen (data);
    }
    if (blobref_hash (kt->ktm->hash_name, data, len, ref) < 0) {
        flux_log_error (kt->ktm->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (kt->ktm->cache, ref, current_epoch))) {
        if (!(entry = cache_entry_create ())) {
            flux_log_error (kt->ktm->h, "%s: cache_entry_create", __FUNCTION__);
            goto error;
        }
        cache_insert (kt->ktm->cache, ref, entry);
    }
    if (cache_entry_get_valid (entry)) {
        kt->ktm->noop_stores++;
        rc = 0;
    }
    else {
        if (cache_entry_set_raw (entry, data, len) < 0) {
            int ret;
            ret = cache_remove_entry (kt->ktm->cache, ref);
            assert (ret == 1);
            goto error;
        }
        if (cache_entry_set_dirty (entry, true) < 0) {
            flux_log_error (kt->ktm->h, "%s: cache_entry_set_dirty",__FUNCTION__);
            int ret;
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
static int kvstxn_unroll (kvstxn_t *kt, int current_epoch, json_t *dir)
{
    json_t *dir_entry;
    json_t *dir_data;
    json_t *ktmp;
    blobref_t ref;
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
            if (kvstxn_unroll (kt, current_epoch, dir_entry) < 0) /* depth first */
                return -1;
            if ((ret = store_cache (kt, current_epoch, dir_entry,
                                    false, ref, &entry)) < 0)
                return -1;
            if (ret) {
                if (zlist_push (kt->dirty_cache_entries_list, entry) < 0) {
                    kvstxn_cleanup_dirty_cache_entry (kt, entry);
                    errno = ENOMEM;
                    return -1;
                }
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
            const char *str;

            if (!(val_data = treeobj_get_data (dir_entry)))
                return -1;
            /* jansson >= 2.7 could use json_string_length() instead */
            str = json_string_value (val_data);
            assert (str);
            if (strlen (str) > BLOBREF_MAX_STRING_SIZE) {
                if ((ret = store_cache (kt, current_epoch, val_data,
                                        true, ref, &entry)) < 0)
                    return -1;
                if (ret) {
                    if (zlist_push (kt->dirty_cache_entries_list, entry) < 0) {
                        kvstxn_cleanup_dirty_cache_entry (kt, entry);
                        errno = ENOMEM;
                        return -1;
                    }
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

static int kvstxn_val_data_to_cache (kvstxn_t *kt, int current_epoch,
                                     json_t *val, blobref_t ref)
{
    struct cache_entry *entry;
    json_t *val_data;
    int ret;

    if (!(val_data = treeobj_get_data (val)))
        return -1;

    if ((ret = store_cache (kt, current_epoch, val_data,
                            true, ref, &entry)) < 0)
        return -1;

    if (ret) {
        if (zlist_push (kt->dirty_cache_entries_list, entry) < 0) {
            kvstxn_cleanup_dirty_cache_entry (kt, entry);
            errno = ENOMEM;
            return -1;
        }
    }

    return 0;
}

static int kvstxn_append (kvstxn_t *kt, int current_epoch, json_t *dirent,
                          json_t *dir, const char *final_name)
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
    }
    else if (treeobj_is_valref (entry)) {
        blobref_t ref;
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

        if (kvstxn_val_data_to_cache (kt, current_epoch, dirent, ref) < 0)
            return -1;

        if (!(cpy = treeobj_deep_copy (entry)))
            return -1;

        if (treeobj_append_blobref (cpy, ref) < 0) {
            json_decref (cpy);
            return -1;
        }

        if (treeobj_insert_entry (dir, final_name, cpy) < 0) {
            json_decref (cpy);
            return -1;
        }
    }
    else if (treeobj_is_val (entry)) {
        json_t *ktmp;
        blobref_t ref1, ref2;

        /* treeobj entry is val, so we need to convert the treeobj
         * into a valref first.  Then the procedure is basically the
         * same as the treeobj valref case above.
         */

        if (kvstxn_val_data_to_cache (kt, current_epoch, entry, ref1) < 0)
            return -1;

        if (kvstxn_val_data_to_cache (kt, current_epoch, dirent, ref2) < 0)
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
        char *s = json_dumps (entry, 0);
        flux_log (kt->ktm->h, LOG_ERR, "%s: corrupt treeobj: %s",
                  __FUNCTION__, s);
        free (s);
        errno = ENOTRECOVERABLE;
        return -1;
    }
    return 0;
}

/* link (key, dirent) into directory 'dir'.
 */
static int kvstxn_link_dirent (kvstxn_t *kt, int current_epoch,
                               json_t *rootdir, const char *key,
                               json_t *dirent, int flags,
                               const char **missing_ref)
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
    if (strcmp (name, ".") == 0) {
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
            if (treeobj_insert_entry (dir, name, subdir) < 0) {
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

            if (!(entry = cache_lookup (kt->ktm->cache, ref, current_epoch))
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

            if (treeobj_insert_entry (dir, name, subdir) < 0) {
                saved_errno = errno;
                json_decref (subdir);
                goto done;
            }
            json_decref (subdir);
        } else if (treeobj_is_symlink (dir_entry)) {
            json_t *symlink = treeobj_get_data (dir_entry);
            const char *symlinkstr;
            char *nkey = NULL;

            if (!symlink) {
                saved_errno = errno;
                goto done;
            }

            assert (json_is_string (symlink));

            symlinkstr = json_string_value (symlink);
            if (asprintf (&nkey, "%s.%s", symlinkstr, next) < 0) {
                saved_errno = ENOMEM;
                goto done;
            }
            if (kvstxn_link_dirent (kt,
                                    current_epoch,
                                    rootdir,
                                    nkey,
                                    dirent,
                                    flags,
                                    missing_ref) < 0) {
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
            if (treeobj_insert_entry (dir, name, subdir) < 0) {
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
            if (kvstxn_append (kt, current_epoch, dirent, dir, name) < 0) {
                saved_errno = errno;
                goto done;
            }
        }
        else {
            /* if not append, it's a normal insertion */
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

kvstxn_process_t kvstxn_process (kvstxn_t *kt,
                                 int current_epoch,
                                 const blobref_t rootdir_ref)
{
    /* Incase user calls kvstxn_process() again */
    if (kt->errnum)
        return KVSTXN_PROCESS_ERROR;

    switch (kt->state) {
    case KVSTXN_STATE_INIT:
    case KVSTXN_STATE_LOAD_ROOT:
    {
        /* Make a copy of the root directory.
         */
        struct cache_entry *entry;
        const json_t *rootdir;

        /* Caller didn't call kvstxn_iter_missing_refs() */
        if (zlist_first (kt->missing_refs_list))
            goto stall_load;

        kt->state = KVSTXN_STATE_LOAD_ROOT;

        if (!(entry = cache_lookup (kt->ktm->cache,
                                    rootdir_ref,
                                    current_epoch))
            || !cache_entry_get_valid (entry)) {

            if (zlist_push (kt->missing_refs_list,
                            (void *)rootdir_ref) < 0) {
                kt->errnum = ENOMEM;
                return KVSTXN_PROCESS_ERROR;
            }
            goto stall_load;
        }

        if (!(rootdir = cache_entry_get_treeobj (entry))) {
            kt->errnum = ENOTRECOVERABLE;
            return KVSTXN_PROCESS_ERROR;
        }

        if (!(kt->rootcpy = treeobj_deep_copy (rootdir))) {
            kt->errnum = errno;
            return KVSTXN_PROCESS_ERROR;
        }

        kt->state = KVSTXN_STATE_APPLY_OPS;
        /* fallthrough */
    }
    case KVSTXN_STATE_APPLY_OPS:
    {
        /* Apply each op (e.g. key = val) in sequence to the root
         * copy.  A side effect of walking key paths is to convert
         * dirref objects to dir objects in the copy.  This allows
         * the transaction to be self-contained in the rootcpy
         * until it is unrolled later on.
         */
        json_t *op, *dirent;
        const char *missing_ref = NULL;
        int i, len = json_array_size (kt->ops);
        const char *key;
        int flags;

        /* Caller didn't call kvstxn_iter_missing_refs() */
        if (zlist_first (kt->missing_refs_list))
            goto stall_load;

        for (i = 0; i < len; i++) {
            missing_ref = NULL;
            op = json_array_get (kt->ops, i);
            assert (op != NULL);
            if (txn_decode_op (op, &key, &flags, &dirent) < 0) {
                kt->errnum = errno;
                break;
            }
            if (kvstxn_link_dirent (kt,
                                    current_epoch,
                                    kt->rootcpy,
                                    key,
                                    dirent,
                                    flags,
                                    &missing_ref) < 0) {
                kt->errnum = errno;
                break;
            }
            if (missing_ref) {
                if (zlist_push (kt->missing_refs_list,
                                (void *)missing_ref) < 0) {
                    kt->errnum = ENOMEM;
                    break;
                }
            }
        }

        if (kt->errnum != 0) {
            /* empty missing_refs_list to prevent mistakes later */
            while ((missing_ref = zlist_pop (kt->missing_refs_list)));
            return KVSTXN_PROCESS_ERROR;
        }

        if (zlist_first (kt->missing_refs_list))
            goto stall_load;

        kt->state = KVSTXN_STATE_STORE;
        /* fallthrough */
    }
    case KVSTXN_STATE_STORE:
    {
        /* Unroll the root copy.
         * When a dir is found, store an object and replace it
         * with a dirref.  Finally, store the unrolled root copy
         * as an object and keep its reference in kt->newroot.
         * Flushes to content cache are asynchronous but we don't
         * proceed until they are completed.
         */
        struct cache_entry *entry;
        int sret;

        if (kvstxn_unroll (kt, current_epoch, kt->rootcpy) < 0)
            kt->errnum = errno;
        else if ((sret = store_cache (kt,
                                      current_epoch,
                                      kt->rootcpy,
                                      false,
                                      kt->newroot,
                                      &entry)) < 0)
            kt->errnum = errno;
        else if (sret
                 && zlist_push (kt->dirty_cache_entries_list, entry) < 0) {
            kvstxn_cleanup_dirty_cache_entry (kt, entry);
            kt->errnum = ENOMEM;
        }

        if (kt->errnum) {
            cleanup_dirty_cache_list (kt);
            return KVSTXN_PROCESS_ERROR;
        }

        /* cache now has ownership of rootcpy, we don't need our
         * rootcpy anymore.  But we may still need to stall user.
         */
        kt->state = KVSTXN_STATE_PRE_FINISHED;
        json_decref (kt->rootcpy);
        kt->rootcpy = NULL;

        /* fallthrough */
    }
    case KVSTXN_STATE_PRE_FINISHED:
        /* If we did not fall through to here, caller didn't call
         * kvstxn_iter_dirty_cache_entries()
         */
        if (zlist_first (kt->dirty_cache_entries_list))
            goto stall_store;

        kt->state = KVSTXN_STATE_FINISHED;
        /* fallthrough */
    case KVSTXN_STATE_FINISHED:
        break;
    default:
        flux_log (kt->ktm->h, LOG_ERR, "invalid kvstxn state: %d", kt->state);
        kt->errnum = ENOTRECOVERABLE;
        return KVSTXN_PROCESS_ERROR;
    }

    return KVSTXN_PROCESS_FINISHED;

 stall_load:
    kt->blocked = 1;
    return KVSTXN_PROCESS_LOAD_MISSING_REFS;

 stall_store:
    kt->blocked = 1;
    return KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES;
}

int kvstxn_iter_missing_refs (kvstxn_t *kt, kvstxn_ref_f cb, void *data)
{
    const char *ref;
    int saved_errno, rc = 0;

    if (kt->state != KVSTXN_STATE_LOAD_ROOT
        && kt->state != KVSTXN_STATE_APPLY_OPS) {
        errno = EINVAL;
        return -1;
    }

    while ((ref = zlist_pop (kt->missing_refs_list))) {
        if (cb (kt, ref, data) < 0) {
            saved_errno = errno;
            rc = -1;
            break;
        }
    }

    if (rc < 0) {
        while ((ref = zlist_pop (kt->missing_refs_list)));
        errno = saved_errno;
    }

    return rc;
}

int kvstxn_iter_dirty_cache_entries (kvstxn_t *kt,
                                     kvstxn_cache_entry_f cb,
                                     void *data)
{
    struct cache_entry *entry;
    int saved_errno, rc = 0;

    if (kt->state != KVSTXN_STATE_PRE_FINISHED) {
        errno = EINVAL;
        return -1;
    }

    while ((entry = zlist_pop (kt->dirty_cache_entries_list))) {
        if (cb (kt, entry, data) < 0) {
            saved_errno = errno;
            rc = -1;
            break;
        }
    }

    if (rc < 0) {
        cleanup_dirty_cache_list (kt);
        errno = saved_errno;
    }

    return rc;
}

kvstxn_mgr_t *kvstxn_mgr_create (struct cache *cache,
                                 const char *namespace,
                                 const char *hash_name,
                                 flux_t *h,
                                 void *aux)
{
    kvstxn_mgr_t *ktm = NULL;
    int saved_errno;

    if (!cache || !namespace || !hash_name) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(ktm = calloc (1, sizeof (*ktm)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    ktm->cache = cache;
    ktm->namespace = namespace;
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

int kvstxn_mgr_process_fence_request (kvstxn_mgr_t *ktm, treq_t *tr)
{
    if (treq_count_reached (tr)) {
        kvstxn_t *kt;

        /* treq is already processed */
        if (treq_get_processed (tr))
            return 0;

        if (!(kt = kvstxn_create (tr, ktm)))
            return -1;

        if (zlist_append (ktm->ready, kt) < 0) {
            kvstxn_destroy (kt);
            errno = ENOMEM;
            return -1;
        }
        /* we use this flag to indicate if a treq is "ready" */
        treq_set_processed (tr, true);
        zlist_freefn (ktm->ready, kt, (zlist_free_fn *)kvstxn_destroy, true);
    }

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
    if (kvstxn_mgr_transaction_ready (ktm))
        return zlist_first (ktm->ready);
    return NULL;
}

void kvstxn_mgr_remove_transaction (kvstxn_mgr_t *ktm, kvstxn_t *kt)
{
    zlist_remove (ktm->ready, kt);
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

static int kvstxn_merge (kvstxn_t *dest, kvstxn_t *src)
{
    json_t *names = NULL;
    json_t *ops = NULL;
    int i, len, saved_errno;

    if ((dest->flags & FLUX_KVS_NO_MERGE) || (src->flags & FLUX_KVS_NO_MERGE))
        return 0;

    if ((len = json_array_size (src->names))) {
        if (!(names = json_copy (dest->names))) {
            saved_errno = ENOMEM;
            goto error;
        }
        for (i = 0; i < len; i++) {
            json_t *name;
            if ((name = json_array_get (src->names, i))) {
                if (json_array_append (names, name) < 0) {
                    saved_errno = ENOMEM;
                    goto error;
                }
            }
        }
    }
    if ((len = json_array_size (src->ops))) {
        if (!(ops = json_copy (dest->ops))) {
            saved_errno = ENOMEM;
            goto error;
        }
        for (i = 0; i < len; i++) {
            json_t *op;
            if ((op = json_array_get (src->ops, i))) {
                if (json_array_append (ops, op) < 0) {
                    saved_errno = ENOMEM;
                    goto error;
                }
            }
        }
    }

    if (names) {
        json_decref (dest->names);
        dest->names = names;
    }
    if (ops) {
        json_decref (dest->ops);
        dest->ops = ops;
    }
    return 1;

 error:
    json_decref (names);
    json_decref (ops);
    errno = saved_errno;
    return -1;
}

/* Merge ready transactions that are mergeable, where merging consists
 * of popping the "donor" transaction off the ready list, and
 * appending its ops to the top transaction.  The top transaction can
 * be appended to if it hasn't started, or is still building the
 * rootcpy, e.g. stalled walking the namespace.
 *
 * Break when an unmergeable transaction is discovered.  We do not
 * wish to merge non-adjacent transactions, as it can create
 * undesireable out of order scenarios.  e.g.
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
    kvstxn_t *kt = zlist_first (ktm->ready);

    /* transaction must still be in state where merged in ops can be
     * applied */
    if (kt
        && kt->errnum == 0
        && kt->state <= KVSTXN_STATE_APPLY_OPS
        && !(kt->flags & FLUX_KVS_NO_MERGE)) {
        kvstxn_t *nkt;
        while ((nkt = zlist_next (ktm->ready))) {
            int ret;

            if ((ret = kvstxn_merge (kt, nkt)) < 0)
                return -1;

            /* if return == 0, we've merged as many as we currently
             * can */
            if (!ret)
                break;

            /* Merged kvstxn, remove off ready list */
            zlist_remove (ktm->ready, nkt);
        }
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
