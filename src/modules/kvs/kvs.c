/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

#include "waitqueue.h"
#include "proto.h"
#include "cache.h"
#include "json_dirent.h"
#include "json_util.h"

typedef char href_t[BLOBREF_MAX_STRING_SIZE];

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

/* Expire cache_entry after 'max_lastuse_age' heartbeats.
 */
const int max_lastuse_age = 5;

/* Include root directory in kvs.setroot event.
 */
const bool event_includes_rootdir = true;

/* Statistics gathered for kvs.stats.get, etc.
 */
typedef struct {
    int faults;
    int noop_stores;
} stats_t;

typedef struct {
    struct cache *cache;    /* blobref => cache_entry */
    href_t rootdir;         /* current root blobref */
    int rootseq;            /* current root version (for ordering) */
    zhash_t *fences;
    zlist_t *ready;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    stats_t stats;
    flux_t *h;
    uint32_t rank;
    int epoch;              /* tracks current heartbeat epoch */
    struct json_tokener *tok;
    flux_watcher_t *prep_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int commit_merge;
    const char *hash_name;
} kvs_ctx_t;

typedef struct {
    json_object *ops;
    zlist_t *requests;
    json_object *names;
    int nprocs;
    int flags;
    int count;
    int errnum;
    kvs_ctx_t *ctx;
    json_object *rootcpy;   /* working copy of root dir */
    int blocked:1;
    int rootcpy_stored:1;
    href_t newroot;
} fence_t;

static int setroot_event_send (kvs_ctx_t *ctx, json_object *names);
static int error_event_send (kvs_ctx_t *ctx, json_object *names, int errnum);
static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg);
static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg);

static void freectx (void *arg)
{
    kvs_ctx_t *ctx = arg;
    if (ctx) {
        cache_destroy (ctx->cache);
        zhash_destroy (&ctx->fences);
        zlist_destroy (&ctx->ready);
        if (ctx->watchlist)
            wait_queue_destroy (ctx->watchlist);
        if (ctx->tok)
            json_tokener_free (ctx->tok);
        flux_watcher_destroy (ctx->prep_w);
        flux_watcher_destroy (ctx->check_w);
        flux_watcher_destroy (ctx->idle_w);
        free (ctx);
    }
}

static kvs_ctx_t *getctx (flux_t *h)
{
    kvs_ctx_t *ctx = (kvs_ctx_t *)flux_aux_get (h, "kvssrv");
    flux_reactor_t *r;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(r = flux_get_reactor (h)))
            goto error;
        ctx->cache = cache_create ();
        ctx->watchlist = wait_queue_create ();
        ctx->fences = zhash_new ();
        ctx->ready = zlist_new ();
        if (!ctx->cache || !ctx->watchlist || !ctx->fences || !ctx->ready) {
            errno = ENOMEM;
            goto error;
        }
        ctx->h = h;
        if (flux_get_rank (h, &ctx->rank) < 0)
            goto error;
        if (!(ctx->tok = json_tokener_new ())) {
            errno = ENOMEM;
            goto error;
        }
        if (ctx->rank == 0) {
            ctx->prep_w = flux_prepare_watcher_create (r, commit_prep_cb, ctx);
            ctx->check_w = flux_check_watcher_create (r, commit_check_cb, ctx);
            ctx->idle_w = flux_idle_watcher_create (r, NULL, NULL);
            if (!ctx->prep_w || !ctx->check_w || !ctx->idle_w)
                goto error;
            flux_watcher_start (ctx->prep_w);
            flux_watcher_start (ctx->check_w);
        }
        ctx->commit_merge = 1;
        if (!(ctx->hash_name = flux_attr_get (h, "content.hash", NULL))) {
            flux_log_error (h, "content.hash");
            goto error;
        }
        flux_aux_set (h, "kvssrv", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static void content_load_completion (flux_rpc_t *rpc, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_object *o;
    const void *data;
    int size;
    const char *blobref;
    struct cache_entry *hp;

    if (flux_rpc_get_raw (rpc, &data, &size) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    blobref = flux_rpc_aux_get (rpc, "ref");
    if (!(o = json_tokener_parse_ex (ctx->tok, (char *)data, size))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        json_tokener_reset (ctx->tok);
        goto done;
    }
    if ((hp = cache_lookup (ctx->cache, blobref, ctx->epoch)))
        cache_entry_set_json (hp, o);
    else {
        hp = cache_entry_create (o);
        cache_insert (ctx->cache, blobref, hp);
    }
done:
    flux_rpc_destroy (rpc);
}

/* If now is true, perform the load rpc synchronously;
 * otherwise arrange for a continuation to handle the response.
 */
static int content_load_request_send (kvs_ctx_t *ctx, const href_t ref, bool now)
{
    flux_rpc_t *rpc = NULL;

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    if (!(rpc = flux_rpc_raw (ctx->h, "content.load",
                    ref, strlen (ref) + 1, FLUX_NODEID_ANY, 0)))
        goto error;
    if (flux_rpc_aux_set (rpc, "ref", xstrdup (ref), free) < 0)
        goto error;
    if (now) {
        content_load_completion (rpc, ctx);
    } else if (flux_rpc_then (rpc, content_load_completion, ctx) < 0) {
        flux_rpc_destroy (rpc);
        goto error;
    }
    return 0;
error:
    return -1;
}

/* Return true if load successful, false if stalling */
static bool load (kvs_ctx_t *ctx, const href_t ref, wait_t *wait, json_object **op)
{
    struct cache_entry *hp = cache_lookup (ctx->cache, ref, ctx->epoch);

    /* Create an incomplete hash entry if none found.
     */
    if (!hp) {
        hp = cache_entry_create (NULL);
        cache_insert (ctx->cache, ref, hp);
        if (content_load_request_send (ctx, ref, wait ? false : true) < 0)
            flux_log_error (ctx->h, "content_load_request_send");
        ctx->stats.faults++;
    }
    /* If hash entry is incomplete (either created above or earlier),
     * arrange to stall caller if wait_t was provided.
     */
    if (!cache_entry_get_valid (hp)) {
        cache_entry_wait_valid (hp, wait);
        return false;
    }

    if (op)
        *op = cache_entry_get_json (hp);
    return true;
}

static int content_store_get (flux_rpc_t *rpc, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct cache_entry *hp;
    const char *blobref;
    int blobref_size;
    int rc = -1;

    if (flux_rpc_get_raw (rpc, &blobref, &blobref_size) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    hp = cache_lookup (ctx->cache, blobref, ctx->epoch);
    cache_entry_set_dirty (hp, false);
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

static void content_store_completion (flux_rpc_t *rpc, void *arg)
{
    (void)content_store_get (rpc, arg);
}

static int content_store_request_send (kvs_ctx_t *ctx, const href_t ref,
                                       json_object *val, bool now)
{
    flux_rpc_t *rpc;
    const char *data = Jtostr (val);
    int size = strlen (data) + 1;

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    if (!(rpc = flux_rpc_raw (ctx->h, "content.store",
                              data, size, FLUX_NODEID_ANY, 0)))
        goto error;
    if (now) {
        if (content_store_get (rpc, ctx) < 0)
            goto error;
    } else if (flux_rpc_then (rpc, content_store_completion, ctx) < 0) {
        flux_rpc_destroy (rpc);
        goto error;
    }
    return 0;
error:
    return -1;
}

/* Store object 'o' under key 'ref' in local cache.
 * If 'wait' is NULL, flush to content cache synchronously; otherwise,
 * do it asynchronously and push wait onto cache object's wait queue.
 * FIXME: asynchronous errors need to be propagated back to caller.
 */
static int store (kvs_ctx_t *ctx, json_object *o, href_t ref, wait_t *wait)
{
    struct cache_entry *hp;
    const char *s = json_object_to_json_string (o);
    int rc = -1;

    if (blobref_hash (ctx->hash_name, (uint8_t *)s, strlen (s) + 1,
                      ref, sizeof (href_t)) < 0) {
        flux_log_error (ctx->h, "blobref_hash");
        goto done;
    }
    if (!(hp = cache_lookup (ctx->cache, ref, ctx->epoch))) {
        hp = cache_entry_create (NULL);
        cache_insert (ctx->cache, ref, hp);
    }
    if (cache_entry_get_valid (hp)) {
        ctx->stats.noop_stores++;
        json_object_put (o);
    } else {
        cache_entry_set_json (hp, o);
        cache_entry_set_dirty (hp, true);
        if (wait) {
            if (content_store_request_send (ctx, ref, o, false) < 0) {
                flux_log_error (ctx->h, "content_store");
                goto done;
            }
        } else {
            if (content_store_request_send (ctx, ref, o, true) < 0) {
                flux_log_error (ctx->h, "content_store");
                goto done;
            }
        }
    }
    if (wait && cache_entry_get_dirty (hp))
        cache_entry_wait_notdirty (hp, wait);
    rc = 0;
done:
    return rc;
}

static void setroot (kvs_ctx_t *ctx, const char *rootdir, int rootseq)
{
    if (rootseq == 0 || rootseq > ctx->rootseq) {
        assert (strlen (rootdir) < sizeof (href_t));
        strcpy (ctx->rootdir, rootdir);
        ctx->rootseq = rootseq;
        wait_runqueue (ctx->watchlist);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
}

/* Store DIRVAL objects, converting them to DIRREFs.
 * Store (large) FILEVAL objects, converting them to FILEREFs.
 * Return false and enqueue wait_t on cache object(s) if any are dirty.
 */
static int commit_unroll (kvs_ctx_t *ctx, json_object *dir, wait_t *wait)
{
    json_object_iter iter;
    json_object *subdir, *value;
    const char *s;
    href_t ref;
    int rc = -1;

    json_object_object_foreachC (dir, iter) {
        if (json_object_object_get_ex (iter.val, "DIRVAL", &subdir)) {
            if (commit_unroll (ctx, subdir, wait) < 0) /* depth first */
                goto done;
            json_object_get (subdir);
            if (store (ctx, subdir, ref, wait) < 0)
                goto done;
            json_object_object_add (dir, iter.key,
                                    dirent_create ("DIRREF", ref));
        }
        else if (json_object_object_get_ex (iter.val, "FILEVAL", &value)
                                && (s = Jtostr (value))
                                && strlen (s) > BLOBREF_MAX_STRING_SIZE) {
            json_object_get (value);
            if (store (ctx, value, ref, wait) < 0)
                goto done;
            json_object_object_add (dir, iter.key,
                                    dirent_create ("FILEREF", ref));
        }
    }
    rc = 0;
done:
    return rc;
}

/* link (key, dirent) into directory 'dir'.
 */
static int commit_link_dirent (kvs_ctx_t *ctx, json_object *rootdir,
                               const char *key, json_object *dirent,
                               wait_t *wait)
{
    char *cpy = xstrdup (key);
    char *next, *name = cpy;
    json_object *dir = rootdir;
    json_object *o, *subdir = NULL, *subdirent;
    int rc = -1;

    /* Special case root
     */
    if (strcmp (name, ".") == 0) {
        errno = EINVAL;
        goto done;
    }

    /* This is the first part of a key with multiple path components.
     * Make sure that it is a directory in DIRVAL form, then recurse
     * on the remaining path components.
     */
    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!json_object_object_get_ex (dir, name, &subdirent)) {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto success;
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else if (json_object_object_get_ex (subdirent, "DIRVAL", &o)) {
            subdir = o;
        } else if (json_object_object_get_ex (subdirent, "DIRREF", &o)) {
            if (!load (ctx, json_object_get_string (o), wait, &subdir))
                goto success; /* stall */
            /* do not corrupt store by modify orig. */
            subdir = json_object_copydir (subdir);
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else if (json_object_object_get_ex (subdirent, "LINKVAL", &o)) {
            FASSERT (ctx->h, json_object_get_type (o) == json_type_string);
            char *nkey = xasprintf ("%s.%s", json_object_get_string (o), next);
            if (commit_link_dirent (ctx, rootdir, nkey, dirent, wait) < 0) {
                free (nkey);
                goto done;
            }
            free (nkey);
            goto success;
        } else {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto success;
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        }
        name = next;
        dir = subdir;
    }
    /* This is the final path component of the key.  Add it to the directory.
     */
    if (dirent)
        json_object_object_add (dir, name, json_object_get (dirent));
    else
        json_object_object_del (dir, name);
success:
    rc = 0;
done:
    free (cpy);
    return rc;
}

/* Commit all the ops for a particular commit/fence request (rank 0 only).
 * The setroot event will cause responses to be sent to the fence requests
 * and clean up the fence_t state.  This function is idempotent.
 * Error handling: set f->errnum to errno, then let process complete.
 */
static void commit_apply_fence (fence_t *f)
{
    kvs_ctx_t *ctx = f->ctx;
    wait_t *wait = NULL;
    int count;

    if (f->errnum)
        goto done;
    if (!(wait = wait_create ((wait_cb_f)commit_apply_fence, f))) {
        f->errnum = errno;
        goto done;
    }

    /* Make a copy of the root directory.
     */
    if (!f->rootcpy_stored && !f->rootcpy) {
        json_object *rootdir;
        if (!load (ctx, ctx->rootdir, wait, &rootdir))
            goto stall;
        f->rootcpy = json_object_copydir (rootdir);
    }

    /* Apply each op (e.g. key = val) in sequence to the root copy.
     * A side effect of walking key paths is to convert DIRREFs to DIRVALs
     * in the copy. This allows the commit to be self-contained in the rootcpy
     * until it is unrolled later on.
     */
    if (f->ops && !f->rootcpy_stored) {
        int i, len = json_object_array_length (f->ops);
        json_object *op, *dirent;
        const char *key;

        for (i = 0; i < len; i++) {
            if (!(op = json_object_array_get_idx (f->ops, i))
                    || !Jget_str (op, "key", &key))
                continue;
            dirent = NULL;
            (void)Jget_obj (op, "dirent", &dirent); /* NULL for unlink */
            if (commit_link_dirent (ctx, f->rootcpy, key, dirent, wait) < 0) {
                f->errnum = errno;
                break;
            }
        }
        if (wait_get_usecount (wait) > 0)
            goto stall;
        if (f->errnum != 0)
            goto done;
    }

    /* Unroll the root copy.
     * When a DIRVAL is found, store an object and replace it with a DIRREF.
     * Finally, store the unrolled root copy as an object and keep its
     * reference in f->newroot.  Flushes to content cache are asynchronous
     * but we don't proceed until they are completed.
     */
    if (!f->rootcpy_stored) {
        if (commit_unroll (ctx, f->rootcpy, wait) < 0)
            f->errnum = errno;
        else if (store (ctx, f->rootcpy, f->newroot, wait) < 0)
            f->errnum = errno;
        f->rootcpy_stored = true; /* cache takes ownership of rootcpy */
        f->rootcpy = NULL;
        if (wait_get_usecount (wait) > 0)
            goto stall;
    }

    /* This is the transaction that finalizes the commit by replacing
     * ctx->rootdir with f->newroot, incrementing the root seq,
     * and sending out the setroot event for "eventual consistency"
     * of other nodes.
     */
done:
    if (f->errnum == 0) {
        if (Jget_ar_len (f->names, &count) && count > 1) {
            int opcount = 0;
            (void)Jget_ar_len (f->ops, &opcount);
            flux_log (ctx->h, LOG_DEBUG, "aggregated %d commits (%d ops)",
                      count, opcount);
        }
        setroot (ctx, f->newroot, ctx->rootseq + 1);
        setroot_event_send (ctx, f->names);
    } else {
        flux_log (ctx->h, LOG_ERR, "commit failed: %s",
                  flux_strerror (f->errnum));
        error_event_send (ctx, f->names, f->errnum);
    }
    wait_destroy (wait);

    /* Completed: remove from 'ready' list.
     * N.B. fence_t remains in the fences hash until event is received.
     */
    zlist_remove (ctx->ready, f);
    return;

stall:
    f->blocked = 1;
    return;
}

static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    fence_t *f;
    if ((f = zlist_first (ctx->ready)) && !f->blocked)
        flux_watcher_start (ctx->idle_w);
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
static void commit_merge_all (kvs_ctx_t *ctx)
{
    fence_t *f = zlist_first (ctx->ready);

    if (f
        && f->errnum == 0
        && !f->rootcpy_stored
        && !(f->flags & KVS_NO_MERGE)) {
        fence_t *nf;
        nf = zlist_pop (ctx->ready);
        assert (nf == f);
        while ((nf = zlist_first (ctx->ready))) {
            int i, len;

            /* We've merged as many as we currently can */
            if (nf->flags & KVS_NO_MERGE)
                break;

            /* Fence is mergeable, pop off list */
            nf = zlist_pop (ctx->ready);

            if (Jget_ar_len (nf->names, &len)) {
                for (i = 0; i < len; i++) {
                    const char *name;
                    if (Jget_ar_str (nf->names, i, &name))
                        Jadd_ar_str (f->names, name);
                }
            }
            if (Jget_ar_len (nf->ops, &len)) {
                for (i = 0; i < len; i++) {
                    json_object *op;
                    if (Jget_ar_obj (nf->ops, i, &op))
                        Jadd_ar_obj (f->ops, op);
                }
            }
        }
        if (zlist_push (ctx->ready, f) < 0)
            oom ();
    }
}

static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    fence_t *f;

    flux_watcher_stop (ctx->idle_w);

    if ((f = zlist_first (ctx->ready))) {
        if (ctx->commit_merge)
            commit_merge_all (ctx);
        if (!f->blocked)
            commit_apply_fence (f);
    }
}

static void dropcache_request_cb (flux_t *h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int size, expcount = 0;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    size = cache_count_entries (ctx->cache);
    expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0);
    flux_log (h, LOG_ALERT, "dropped %d of %d cache entries", expcount, size);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void dropcache_event_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int size, expcount = 0;

    if (flux_event_decode (msg, NULL, NULL) < 0)
        return;
    size = cache_count_entries (ctx->cache);
    expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0);
    flux_log (h, LOG_ALERT, "dropped %d of %d cache entries", expcount, size);
}

static void heartbeat_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        return;
    }
    /* "touch" objects involved in watched keys */
    if (ctx->epoch - ctx->watchlist_lastrun_epoch > max_lastuse_age) {
        wait_runqueue (ctx->watchlist);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
    /* "touch" root */
    (void)load (ctx, ctx->rootdir, NULL, NULL);

    cache_expire_entries (ctx->cache, ctx->epoch, max_lastuse_age);
}

/* Get dirent containing requested key.
 */
static bool walk (kvs_ctx_t *ctx, json_object *root, const char *path,
                  json_object **direntp, int flags, int depth,
                  const char **missing_ref, int *ep)
{
    char *cpy = xstrdup (path);
    char *next, *name = cpy;
    const char *ref;
    const char *link;
    json_object *dirent = NULL;
    json_object *dir = root;
    int errnum = 0;

    depth++;

    /* walk directories */
    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!json_object_object_get_ex (dir, name, &dirent))
            /* not necessarily ENOENT, let caller decide */
            goto error;
        if (Jget_str (dirent, "LINKVAL", &link)) {
            if (depth == SYMLINK_CYCLE_LIMIT) {
                errnum = ELOOP;
                goto error;
            }
            if (!walk (ctx, root, link, &dirent, flags, depth, missing_ref, ep))
                goto stall;
            if (*ep != 0) {
                errnum = *ep;
                goto error;
            }
            if (!dirent)
                /* not necessarily ENOENT, let caller decide */
                goto error;
        }

        /* Check for errors in dirent before looking up reference.
         * Note that reference to lookup is determined in final
         * error check.
         */

        if (json_object_object_get_ex (dirent, "DIRVAL", NULL)) {
            /* N.B. in current code, directories are never stored by value */
            log_msg_exit ("%s: unexpected DIRVAL: path=%s name=%s: dirent=%s ",
                          __FUNCTION__, path, name, Jtostr (dirent));
        } else if ((Jget_str (dirent, "FILEREF", NULL)
                    || json_object_object_get_ex (dirent, "FILEVAL", NULL))) {
            /* don't return ENOENT or ENOTDIR, error to be determined
             * by caller */
            goto error;
        } else if (!Jget_str (dirent, "DIRREF", &ref)) {
            log_msg_exit ("%s: unknown dirent type: path=%s name=%s: dirent=%s ",
                          __FUNCTION__, path, name, Jtostr (dirent));
        }

        if (!(dir = cache_lookup_and_get_json (ctx->cache,
                                               ref,
                                               ctx->epoch))) {
            *missing_ref = ref;
            goto stall;
        }

        name = next;
    }
    /* now terminal path component */
    if (json_object_object_get_ex (dir, name, &dirent) &&
        Jget_str (dirent, "LINKVAL", &link)) {
        if (!(flags & KVS_PROTO_READLINK) && !(flags & KVS_PROTO_TREEOBJ)) {
            if (depth == SYMLINK_CYCLE_LIMIT) {
                errnum = ELOOP;
                goto error;
            }
            if (!walk (ctx, root, link, &dirent, flags, depth, missing_ref, ep))
                goto stall;
            if (*ep != 0) {
                errnum = *ep;
                goto error;
            }
        }
    }
    free (cpy);
    *direntp = dirent;
    return true;
error:
    if (errnum != 0)
        *ep = errnum;
    free (cpy);
    *direntp = NULL;
    return true;
stall:
    free (cpy);
    return false;
}

static bool lookup (kvs_ctx_t *ctx, json_object *root,
                    int flags, const char *name,
                    json_object **valp, const char **missing_ref, int *ep)
{
    json_object *vp, *dirent, *val = NULL;
    int walk_errnum = 0;
    int errnum = 0;

    assert (root != NULL);

    if (!strcmp (name, ".")) { /* special case root */
        if ((flags & KVS_PROTO_TREEOBJ)) {
            val = dirent_create ("DIRREF", ctx->rootdir);
        } else {
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            val = json_object_get (root);
        }
    } else {
        if (!walk (ctx, root, name, &dirent, flags, 0,
                   missing_ref, &walk_errnum))
            goto stall;
        if (walk_errnum != 0) {
            errnum = walk_errnum;
            goto done;
        }
        if (!dirent) {
            //errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }
        if ((flags & KVS_PROTO_TREEOBJ)) {
            val = json_object_get (dirent);
            goto done;
        }
        if (json_object_object_get_ex (dirent, "DIRREF", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            if (!(val = cache_lookup_and_get_json (ctx->cache,
                                                   json_object_get_string (vp),
                                                   ctx->epoch))) {
                *missing_ref = json_object_get_string (vp);
                goto stall;
            }
            val = json_object_copydir (val);
        } else if (json_object_object_get_ex (dirent, "FILEREF", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if ((flags & KVS_PROTO_READDIR)) {
                errnum = ENOTDIR;
                goto done;
            }
            if (!(val = cache_lookup_and_get_json (ctx->cache,
                                                   json_object_get_string (vp),
                                                   ctx->epoch))) {
                *missing_ref = json_object_get_string (vp);
                goto stall;
            }
            val = json_object_get (val);
        } else if (json_object_object_get_ex (dirent, "DIRVAL", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if (!(flags & KVS_PROTO_READDIR)) {
                errnum = EISDIR;
                goto done;
            }
            val = json_object_copydir (vp);
        } else if (json_object_object_get_ex (dirent, "FILEVAL", &vp)) {
            if ((flags & KVS_PROTO_READLINK)) {
                errnum = EINVAL;
                goto done;
            }
            if ((flags & KVS_PROTO_READDIR)) {
                errnum = ENOTDIR;
                goto done;
            }
            val = json_object_get (vp);
        } else if (json_object_object_get_ex (dirent, "LINKVAL", &vp)) {
            if (!(flags & KVS_PROTO_READLINK) || (flags & KVS_PROTO_READDIR)) {
                errnum = EPROTO;
                goto done;
            }
            val = json_object_get (vp);
        } else
            log_msg_exit ("%s: corrupt dirent: %s", __FUNCTION__,
                          Jtostr (dirent));
    }
    /* val now contains the requested object (copied) */
done:
    *valp = val;
    if (errnum != 0)
        *ep = errnum;
    return true;
stall:
    return false;
}

static void get_request_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;
    json_object *out = NULL;
    int flags;
    const char *key;
    json_object *val;
    json_object *root = NULL;
    json_object *root_dirent = NULL;
    json_object *tmp_dirent = NULL;
    const char *root_ref = ctx->rootdir;
    const char *missing_ref = NULL;
    wait_t *wait = NULL;
    int lookup_errnum = 0;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!json_str || !(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_tget_dec (in, &root_dirent, &key, &flags) < 0)
        goto done;
    if (!(wait = wait_create_msg_handler (h, w, msg, get_request_cb, arg)))
        goto done;
    /* If root dirent was specified, lookup corresponding 'root' directory.
     * Otherwise, use the current root.
     */
    if (root_dirent) {
        if (!Jget_str (root_dirent, "DIRREF", &root_ref)) {
            errno = EINVAL;
            goto done;
        }
    } else {
        root_dirent = tmp_dirent = dirent_create ("DIRREF", ctx->rootdir);
    }
    if (!load (ctx, root_ref, wait, &root))
        goto stall;
    if (!lookup (ctx, root, flags, key, &val, &missing_ref, &lookup_errnum)) {
        assert (missing_ref);
        if (load (ctx, missing_ref, wait, NULL))
            log_msg_exit ("%s: failure in load logic", __FUNCTION__);
        goto stall;
    }
    if (lookup_errnum != 0) {
        errno = lookup_errnum;
        goto done;
    }
    if (val == NULL) {
        errno = ENOENT;
        goto done;
    }
    if (!(out = kp_rget_enc (Jget (root_dirent), val)))
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    wait_destroy (wait);
stall:
    Jput (in);
    Jput (out);
    Jput (tmp_dirent);
}

static void watch_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;
    json_object *in2 = NULL;
    json_object *out = NULL;
    json_object *oval;
    json_object *val = NULL;
    json_object *root = NULL;
    flux_msg_t *cpy = NULL;
    const char *key;
    int flags;
    int lookup_errnum = 0;
    const char *missing_ref = NULL;
    wait_t *wait = NULL;
    wait_t *watcher = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!json_str || !(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_twatch_dec (in, &key, &oval, &flags) < 0)
        goto done;
    if (!(wait = wait_create_msg_handler (h, w, msg, watch_request_cb, arg)))
        goto done;
    if (!load (ctx, ctx->rootdir, wait, &root))
        goto stall;
    if (!lookup (ctx, root, flags, key, &val, &missing_ref, &lookup_errnum)) {
        assert (missing_ref);
        if (load (ctx, missing_ref, wait, NULL))
            log_msg_exit ("%s: failure in load logic", __FUNCTION__);
        goto stall;
    }
    if (lookup_errnum) {
        errno = lookup_errnum;
        goto done;
    }
    /* Value changed or this is the initial request, so prepare a reply.
     */
    if ((flags & KVS_PROTO_FIRST) || !json_compare (val, oval)) {
        if (!(out = kp_rwatch_enc (Jget (val))))
            goto done;
    }
    /* No reply sent or this is a multi-response watch request.
     * Arrange to wait on ctx->watchlist for each new commit.
     * Reconstruct the payload with 'first' flag clear, and updated value.
     */
    if (!out || !(flags & KVS_PROTO_ONCE)) {
        if (!(cpy = flux_msg_copy (msg, false)))
            goto done;
        if (!(in2 = kp_twatch_enc (key, Jget (val), flags & ~KVS_PROTO_FIRST)))
            goto done;
        if (flux_msg_set_json (cpy, Jtostr (in2)) < 0)
            goto done;
        if (!(watcher = wait_create_msg_handler (h, w, cpy,
                                                 watch_request_cb, arg)))
            goto done;
        wait_addqueue (ctx->watchlist, watcher);
    }
    rc = 0;
done:
    if (rc < 0 || out != NULL) {
        if (flux_respond (h, msg, rc < 0 ? errno : 0,
                                  rc < 0 ? NULL : Jtostr (out)) < 0)
            flux_log_error (h, "%s", __FUNCTION__);
    }
    wait_destroy (wait);
stall:
    Jput (val);
    Jput (in);
    Jput (in2);
    Jput (out);
    flux_msg_destroy (cpy);
}

typedef struct {
    const char *key;
    char *sender;
} unwatch_param_t;

static bool unwatch_cmp (const flux_msg_t *msg, void *arg)
{
    unwatch_param_t *p = arg;
    char *sender = NULL;
    json_object *o = NULL;
    json_object *val;
    const char *key, *topic, *json_str;
    int flags;
    bool match = false;

    if (flux_request_decode (msg, &topic, &json_str) < 0)
        goto done;
    if (strcmp (topic, "kvs.watch") != 0)
        goto done;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (strcmp (sender, p->sender) != 0)
        goto done;
    if (!json_str || !(o = Jfromstr (json_str)))
        goto done;
    if (kp_twatch_dec (o, &key, &val, &flags) <  0)
        goto done;
    if (strcmp (p->key, key) != 0)
        goto done;
    match = true;
done:
    if (sender)
        free (sender);
    Jput (o);
    return match;
}

static void unwatch_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;
    unwatch_param_t p = { NULL, NULL };
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!json_str || !(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_tunwatch_dec (in, &p.key) < 0)
        goto done;
    if (flux_msg_get_route_first (msg, &p.sender) < 0)
        goto done;
    if (wait_destroy_msg (ctx->watchlist, unwatch_cmp, &p) < 0)
        goto done;
    if (cache_wait_destroy_msg (ctx->cache, unwatch_cmp, &p) < 0)
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
    if (p.sender)
        free (p.sender);
}

static void fence_destroy (fence_t *f)
{
    if (f) {
        Jput (f->names);
        Jput (f->ops);
        Jput (f->rootcpy);
        if (f->requests) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (f->requests)))
                flux_msg_destroy (msg);
            /* FIXME: respond with error here? */
            zlist_destroy (&f->requests);
        }
        free (f);
    }
}

static fence_t *fence_create (kvs_ctx_t *ctx, const char *name, int nprocs,
                              int flags)
{
    fence_t *f;

    if (!(f = calloc (1, sizeof (*f))) || !(f->ops = json_object_new_array ())
                                       || !(f->requests = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    f->nprocs = nprocs;
    f->flags = flags;
    f->ctx = ctx;
    f->names = Jnew_ar ();
    Jadd_ar_str (f->names, name);
    return f;
error:
    fence_destroy (f);
    return NULL;
}

static int fence_append_ops (fence_t *f, json_object *ops)
{
    json_object *op;
    int i;

    if (ops) {
        for (i = 0; i < json_object_array_length (ops); i++) {
            if ((op = json_object_array_get_idx (ops, i)))
                if (json_object_array_add (f->ops, Jget (op)) < 0) {
                    Jput (op);
                    errno = ENOMEM;
                    return -1;
                }
        }
    }
    return 0;
}

static int fence_append_request (fence_t *f, const flux_msg_t *request)
{
    flux_msg_t *cpy = flux_msg_copy (request, false);
    if (!cpy)
        return -1;
    if (zlist_push (f->requests, cpy) < 0) {
        flux_msg_destroy (cpy);
        return -1;
    }
    return 0;
}

static int fence_add (kvs_ctx_t *ctx, fence_t *f)
{
    const char *name;

    if (!Jget_ar_str (f->names, 0, &name)) {
        errno = EINVAL;
        goto error;
    }
    if (zhash_insert (ctx->fences, name, f) < 0) {
        errno = EEXIST;
        goto error;
    }
    zhash_freefn (ctx->fences, name, (zhash_free_fn *)fence_destroy);
    return 0;
error:
    return -1;
}

static fence_t *fence_lookup (kvs_ctx_t *ctx, const char *name)
{
    return zhash_lookup (ctx->fences, name);
}

static void fence_finalize (kvs_ctx_t *ctx, fence_t *f, int errnum)
{
    flux_msg_t *msg;
    const char *name;

    while ((msg = zlist_pop (f->requests))) {
        if (flux_respond (ctx->h, msg, errnum, NULL) < 0)
            flux_log_error (ctx->h, "%s", __FUNCTION__);
        flux_msg_destroy (msg);
    }
    if (Jget_ar_str (f->names, 0, &name))
        zhash_delete (ctx->fences, name);
}

static void fence_finalize_bynames (kvs_ctx_t *ctx, json_object *names, int errnum)
{
    int i, len;
    const char *name;
    fence_t *f;

    if (!Jget_ar_len (names, &len)) {
        flux_log_error (ctx->h, "%s: parsing array", __FUNCTION__);
        return;
    }
    for (i = 0; i < len; i++) {
        if (!Jget_ar_str (names, i, &name)) {
            flux_log_error (ctx->h, "%s: parsing array[%d]", __FUNCTION__, i);
            return;
        }
        if ((f = fence_lookup (ctx, name)))
            fence_finalize (ctx, f, errnum);
    }
}

/* kvs.relayfence (rank 0 only, no response).
 */
static void relayfence_request_cb (flux_t *h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str, *name;
    int nprocs, flags;
    json_object *in = NULL;
    json_object *ops = NULL;
    fence_t *f;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (h, "%s request decode", __FUNCTION__);
        goto done;
    }
    if (!json_str
        || !(in = Jfromstr (json_str))
        || kp_tfence_dec (in, &name, &nprocs, &flags, &ops) < 0) {
        errno = EPROTO;
        flux_log_error (h, "%s payload decode", __FUNCTION__);
        goto done;
    }
    /* FIXME: generate a kvs.fence.abort (or similar) if an error
     * occurs after we know the fence name
     */
    if (!(f = fence_lookup (ctx, name))) {
        if (!(f = fence_create (ctx, name, nprocs, flags))) {
            flux_log_error (h, "%s fence_create %s", __FUNCTION__, name);
            goto done;
        }
        if (fence_add (ctx, f) < 0) {
            flux_log_error (h, "%s fence_add %s", __FUNCTION__, name);
            fence_destroy (f);
            goto done;
        }
    }
    else
        f->flags |= flags;

    if (fence_append_ops (f, ops) < 0) {
        flux_log_error (h, "%s fence_append_ops %s", __FUNCTION__, name);
        goto done;
    }
    f->count++;
    //flux_log (h, LOG_DEBUG, "%s: %s count=%d/%d",
    //          __FUNCTION__, name, f->count, f->nprocs);
    if (f->count == f->nprocs) {
        if (zlist_append (ctx->ready, f) < 0)
            oom ();
    }
done:
    Jput (in);
}

/* kvs.fence
 * Sent from users to local kvs module.
 */
static void fence_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str, *name;
    int nprocs, flags;
    json_object *in = NULL;
    json_object *ops = NULL;
    fence_t *f;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!json_str || !(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    if (kp_tfence_dec (in, &name, &nprocs, &flags, &ops) < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = fence_lookup (ctx, name))) {
        if (!(f = fence_create (ctx, name, nprocs, flags)))
            goto error;
        if (fence_add (ctx, f) < 0) {
            fence_destroy (f);
            goto error;
        }
    }
    else
        f->flags |= flags;

    if (fence_append_request (f, msg) < 0)
        goto error;
    if (ctx->rank == 0) {
        if (fence_append_ops (f, ops) < 0)
            goto error;
        f->count++;
        // flux_log (h, LOG_DEBUG, "%s: %s count=%d/%d",
        //          __FUNCTION__, name, f->count, f->nprocs);
        if (f->count == f->nprocs) {
            if (zlist_append (ctx->ready, f) < 0)
                oom ();
        }
    }
    else {
        flux_rpc_t *rpc = flux_rpc (h, "kvs.relayfence", json_str,
                                    0, FLUX_RPC_NORESPONSE);
        if (!rpc)
            goto error;
        flux_rpc_destroy (rpc);
    }
    Jput (in);
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
}


/* For wait_version().
 */
static void sync_request_cb (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int rootseq;
    wait_t *wait = NULL;

    if (flux_request_decodef (msg, NULL, "{ s:i }",
                              "rootseq", &rootseq) < 0)
        goto error;
    if (ctx->rootseq < rootseq) {
        if (!(wait = wait_create_msg_handler (h, w, msg, sync_request_cb, arg)))
            goto error;
        wait_addqueue (ctx->watchlist, wait);
        return; /* stall */
    }
    if (flux_respondf (h, msg, "{ s:i s:s }",
                       "rootseq", ctx->rootseq,
                       "rootdir", ctx->rootdir) < 0)
        goto error;
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void getroot_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respondf (h, msg, "{ s:i s:s }",
                       "rootseq", ctx->rootseq,
                       "rootdir", ctx->rootdir) < 0)
        goto error;
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static int getroot_rpc (kvs_ctx_t *ctx, int *rootseq, href_t rootdir)
{
    flux_rpc_t *rpc;
    const char *ref;
    int rc = -1;

    if (!(rpc = flux_rpc (ctx->h, "kvs.getroot", NULL,
                                             FLUX_NODEID_UPSTREAM, 0)))
        goto done;
    if (flux_rpc_getf (rpc, "{ s:i s:s }",
                       "rootseq", rootseq,
                       "rootdir", &ref) < 0)
        goto done;
    if (strlen (ref) > sizeof (href_t) - 1) {
        errno = EPROTO;
        goto done;
    }
    strcpy (rootdir, ref);
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

static void error_event_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *json_str;
    json_object *out = NULL;
    json_object *names = NULL;
    int errnum;

    if (flux_event_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        goto done;
    }
    if (!json_str || !(out = Jfromstr (json_str))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s: json_decode", __FUNCTION__);
        goto done;
    }
    if (kp_terror_dec (out, &names, &errnum) < 0) {
        flux_log_error (ctx->h, "%s: kp_terror_dec", __FUNCTION__);
        goto done;
    }
    fence_finalize_bynames (ctx, names, errnum);
done:
    Jput (out);
}

static int error_event_send (kvs_ctx_t *ctx, json_object *names, int errnum)
{
    json_object *in = NULL;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!(in = kp_terror_enc (names, errnum)))
        goto done;
    if (!(msg = flux_event_encode ("kvs.error", Jtostr (in))))
        goto done;
    if (flux_msg_set_private (msg) < 0)
        goto done;
    if (flux_send (ctx->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_msg_destroy (msg);
    return rc;
}

/* Alter the (rootdir, rootseq) in response to a setroot event.
 */
static void setroot_event_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_object *out = NULL;
    int rootseq;
    const char *json_str;
    const char *rootdir;
    json_object *root = NULL;
    json_object *names = NULL;

    if (flux_event_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        goto done;
    }
    if (!json_str || !(out = Jfromstr (json_str))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s: json decode", __FUNCTION__);
        goto done;
    }
    if (kp_tsetroot_dec (out, &rootseq, &rootdir, &root, &names) < 0) {
        flux_log_error (ctx->h, "%s: kp_tsetroot_dec", __FUNCTION__);
        goto done;
    }
    fence_finalize_bynames (ctx, names, 0);
    /* Copy of root object (corresponding to rootdir blobref) was included
     * in the setroot event as an optimization, since it would otherwise
     * be loaded from the content store on next KVS access - immediate
     * if there are watchers.  Store this object in the KVS cache
     * with clear dirty bit as it is already valid in the content store.
     */
    if (root) {
        struct cache_entry *hp;
        if ((hp = cache_lookup (ctx->cache, rootdir, ctx->epoch))) {
            if (!cache_entry_get_valid (hp))
                cache_entry_set_json (hp, Jget (root));
            if (cache_entry_get_dirty (hp))
                cache_entry_set_dirty (hp, false);
        } else {
            hp = cache_entry_create (Jget (root));
            cache_insert (ctx->cache, rootdir, hp);
        }
    }
    setroot (ctx, rootdir, rootseq);
done:
    Jput (out);
}

static int setroot_event_send (kvs_ctx_t *ctx, json_object *names)
{
    json_object *in = NULL;
    json_object *root = NULL;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (event_includes_rootdir) {
        bool stall = !load (ctx, ctx->rootdir, NULL, &root);
        FASSERT (ctx->h, stall == false);
    }
    if (!(in = kp_tsetroot_enc (ctx->rootseq, ctx->rootdir, root, names)))
        goto done;
    if (!(msg = flux_event_encode ("kvs.setroot", Jtostr (in))))
        goto done;
    if (flux_msg_set_private (msg) < 0)
        goto done;
    if (flux_send (ctx->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_msg_destroy (msg);
    return rc;
}

static bool disconnect_cmp (const flux_msg_t *msg, void *arg)
{
    char *sender = arg;
    char *s = NULL;
    bool match = false;

    if (flux_msg_get_route_first (msg, &s) == 0 && !strcmp (s, sender))
        match = true;
    if (s)
        free (s);
    return match;
}

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    char *sender = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        return;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    (void)wait_destroy_msg (ctx->watchlist, disconnect_cmp, sender);
    (void)cache_wait_destroy_msg (ctx->cache, disconnect_cmp, sender);
    free (sender);
}

static void add_tstat (json_object *o, const char *name, tstat_t *ts,
                       double scale)
{
    json_object *t = Jnew ();

    Jadd_int (t, "count", tstat_count (ts));
    Jadd_double (t, "min", tstat_min (ts)*scale);
    Jadd_double (t, "mean", tstat_mean (ts)*scale);
    Jadd_double (t, "stddev", tstat_stddev (ts)*scale);
    Jadd_double (t, "max", tstat_max (ts)*scale);

    json_object_object_add (o, name, t);
}

static void stats_get_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_object *o = Jnew ();
    tstat_t ts;
    int size, incomplete, dirty;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    memset (&ts, 0, sizeof (ts));
    cache_get_stats (ctx->cache, &ts, &size, &incomplete, &dirty);
    Jadd_double (o, "obj size total (MiB)", (double)size/1048576);
    add_tstat (o, "obj size (KiB)", &ts, 1E-3);
    Jadd_int (o, "#obj dirty", dirty);
    Jadd_int (o, "#obj incomplete", incomplete);
    Jadd_int (o, "#watchers", wait_queue_length (ctx->watchlist));
    Jadd_int (o, "#no-op stores", ctx->stats.noop_stores);
    Jadd_int (o, "#faults", ctx->stats.faults);
    Jadd_int (o, "store revision", ctx->rootseq);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (o)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (o);
}

static void stats_clear_event_cb (flux_t *h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    memset (&ctx->stats, 0, sizeof (ctx->stats));
}

static void stats_clear_request_cb (flux_t *h, flux_msg_handler_t *w,
                                    const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    memset (&ctx->stats, 0, sizeof (ctx->stats));

    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.get",        stats_get_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.clear",      stats_clear_request_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.stats.clear",      stats_clear_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.setroot",          setroot_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.error",            error_event_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",          getroot_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",        dropcache_request_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.dropcache",        dropcache_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "hb",                   heartbeat_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect",       disconnect_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.unwatch",          unwatch_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.sync",             sync_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",              get_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",            watch_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.fence",            fence_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.relayfence",       relayfence_request_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

static void process_args (kvs_ctx_t *ctx, int ac, char **av)
{
    int i;

    for (i = 0; i < ac; i++) {
        if (strncmp (av[i], "commit-merge=", 13) == 0)
            ctx->commit_merge = strtoul (av[i]+13, NULL, 10);
        else
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
    }
}

int mod_main (flux_t *h, int argc, char **argv)
{
    kvs_ctx_t *ctx = getctx (h);

    if (!ctx) {
        flux_log_error (h, "error creating KVS context");
        return -1;
    }
    process_args (ctx, argc, argv);
    if (flux_event_subscribe (h, "hb") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (flux_event_subscribe (h, "kvs.") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (ctx->rank == 0) {
        json_object *rootdir = Jnew ();
        href_t href;

        if (store (ctx, rootdir, href, NULL) < 0) {
            flux_log_error (h, "storing root object");
            return -1;
        }
        setroot (ctx, href, 0);
    } else {
        href_t href;
        int rootseq;
        if (getroot_rpc (ctx, &rootseq, href) < 0) {
            flux_log_error (h, "getroot");
            return -1;
        }
        setroot (ctx, href, rootseq);
    }
    if (flux_msg_handler_addvec (h, handlers, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        return -1;
    }
    flux_msg_handler_delvec (handlers);
    return 0;
}

MOD_NAME ("kvs");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
