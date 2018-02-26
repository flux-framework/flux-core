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

#include "commit.h"
#include "kvs_util.h"

struct commit_mgr {
    struct cache *cache;
    const char *namespace;
    const char *hash_name;
    int noop_stores;            /* for kvs.stats.get, etc.*/
    zhash_t *fences;
    bool iterating_fences;
    zlist_t *removelist;
    zlist_t *ready;
    flux_t *h;
    void *aux;
};

struct commit {
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
    commit_mgr_t *cm;
    enum {
        COMMIT_STATE_INIT = 1,
        COMMIT_STATE_LOAD_ROOT = 2,
        COMMIT_STATE_APPLY_OPS = 3,
        COMMIT_STATE_STORE = 4,
        COMMIT_STATE_PRE_FINISHED = 5,
        COMMIT_STATE_FINISHED = 6,
    } state;
};

static void commit_destroy (commit_t *c)
{
    if (c) {
        json_decref (c->ops);
        json_decref (c->names);
        json_decref (c->rootcpy);
        if (c->missing_refs_list)
            zlist_destroy (&c->missing_refs_list);
        if (c->dirty_cache_entries_list)
            zlist_destroy (&c->dirty_cache_entries_list);
        free (c);
    }
}

static commit_t *commit_create (fence_t *f, commit_mgr_t *cm)
{
    commit_t *c;
    const char *name;
    int saved_errno;

    if (!(c = calloc (1, sizeof (*c)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(c->ops = json_copy (fence_get_json_ops (f)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(c->names = json_array ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if ((name = fence_get_name (f))) {
        json_t *s;
        if (!(s = json_string (name))) {
            saved_errno = ENOMEM;
            goto error;
        }
        if (json_array_append_new (c->names, s) < 0) {
            json_decref (s);
            saved_errno = ENOMEM;
            goto error;
        }
    }
    c->flags = fence_get_flags (f);
    if (!(c->missing_refs_list = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(c->dirty_cache_entries_list = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    c->cm = cm;
    c->state = COMMIT_STATE_INIT;
    return c;
error:
    commit_destroy (c);
    errno = saved_errno;
    return NULL;
}

int commit_get_errnum (commit_t *c)
{
    return c->errnum;
}

int commit_get_aux_errnum (commit_t *c)
{
    return c->aux_errnum;
}

int commit_set_aux_errnum (commit_t *c, int errnum)
{
    c->aux_errnum = errnum;
    return c->aux_errnum;
}

json_t *commit_get_ops (commit_t *c)
{
    return c->ops;
}

json_t *commit_get_names (commit_t *c)
{
    return c->names;
}

int commit_get_flags (commit_t *c)
{
    return c->flags;
}

const char *commit_get_namespace (commit_t *c)
{
    return c->cm->namespace;
}

void *commit_get_aux (commit_t *c)
{
    return c->cm->aux;
}

const char *commit_get_newroot_ref (commit_t *c)
{
    if (c->state == COMMIT_STATE_FINISHED)
        return c->newroot;
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
void commit_cleanup_dirty_cache_entry (commit_t *c, struct cache_entry *entry)
{
    if (c->state == COMMIT_STATE_STORE
        || c->state == COMMIT_STATE_PRE_FINISHED) {
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

        blobref_hash (c->cm->hash_name, data, len, ref);

        ret = cache_remove_entry (c->cm->cache, ref);
        assert (ret == 1);
    }
}

static void cleanup_dirty_cache_list (commit_t *c)
{
    struct cache_entry *entry;

    while ((entry = zlist_pop (c->dirty_cache_entries_list)))
        commit_cleanup_dirty_cache_entry (c, entry);
}

/* Store object 'o' under key 'ref' in local cache.
 * Object reference is still owned by the caller.
 * 'is_raw' indicates this data is a json string w/ base64 value and
 * should be flushed to the content store as raw data after it is
 * decoded.  Otherwise, the json object should be a treeobj.
 * Returns -1 on error, 0 on success entry already there, 1 on success
 * entry needs to be flushed to content store
 */
static int store_cache (commit_t *c, int current_epoch, json_t *o,
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
            flux_log_error (c->cm->h, "malloc");
            goto error;
        }
        if (base64_decode_block (data, &len, xdata, xlen) < 0) {
            flux_log_error (c->cm->h, "base64_decode_block");
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
            flux_log_error (c->cm->h, "%s: treeobj_encode", __FUNCTION__);
            goto error;
        }
        len = strlen (data);
    }
    if (blobref_hash (c->cm->hash_name, data, len, ref) < 0) {
        flux_log_error (c->cm->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (c->cm->cache, ref, current_epoch))) {
        if (!(entry = cache_entry_create ())) {
            flux_log_error (c->cm->h, "%s: cache_entry_create", __FUNCTION__);
            goto error;
        }
        cache_insert (c->cm->cache, ref, entry);
    }
    if (cache_entry_get_valid (entry)) {
        c->cm->noop_stores++;
        rc = 0;
    }
    else {
        if (cache_entry_set_raw (entry, data, len) < 0) {
            int ret;
            ret = cache_remove_entry (c->cm->cache, ref);
            assert (ret == 1);
            goto error;
        }
        if (cache_entry_set_dirty (entry, true) < 0) {
            flux_log_error (c->cm->h, "%s: cache_entry_set_dirty",__FUNCTION__);
            int ret;
            ret = cache_remove_entry (c->cm->cache, ref);
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
static int commit_unroll (commit_t *c, int current_epoch, json_t *dir)
{
    json_t *dir_entry;
    json_t *dir_data;
    json_t *tmp;
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
            if (commit_unroll (c, current_epoch, dir_entry) < 0) /* depth first */
                return -1;
            if ((ret = store_cache (c, current_epoch, dir_entry,
                                    false, ref, &entry)) < 0)
                return -1;
            if (ret) {
                if (zlist_push (c->dirty_cache_entries_list, entry) < 0) {
                    commit_cleanup_dirty_cache_entry (c, entry);
                    errno = ENOMEM;
                    return -1;
                }
            }
            if (!(tmp = treeobj_create_dirref (ref)))
                return -1;
            if (json_object_iter_set_new (dir, iter, tmp) < 0) {
                json_decref (tmp);
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
                if ((ret = store_cache (c, current_epoch, val_data,
                                        true, ref, &entry)) < 0)
                    return -1;
                if (ret) {
                    if (zlist_push (c->dirty_cache_entries_list, entry) < 0) {
                        commit_cleanup_dirty_cache_entry (c, entry);
                        errno = ENOMEM;
                        return -1;
                    }
                }
                if (!(tmp = treeobj_create_valref (ref)))
                    return -1;
                if (json_object_iter_set_new (dir, iter, tmp) < 0) {
                    json_decref (tmp);
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
        iter = json_object_iter_next (dir_data, iter);
    }

    return 0;
}

static int commit_val_data_to_cache (commit_t *c, int current_epoch,
                                     json_t *val, blobref_t ref)
{
    struct cache_entry *entry;
    json_t *val_data;
    int ret;

    if (!(val_data = treeobj_get_data (val)))
        return -1;

    if ((ret = store_cache (c, current_epoch, val_data,
                            true, ref, &entry)) < 0)
        return -1;

    if (ret) {
        if (zlist_push (c->dirty_cache_entries_list, entry) < 0) {
            commit_cleanup_dirty_cache_entry (c, entry);
            errno = ENOMEM;
            return -1;
        }
    }

    return 0;
}

static int commit_append (commit_t *c, int current_epoch, json_t *dirent,
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

        if (commit_val_data_to_cache (c, current_epoch,
                                      dirent, ref) < 0)
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
        json_t *tmp;
        blobref_t ref1, ref2;

        /* treeobj entry is val, so we need to convert the treeobj
         * into a valref first.  Then the procedure is basically the
         * same as the treeobj valref case above.
         */

        if (commit_val_data_to_cache (c, current_epoch,
                                      entry, ref1) < 0)
            return -1;

        if (commit_val_data_to_cache (c, current_epoch,
                                      dirent, ref2) < 0)
            return -1;

        if (!(tmp = treeobj_create_valref (ref1)))
            return -1;

        if (treeobj_append_blobref (tmp, ref2) < 0) {
            json_decref (tmp);
            return -1;
        }

        if (treeobj_insert_entry (dir, final_name, tmp) < 0) {
            json_decref (tmp);
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
        flux_log (c->cm->h, LOG_ERR, "%s: corrupt treeobj: %s",
                  __FUNCTION__, s);
        free (s);
        errno = ENOTRECOVERABLE;
        return -1;
    }
    return 0;
}

/* link (key, dirent) into directory 'dir'.
 */
static int commit_link_dirent (commit_t *c, int current_epoch,
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
            const json_t *subdirtmp;
            int refcount;

            if ((refcount = treeobj_get_count (dir_entry)) < 0) {
                saved_errno = errno;
                goto done;
            }

            if (refcount != 1) {
                flux_log (c->cm->h, LOG_ERR, "invalid dirref count: %d",
                          refcount);
                saved_errno = ENOTRECOVERABLE;
                goto done;
            }

            if (!(ref = treeobj_get_blobref (dir_entry, 0))) {
                saved_errno = errno;
                goto done;
            }

            if (!(entry = cache_lookup (c->cm->cache, ref, current_epoch))
                || !cache_entry_get_valid (entry)) {
                *missing_ref = ref;
                goto success; /* stall */
            }

            if (!(subdirtmp = cache_entry_get_treeobj (entry))) {
                saved_errno = ENOTRECOVERABLE;
                goto done;
            }

            /* do not corrupt store by modifying orig. */
            if (!(subdir = treeobj_deep_copy (subdirtmp))) {
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
            if (commit_link_dirent (c,
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
            if (commit_append (c, current_epoch, dirent, dir, name) < 0) {
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

commit_process_t commit_process (commit_t *c,
                                 int current_epoch,
                                 const blobref_t rootdir_ref)
{
    /* Incase user calls commit_process() again */
    if (c->errnum)
        return COMMIT_PROCESS_ERROR;

    switch (c->state) {
        case COMMIT_STATE_INIT:
        case COMMIT_STATE_LOAD_ROOT:
        {
            /* Make a copy of the root directory.
             */
            struct cache_entry *entry;
            const json_t *rootdir;

            /* Caller didn't call commit_iter_missing_refs() */
            if (zlist_first (c->missing_refs_list))
                goto stall_load;

            c->state = COMMIT_STATE_LOAD_ROOT;

            if (!(entry = cache_lookup (c->cm->cache,
                                        rootdir_ref,
                                        current_epoch))
                || !cache_entry_get_valid (entry)) {

                if (zlist_push (c->missing_refs_list,
                                (void *)rootdir_ref) < 0) {
                    c->errnum = ENOMEM;
                    return COMMIT_PROCESS_ERROR;
                }
                goto stall_load;
            }

            if (!(rootdir = cache_entry_get_treeobj (entry))) {
                c->errnum = ENOTRECOVERABLE;
                return COMMIT_PROCESS_ERROR;
            }

            if (!(c->rootcpy = treeobj_deep_copy (rootdir))) {
                c->errnum = errno;
                return COMMIT_PROCESS_ERROR;
            }

            c->state = COMMIT_STATE_APPLY_OPS;
            /* fallthrough */
        }
        case COMMIT_STATE_APPLY_OPS:
        {
            /* Apply each op (e.g. key = val) in sequence to the root
             * copy.  A side effect of walking key paths is to convert
             * dirref objects to dir objects in the copy.  This allows
             * the commit to be self-contained in the rootcpy until it
             * is unrolled later on.
             */
            json_t *op, *dirent;
            const char *missing_ref = NULL;
            int i, len = json_array_size (c->ops);
            const char *key;
            int flags;

            /* Caller didn't call commit_iter_missing_refs() */
            if (zlist_first (c->missing_refs_list))
                goto stall_load;

            for (i = 0; i < len; i++) {
                missing_ref = NULL;
                op = json_array_get (c->ops, i);
                assert (op != NULL);
                if (txn_decode_op (op, &key, &flags, &dirent) < 0) {
                    c->errnum = errno;
                    break;
                }
                if (commit_link_dirent (c,
                                        current_epoch,
                                        c->rootcpy,
                                        key,
                                        dirent,
                                        flags,
                                        &missing_ref) < 0) {
                    c->errnum = errno;
                    break;
                }
                if (missing_ref) {
                    if (zlist_push (c->missing_refs_list,
                                    (void *)missing_ref) < 0) {
                        c->errnum = ENOMEM;
                        break;
                    }
                }
            }

            if (c->errnum != 0) {
                /* empty missing_refs_list to prevent mistakes later */
                while ((missing_ref = zlist_pop (c->missing_refs_list)));
                return COMMIT_PROCESS_ERROR;
            }

            if (zlist_first (c->missing_refs_list))
                goto stall_load;

            c->state = COMMIT_STATE_STORE;
            /* fallthrough */
        }
        case COMMIT_STATE_STORE:
        {
            /* Unroll the root copy.
             * When a dir is found, store an object and replace it
             * with a dirref.  Finally, store the unrolled root copy
             * as an object and keep its reference in c->newroot.
             * Flushes to content cache are asynchronous but we don't
             * proceed until they are completed.
             */
            struct cache_entry *entry;
            int sret;

            if (commit_unroll (c, current_epoch, c->rootcpy) < 0)
                c->errnum = errno;
            else if ((sret = store_cache (c,
                                          current_epoch,
                                          c->rootcpy,
                                          false,
                                          c->newroot,
                                          &entry)) < 0)
                c->errnum = errno;
            else if (sret
                     && zlist_push (c->dirty_cache_entries_list, entry) < 0) {
                commit_cleanup_dirty_cache_entry (c, entry);
                c->errnum = ENOMEM;
            }

            if (c->errnum) {
                cleanup_dirty_cache_list (c);
                return COMMIT_PROCESS_ERROR;
            }

            /* cache now has ownership of rootcpy, we don't need our
             * rootcpy anymore.  But we may still need to stall user.
             */
            c->state = COMMIT_STATE_PRE_FINISHED;
            json_decref (c->rootcpy);
            c->rootcpy = NULL;

            /* fallthrough */
        }
        case COMMIT_STATE_PRE_FINISHED:
            /* If we did not fall through to here, caller didn't call
             * commit_iter_dirty_cache_entries()
             */
            if (zlist_first (c->dirty_cache_entries_list))
                goto stall_store;

            c->state = COMMIT_STATE_FINISHED;
            /* fallthrough */
        case COMMIT_STATE_FINISHED:
            break;
        default:
            flux_log (c->cm->h, LOG_ERR, "invalid commit state: %d", c->state);
            c->errnum = ENOTRECOVERABLE;
            return COMMIT_PROCESS_ERROR;
    }

    return COMMIT_PROCESS_FINISHED;

stall_load:
    c->blocked = 1;
    return COMMIT_PROCESS_LOAD_MISSING_REFS;

stall_store:
    c->blocked = 1;
    return COMMIT_PROCESS_DIRTY_CACHE_ENTRIES;
}

int commit_iter_missing_refs (commit_t *c, commit_ref_f cb, void *data)
{
    const char *ref;
    int saved_errno, rc = 0;

    if (c->state != COMMIT_STATE_LOAD_ROOT
        && c->state != COMMIT_STATE_APPLY_OPS) {
        errno = EINVAL;
        return -1;
    }

    while ((ref = zlist_pop (c->missing_refs_list))) {
        if (cb (c, ref, data) < 0) {
            saved_errno = errno;
            rc = -1;
            break;
        }
    }

    if (rc < 0) {
        while ((ref = zlist_pop (c->missing_refs_list)));
        errno = saved_errno;
    }

    return rc;
}

int commit_iter_dirty_cache_entries (commit_t *c,
                                     commit_cache_entry_f cb,
                                     void *data)
{
    struct cache_entry *entry;
    int saved_errno, rc = 0;

    if (c->state != COMMIT_STATE_PRE_FINISHED) {
        errno = EINVAL;
        return -1;
    }

    while ((entry = zlist_pop (c->dirty_cache_entries_list))) {
        if (cb (c, entry, data) < 0) {
            saved_errno = errno;
            rc = -1;
            break;
        }
    }

    if (rc < 0) {
        cleanup_dirty_cache_list (c);
        errno = saved_errno;
    }

    return rc;
}

commit_mgr_t *commit_mgr_create (struct cache *cache,
                                 const char *namespace,
                                 const char *hash_name,
                                 flux_t *h,
                                 void *aux)
{
    commit_mgr_t *cm = NULL;
    int saved_errno;

    if (!cache || !namespace || !hash_name) {
        saved_errno = EINVAL;
        goto error;
    }

    if (!(cm = calloc (1, sizeof (*cm)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    cm->cache = cache;
    cm->namespace = namespace;
    cm->hash_name = hash_name;
    if (!(cm->fences = zhash_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(cm->ready = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    cm->iterating_fences = false;
    if (!(cm->removelist = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }
    cm->h = h;
    cm->aux = aux;
    return cm;

 error:
    commit_mgr_destroy (cm);
    errno = saved_errno;
    return NULL;
}

void commit_mgr_destroy (commit_mgr_t *cm)
{
    if (cm) {
        if (cm->fences)
            zhash_destroy (&cm->fences);
        if (cm->ready)
            zlist_destroy (&cm->ready);
        if (cm->removelist)
            zlist_destroy (&cm->removelist);
        free (cm);
    }
}

int commit_mgr_add_fence (commit_mgr_t *cm, fence_t *f)
{
    /* Don't modify hash while iterating */
    if (cm->iterating_fences) {
        errno = EAGAIN;
        goto error;
    }

    if (zhash_insert (cm->fences, fence_get_name (f), f) < 0) {
        errno = EEXIST;
        goto error;
    }

    /* initial fence aux int to 0 */
    fence_set_aux_int (f, 0);
    zhash_freefn (cm->fences,
                  fence_get_name (f),
                  (zhash_free_fn *)fence_destroy);
    return 0;
error:
    return -1;
}

fence_t *commit_mgr_lookup_fence (commit_mgr_t *cm, const char *name)
{
    return zhash_lookup (cm->fences, name);
}

int commit_mgr_iter_not_ready_fences (commit_mgr_t *cm, commit_fence_f cb,
                                      void *data)
{
    fence_t *f;
    char *name;

    cm->iterating_fences = true;

    f = zhash_first (cm->fences);
    while (f) {
        if (!fence_get_processed (f)) {
            if (cb (f, data) < 0)
                goto error;
        }

        f = zhash_next (cm->fences);
    }

    cm->iterating_fences = false;

    while ((name = zlist_pop (cm->removelist))) {
        commit_mgr_remove_fence (cm, name);
        free (name);
    }

    return 0;

error:
    while ((name = zlist_pop (cm->removelist)))
        free (name);
    cm->iterating_fences = false;
    return -1;
}

int commit_mgr_process_fence_request (commit_mgr_t *cm, fence_t *f)
{
    if (fence_count_reached (f)) {
        commit_t *c;

        /* fence is already processed */
        if (fence_get_processed (f))
            return 0;

        if (!(c = commit_create (f, cm)))
            return -1;

        if (zlist_append (cm->ready, c) < 0) {
            commit_destroy (c);
            errno = ENOMEM;
            return -1;
        }
        /* we use this flag to indicate if a fence is "ready" */
        fence_set_processed (f, true);
        zlist_freefn (cm->ready, c, (zlist_free_fn *)commit_destroy, true);
    }

    return 0;
}

bool commit_mgr_commits_ready (commit_mgr_t *cm)
{
    commit_t *c;

    if ((c = zlist_first (cm->ready)) && !c->blocked)
        return true;
    return false;
}

commit_t *commit_mgr_get_ready_commit (commit_mgr_t *cm)
{
    if (commit_mgr_commits_ready (cm))
        return zlist_first (cm->ready);
    return NULL;
}

void commit_mgr_remove_commit (commit_mgr_t *cm, commit_t *c)
{
    zlist_remove (cm->ready, c);
}

int commit_mgr_remove_fence (commit_mgr_t *cm, const char *name)
{
    /* it's dangerous to remove if we're in the middle of an
     * interation, so save name for removal later.
     */
    if (cm->iterating_fences) {
        char *str = strdup (name);

        if (!str) {
            errno = ENOMEM;
            return -1;
        }

        if (zlist_append (cm->removelist, str) < 0) {
            free (str);
            errno = ENOMEM;
            return -1;
        }
    }
    else
        zhash_delete (cm->fences, name);
    return 0;
}

int commit_mgr_get_noop_stores (commit_mgr_t *cm)
{
    return cm->noop_stores;
}

void commit_mgr_clear_noop_stores (commit_mgr_t *cm)
{
    cm->noop_stores = 0;
}

int commit_mgr_fences_count (commit_mgr_t *cm)
{
    return zhash_size (cm->fences);
}

int commit_mgr_ready_commit_count (commit_mgr_t *cm)
{
    return zlist_size (cm->ready);
}

static int commit_merge (commit_t *dest, commit_t *src)
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

/* Merge ready commits that are mergeable, where merging consists of
 * popping the "donor" commit off the ready list, and appending its
 * ops to the top commit.  The top commit can be appended to if it
 * hasn't started, or is still building the rootcpy, e.g. stalled
 * walking the namespace.
 *
 * Break when an unmergeable commit is discovered.  We do not wish to
 * merge non-adjacent fences, as it can create undesireable out of
 * order scenarios.  e.g.
 *
 * commit #1 is mergeable:     set A=1
 * commit #2 is non-mergeable: set A=2
 * commit #3 is mergeable:     set A=3
 *
 * If we were to merge commit #1 and commit #3, A=2 would be set after
 * A=3.
 */

int commit_mgr_merge_ready_commits (commit_mgr_t *cm)
{
    commit_t *c = zlist_first (cm->ready);

    /* commit must still be in state where merged in ops can be
     * applied */
    if (c
        && c->errnum == 0
        && c->state <= COMMIT_STATE_APPLY_OPS
        && !(c->flags & FLUX_KVS_NO_MERGE)) {
        commit_t *nc;
        while ((nc = zlist_next (cm->ready))) {
            int ret;

            if ((ret = commit_merge (c, nc)) < 0)
                return -1;

            /* if return == 0, we've merged as many as we currently
             * can */
            if (!ret)
                break;

            /* Merged commit, remove off ready list */
            zlist_remove (cm->ready, nc);
        }
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
