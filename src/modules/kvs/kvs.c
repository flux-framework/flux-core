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
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"
#include "src/common/libkvs/jansson_dirent.h"

#include "waitqueue.h"
#include "cache.h"
#include "kvs_util.h"

#include "lookup.h"
#include "fence.h"
#include "types.h"
#include "commit.h"

#define KVS_MAGIC 0xdeadbeef

/* Expire cache_entry after 'max_lastuse_age' heartbeats.
 */
const int max_lastuse_age = 5;

/* Include root directory in kvs.setroot event.
 */
const bool event_includes_rootdir = true;

typedef struct {
    int magic;
    struct cache *cache;    /* blobref => cache_entry */
    href_t rootdir;         /* current root blobref */
    int rootseq;            /* current root version (for ordering) */
    commit_mgr_t *cm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int faults;                 /* for kvs.stats.get, etc. */
    flux_t *h;
    uint32_t rank;
    int epoch;              /* tracks current heartbeat epoch */
    flux_watcher_t *prep_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int commit_merge;
    const char *hash_name;
} kvs_ctx_t;

static int setroot_event_send (kvs_ctx_t *ctx, json_t *names);
static int error_event_send (kvs_ctx_t *ctx, json_t *names, int errnum);
static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg);
static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg);

static void freectx (void *arg)
{
    kvs_ctx_t *ctx = arg;
    if (ctx) {
        cache_destroy (ctx->cache);
        commit_mgr_destroy (ctx->cm);
        if (ctx->watchlist)
            wait_queue_destroy (ctx->watchlist);
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
    int saved_errno;

    if (!ctx) {
        if (!(ctx = calloc (1, sizeof (*ctx)))) {
            saved_errno = ENOMEM;
            goto error;
        }
        ctx->magic = KVS_MAGIC;
        if (!(r = flux_get_reactor (h))) {
            saved_errno = errno;
            goto error;
        }
        if (!(ctx->hash_name = flux_attr_get (h, "content.hash", NULL))) {
            saved_errno = errno;
            flux_log_error (h, "content.hash");
            goto error;
        }
        ctx->cache = cache_create ();
        ctx->watchlist = wait_queue_create ();
        ctx->cm = commit_mgr_create (ctx->cache, ctx->hash_name, ctx);
        if (!ctx->cache || !ctx->watchlist || !ctx->cm) {
            saved_errno = ENOMEM;
            goto error;
        }
        ctx->h = h;
        if (flux_get_rank (h, &ctx->rank) < 0) {
            saved_errno = errno;
            goto error;
        }
        if (ctx->rank == 0) {
            ctx->prep_w = flux_prepare_watcher_create (r, commit_prep_cb, ctx);
            if (!ctx->prep_w) {
                saved_errno = errno;
                goto error;
            }
            ctx->check_w = flux_check_watcher_create (r, commit_check_cb, ctx);
            if (!ctx->check_w) {
                saved_errno = errno;
                goto error;
            }
            ctx->idle_w = flux_idle_watcher_create (r, NULL, NULL);
            if (!ctx->idle_w) {
                saved_errno = errno;
                goto error;
            }
            flux_watcher_start (ctx->prep_w);
            flux_watcher_start (ctx->check_w);
        }
        ctx->commit_merge = 1;
        flux_aux_set (h, "kvssrv", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    errno = saved_errno;
    return NULL;
}

static int content_load_get (flux_future_t *f, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_t *o;
    void *data;
    int size;
    const char *blobref;
    struct cache_entry *hp;
    int saved_errno, rc = -1;

    if (flux_rpc_get_raw (f, &data, &size) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_rpc_get_raw", __FUNCTION__);
        goto done;
    }
    blobref = flux_future_aux_get (f, "ref");
    if (!(o = json_loads ((char *)data, JSON_DECODE_ANY, NULL))) {
        saved_errno = EPROTO;
        flux_log_error (ctx->h, "%s: json_loads", __FUNCTION__);
        goto done;
    }
    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it is not yet valid.  But check and log incase there is
     * logic error dealng with error paths using cache_remove_entry().
     */
    if (!(hp = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
        saved_errno = ENOTRECOVERABLE;
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto done;
    }

    /* This is a pretty terrible error case, where we've loaded an
     * object from the content store, but can't put it in the cache.
     *
     * If there was a waiter on this cache entry waiting for it to be
     * valid, the load() will ultimately hang.  The caller will
     * timeout or eventually give up, so the KVS can continue along
     * its merry way.  So we just log this error.
     *
     * If this is the result of a synchronous call to load(), there
     * should be no waiters on this cache entry.  load() will handle
     * this error scenario appropriately.
     */
    if (cache_entry_set_json (hp, o) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: cache_entry_set_json", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void content_load_completion (flux_future_t *f, void *arg)
{
    (void)content_load_get (f, arg);
}

/* If now is true, perform the load rpc synchronously;
 * otherwise arrange for a continuation to handle the response.
 */
static int content_load_request_send (kvs_ctx_t *ctx, const href_t ref, bool now)
{
    flux_future_t *f = NULL;
    char *refcpy;
    int saved_errno;

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    if (!(f = flux_rpc_raw (ctx->h, "content.load",
                    ref, strlen (ref) + 1, FLUX_NODEID_ANY, 0)))
        return -1;
    if (!(refcpy = strdup (ref))) {
        flux_future_destroy (f);
        errno = ENOMEM;
        return -1;
    }
    if (flux_future_aux_set (f, "ref", refcpy, free) < 0) {
        saved_errno = errno;
        free (refcpy);
        flux_future_destroy (f);
        errno = saved_errno;
        return -1;
    }
    if (now) {
        if (content_load_get (f, ctx) < 0) {
            flux_log_error (ctx->h, "%s: content_load_get", __FUNCTION__);
            return -1;
        }
    } else if (flux_future_then (f, -1., content_load_completion, ctx) < 0) {
        saved_errno = errno;
        flux_future_destroy (f);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

/* Return 0 on success, -1 on error.  Set stall variable appropriately */
static int load (kvs_ctx_t *ctx, const href_t ref, wait_t *wait, json_t **op,
                 bool *stall)
{
    struct cache_entry *hp = cache_lookup (ctx->cache, ref, ctx->epoch);
    int saved_errno, ret;

    /* Create an incomplete hash entry if none found.
     */
    if (!hp) {
        if (!(hp = cache_entry_create (NULL))) {
            flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
            return -1;
        }
        cache_insert (ctx->cache, ref, hp);
        if (content_load_request_send (ctx, ref, wait ? false : true) < 0) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: content_load_request_send",
                            __FUNCTION__);
            /* cache entry just created, should always work */
            ret = cache_remove_entry (ctx->cache, ref);
            assert (ret == 1);
            errno = saved_errno;
            return -1;
        }
        ctx->faults++;
    }
    /* If hash entry is incomplete (either created above or earlier),
     * arrange to stall caller if wait_t was provided.
     */
    if (!cache_entry_get_valid (hp)) {
        if (!wait) {
            flux_log (ctx->h, LOG_ERR, "synchronous load(), invalid cache entry");
            errno = ENODATA; /* better errno than this? */
            return -1;
        }
        if (cache_entry_wait_valid (hp, wait) < 0) {
            /* no cleanup in this path, if an rpc was sent, it will
             * complete, but not call a waiter on this load.  Return
             * error so caller can handle error appropriately.
             */
            flux_log_error (ctx->h, "cache_entry_wait_valid");
            return -1;
        }
        if (stall)
            *stall = true;
        return 0;
    }

    if (op)
        *op = cache_entry_get_json (hp);
    if (stall)
        *stall = false;
    return 0;
}

static int content_store_get (flux_future_t *f, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct cache_entry *hp;
    const char *blobref;
    int blobref_size;
    int rc = -1;
    int saved_errno, ret;

    if (flux_rpc_get_raw (f, (void **)&blobref, &blobref_size) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_rpc_get_raw", __FUNCTION__);
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        saved_errno = EPROTO;
        flux_log_error (ctx->h, "%s: invalid blobref", __FUNCTION__);
        goto done;
    }
    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it was dirty.  But check and log incase there is logic
     * error dealng with error paths using cache_remove_entry().
     */
    if (!(hp = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
        saved_errno = ENOTRECOVERABLE;
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto done;
    }

    /* This is a pretty terrible error case, where we've received
     * verification that a dirty cache entry has been flushed to the
     * content store, but we can't notify waiters that it has been
     * flushed.  We also can't notify waiters of an error occurring.
     *
     * If a commit has hung, the most likely scenario is that that
     * commiter will timeout or give up at some point.  setroot() will
     * never happen, so the entire commit has failed and no
     * consistency issue will occur.
     *
     * We'll mark the cache entry not dirty, so that memory can be
     * reclaimed at a later time.  But we can't do that with
     * cache_entry_clear_dirty() b/c that will only clear dirty for
     * entries without waiters.  So in this rare case, we must call
     * cache_entry_force_clear_dirty().
     */
    if (cache_entry_set_dirty (hp, false) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: cache_entry_set_dirty",
                        __FUNCTION__);
        ret = cache_entry_force_clear_dirty (hp);
        assert (ret == 0);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void content_store_completion (flux_future_t *f, void *arg)
{
    (void)content_store_get (f, arg);
}

static int content_store_request_send (kvs_ctx_t *ctx, json_t *val,
                                       bool now)
{
    flux_future_t *f;
    char *data = NULL;
    int size;
    int saved_errno, rc = -1;

    if (!(data = kvs_util_json_dumps (val)))
        goto error;

    size = strlen (data) + 1;

    if (!(f = flux_rpc_raw (ctx->h, "content.store",
                            data, size, FLUX_NODEID_ANY, 0)))
        goto error;
    if (now) {
        if (content_store_get (f, ctx) < 0)
            goto error;
    } else if (flux_future_then (f, -1., content_store_completion, ctx) < 0) {
        saved_errno = errno;
        flux_future_destroy (f);
        errno = saved_errno;
        goto error;
    }

    rc = 0;
error:
    free (data);
    return rc;
}

static void setroot (kvs_ctx_t *ctx, const char *rootdir, int rootseq)
{
    if (rootseq == 0 || rootseq > ctx->rootseq) {
        assert (strlen (rootdir) < sizeof (href_t));
        strcpy (ctx->rootdir, rootdir);
        ctx->rootseq = rootseq;
        /* log error on wait_runqueue(), don't error out.  watchers
         * may miss value change, but will never get older one.
         * Maintains consistency model */
        if (wait_runqueue (ctx->watchlist) < 0)
            flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
}

struct commit_cb_data {
    kvs_ctx_t *ctx;
    wait_t *wait;
    int errnum;
};

static int commit_load_cb (commit_t *c, const char *ref, void *data)
{
    struct commit_cb_data *cbd = data;
    bool stall;

    if (load (cbd->ctx, ref, cbd->wait, NULL, &stall) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "%s: load", __FUNCTION__);
        return -1;
    }
    /* if not stalling, logic issue within code */
    assert (stall);
    return 0;
}

/* Flush to content cache asynchronously and push wait onto cache
 * object's wait queue.  FIXME: asynchronous errors need to be
 * propagated back to caller.
 */
static int commit_cache_cb (commit_t *c, struct cache_entry *hp, void *data)
{
    struct commit_cb_data *cbd = data;

    assert (cache_entry_get_dirty (hp));

    if (content_store_request_send (cbd->ctx,
                                    cache_entry_get_json (hp),
                                    false) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "%s: content_store_request_send",
                        __FUNCTION__);
        commit_cleanup_dirty_cache_entry (c, hp);
        return -1;
    }
    if (cache_entry_wait_notdirty (hp, cbd->wait) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "cache_entry_wait_notdirty");
        commit_cleanup_dirty_cache_entry (c, hp);
        return -1;
    }
    return 0;
}

/* Commit all the ops for a particular commit/fence request (rank 0 only).
 * The setroot event will cause responses to be sent to the fence requests
 * and clean up the fence_t state.  This function is idempotent.
 */
static void commit_apply (commit_t *c)
{
    kvs_ctx_t *ctx = commit_get_aux (c);
    wait_t *wait = NULL;
    int errnum = 0;
    commit_process_t ret;

    if (commit_get_aux_errnum (c))
        goto done;

    if ((ret = commit_process (c,
                               ctx->epoch,
                               ctx->rootdir)) == COMMIT_PROCESS_ERROR) {
        errnum = commit_get_errnum (c);
        goto done;
    }

    if (ret == COMMIT_PROCESS_LOAD_MISSING_REFS) {
        struct commit_cb_data cbd;

        if (!(wait = wait_create ((wait_cb_f)commit_apply, c))) {
            errnum = errno;
            goto done;
        }

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (commit_iter_missing_refs (c, commit_load_cb, &cbd) < 0) {
            errnum = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                commit_set_aux_errnum (c, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
        goto stall;
    }
    else if (ret == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES) {
        struct commit_cb_data cbd;

        if (!(wait = wait_create ((wait_cb_f)commit_apply, c))) {
            errnum = errno;
            goto done;
        }

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (commit_iter_dirty_cache_entries (c, commit_cache_cb, &cbd) < 0) {
            errnum = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                commit_set_aux_errnum (c, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
        goto stall;
    }
    /* else ret == COMMIT_PROCESS_FINISHED */

    /* This is the transaction that finalizes the commit by replacing
     * ctx->rootdir with newroot, incrementing the root seq,
     * and sending out the setroot event for "eventual consistency"
     * of other nodes.
     */
done:
    if (errnum == 0) {
        fence_t *f = commit_get_fence (c);
        int count;
        if ((count = json_array_size (fence_get_json_names (f))) > 1) {
            int opcount = 0;
            opcount = json_array_size (fence_get_json_ops (f));
            flux_log (ctx->h, LOG_DEBUG, "aggregated %d commits (%d ops)",
                      count, opcount);
        }
        setroot (ctx, commit_get_newroot_ref (c), ctx->rootseq + 1);
        setroot_event_send (ctx, fence_get_json_names (f));
    } else {
        fence_t *f = commit_get_fence (c);
        flux_log (ctx->h, LOG_ERR, "commit failed: %s",
                  flux_strerror (errnum));
        error_event_send (ctx, fence_get_json_names (f), errnum);
    }
    wait_destroy (wait);

    /* Completed: remove from 'ready' list.
     * N.B. fence_t remains in the fences hash until event is received.
     */
    commit_mgr_remove_commit (ctx->cm, c);
    return;

stall:
    return;
}

static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (commit_mgr_commits_ready (ctx->cm))
        flux_watcher_start (ctx->idle_w);
}

static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    commit_t *c;

    flux_watcher_stop (ctx->idle_w);

    if ((c = commit_mgr_get_ready_commit (ctx->cm))) {
        if (ctx->commit_merge) {
            /* if merge fails, set errnum in commit_t, let
             * commit_apply() handle error handling.
             */
            if (commit_mgr_merge_ready_commits (ctx->cm) < 0)
                commit_set_aux_errnum (c, errno);
        }
        commit_apply (c);
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
    if ((expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0)) < 0) {
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
        goto done;
    }
    else
        flux_log (h, LOG_ALERT, "dropped %d of %d cache entries",
                  expcount, size);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void dropcache_event_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int size, expcount = 0;

    if (flux_event_decode (msg, NULL, NULL) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        return;
    }
    size = cache_count_entries (ctx->cache);
    if ((expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0)) < 0)
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
    else
        flux_log (h, LOG_ALERT, "dropped %d of %d cache entries",
                  expcount, size);
}

static void heartbeat_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log_error (ctx->h, "%s: flux_heartbeat_decode", __FUNCTION__);
        return;
    }
    /* "touch" objects involved in watched keys */
    if (ctx->epoch - ctx->watchlist_lastrun_epoch > max_lastuse_age) {
        /* log error on wait_runqueue(), don't error out.  watchers
         * may miss value change, but will never get older one.
         * Maintains consistency model */
        if (wait_runqueue (ctx->watchlist) < 0)
            flux_log_error (h, "%s: wait_runqueue", __FUNCTION__);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
    /* "touch" root */
    if (load (ctx, ctx->rootdir, NULL, NULL, NULL) < 0)
        flux_log_error (ctx->h, "%s: load", __FUNCTION__);

    if (cache_expire_entries (ctx->cache, ctx->epoch, max_lastuse_age) < 0)
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
}

static void get_request_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = NULL;
    int flags;
    const char *key;
    json_t *val = NULL;
    json_t *root_dirent = NULL;
    json_t *tmp_dirent = NULL;
    lookup_t *lh = NULL;
    json_t *root_ref = NULL;
    wait_t *wait = NULL;
    int rc = -1;
    int ret;

    /* if bad lh, then first time rpc and not a replay */
    if (lookup_validate (arg) == false) {
        ctx = arg;

        if (flux_request_unpack (msg, NULL, "{ s:s s:i }",
                                 "key", &key,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        /* rootdir is optional */
        (void)flux_request_unpack (msg, NULL, "{ s:o }",
                                   "rootdir", &root_dirent);

        /* If root dirent was specified, lookup corresponding 'root' directory.
         * Otherwise, use the current root.
         */
        if (root_dirent) {
            if (j_dirent_validate (root_dirent) < 0
                || !(root_ref = json_object_get (root_dirent, "DIRREF"))) {
                errno = EINVAL;
                goto done;
            }
        }

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->epoch,
                                  ctx->rootdir,
                                  json_string_value (root_ref),
                                  key,
                                  flags)))
            goto done;

        ret = lookup_set_aux_data (lh, ctx);
        assert (ret == 0);
    }
    else {
        lh = arg;

        ctx = lookup_get_aux_data (lh);
        assert (ctx);

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);
    }

    if (!lookup (lh)) {
        const char *missing_ref;
        bool stall;

        missing_ref = lookup_get_missing_ref (lh);
        assert (missing_ref);

        if (!(wait = wait_create_msg_handler (h, w, msg, get_request_cb, lh)))
            goto done;
        if (load (ctx, missing_ref, wait, NULL, &stall) < 0) {
            flux_log_error (h, "%s: load", __FUNCTION__);
            goto done;
        }
        /* if not stalling, logic issue within code */
        assert (stall);
        goto stall;
    }
    if (lookup_get_errnum (lh) != 0) {
        errno = lookup_get_errnum (lh);
        goto done;
    }
    if ((val = lookup_get_value (lh)) == NULL) {
        errno = ENOENT;
        goto done;
    }

    if (!root_dirent) {
        char *tmprootref = (char *)lookup_get_root_ref (lh);
        if (!(tmp_dirent = j_dirent_create ("DIRREF", tmprootref))) {
            flux_log_error (h, "%s: j_dirent_create", __FUNCTION__);
            goto done;
        }
        root_dirent = tmp_dirent;
    }

    if (flux_respond_pack (h, msg, "{ s:O s:O }",
                           "rootdir", root_dirent,
                           "val", val) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto done;
    }

    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    wait_destroy (wait);
    lookup_destroy (lh);
stall:
    json_decref (tmp_dirent);
    json_decref (val);
}

static void watch_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = NULL;
    json_t *oval = NULL;
    json_t *val = NULL;
    flux_msg_t *cpy = NULL;
    const char *key;
    int flags;
    lookup_t *lh = NULL;
    wait_t *wait = NULL;
    wait_t *watcher = NULL;
    bool isreplay = false;
    bool out = false;
    int rc = -1;
    int saved_errno, ret;

    /* if bad lh, then first time rpc and not a replay */
    if (lookup_validate (arg) == false) {
        ctx = arg;

        if (flux_request_unpack (msg, NULL, "{ s:s s:o s:i }",
                                 "key", &key,
                                 "val", &oval,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->epoch,
                                  ctx->rootdir,
                                  NULL,
                                  key,
                                  flags)))
            goto done;

        ret = lookup_set_aux_data (lh, ctx);
        assert (ret == 0);
    }
    else {
        lh = arg;

        ctx = lookup_get_aux_data (lh);
        assert (ctx);

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);

        isreplay = true;
    }

    if (!lookup (lh)) {
        const char *missing_ref;
        bool stall;

        missing_ref = lookup_get_missing_ref (lh);
        assert (missing_ref);

        if (!(wait = wait_create_msg_handler (h, w, msg, watch_request_cb, lh)))
            goto done;
        if (load (ctx, missing_ref, wait, NULL, &stall) < 0) {
            flux_log_error (h, "%s: load", __FUNCTION__);
            goto done;
        }
        /* if not stalling, logic issue within code */
        assert (stall);
        goto stall;
    }
    if (lookup_get_errnum (lh) != 0) {
        errno = lookup_get_errnum (lh);
        goto done;
    }
    val = lookup_get_value (lh);

    /* if no value, create json null object for remainder of code */
    if (!val) {
        if (!(val = json_null ())) {
            errno = ENOMEM;
            goto done;
        }
    }

    /* we didn't initialize these values on a replay, get them */
    if (isreplay) {
        if (flux_request_unpack (msg, NULL, "{ s:s s:o s:i }",
                                 "key", &key,
                                 "val", &oval,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }
    }

    /* Value changed or this is the initial request, so there will be
     * a reply.
     */
    if ((flags & KVS_WATCH_FIRST) || !json_equal (val, oval))
        out = true;

    /* No reply sent or this is a multi-response watch request.
     * Arrange to wait on ctx->watchlist for each new commit.
     * Reconstruct the payload with 'first' flag clear, and updated value.
     */
    if (!out || !(flags & KVS_WATCH_ONCE)) {
        if (!(cpy = flux_msg_copy (msg, false)))
            goto done;

        if (flux_msg_pack (cpy, "{ s:s s:O s:i }",
                           "key", key,
                           "val", val,
                           "flags", flags & ~KVS_WATCH_FIRST) < 0) {
            flux_log_error (h, "%s: flux_msg_pack", __FUNCTION__);
            goto done;
        }
        if (!(watcher = wait_create_msg_handler (h, w, cpy,
                                                 watch_request_cb, ctx)))
            goto done;
        if (wait_addqueue (ctx->watchlist, watcher) < 0) {
            saved_errno = errno;
            wait_destroy (watcher);
            errno = saved_errno;
            goto done;
        }
    }

    if (out) {
        if (flux_respond_pack (h, msg, "{ s:O }", "val", val) < 0) {
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
            goto done;
        }
    }
    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    wait_destroy (wait);
    lookup_destroy (lh);
stall:
    flux_msg_destroy (cpy);
    json_decref (val);
}

typedef struct {
    const char *key;
    char *sender;
} unwatch_param_t;

static bool unwatch_cmp (const flux_msg_t *msg, void *arg)
{
    unwatch_param_t *p = arg;
    char *sender = NULL;
    json_t *val;
    const char *key, *topic;
    int flags;
    bool match = false;

    if (flux_request_unpack (msg, &topic, "{ s:s s:o s:i }",
                             "key", &key,
                             "val", &val,
                             "flags", &flags) < 0)
        goto done;
    if (strcmp (topic, "kvs.watch") != 0)
        goto done;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (strcmp (sender, p->sender) != 0)
        goto done;
    if (strcmp (p->key, key) != 0)
        goto done;
    match = true;
done:
    if (sender)
        free (sender);
    return match;
}

static void unwatch_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    unwatch_param_t p = { NULL, NULL };
    int rc = -1;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "key", &p.key) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto done;
    }
    if (flux_msg_get_route_first (msg, &p.sender) < 0)
        goto done;
    /* N.B. impossible for a watch to be on watchlist and cache waiter
     * at the same time (i.e. on watchlist means we're watching, if on
     * cache waiter we're not done processing towards being on the
     * watchlist).  So if wait_destroy_msg() on the waitlist succeeds
     * but cache_wait_destroy_msg() fails, it's not that big of a
     * deal.  The current state is still maintained.
     */
    if (wait_destroy_msg (ctx->watchlist, unwatch_cmp, &p) < 0) {
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
        goto done;
    }
    if (cache_wait_destroy_msg (ctx->cache, unwatch_cmp, &p) < 0) {
        flux_log_error (h, "%s: cache_wait_destroy_msg", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    if (p.sender)
        free (p.sender);
}

struct finalize_data {
    kvs_ctx_t *ctx;
    int errnum;
};

static int finalize_fence_req (fence_t *f, const flux_msg_t *req, void *data)
{
    struct finalize_data *d = data;

    if (flux_respond (d->ctx->h, req, d->errnum, NULL) < 0)
        flux_log_error (d->ctx->h, "%s: flux_respond", __FUNCTION__);

    return 0;
}

static void finalize_fences_bynames (kvs_ctx_t *ctx, json_t *names, int errnum)
{
    int i, len;
    json_t *name;
    fence_t *f;
    struct finalize_data d = { .ctx = ctx, .errnum = errnum };

    if (!(len = json_array_size (names))) {
        flux_log_error (ctx->h, "%s: parsing array", __FUNCTION__);
        return;
    }
    for (i = 0; i < len; i++) {
        if (!(name = json_array_get (names, i))) {
            flux_log_error (ctx->h, "%s: parsing array[%d]", __FUNCTION__, i);
            return;
        }
        if ((f = commit_mgr_lookup_fence (ctx->cm, json_string_value (name)))) {
            fence_iter_request_copies (f, finalize_fence_req, &d);
            commit_mgr_remove_fence (ctx->cm, json_string_value (name));
        }
    }
}

/* kvs.relayfence (rank 0 only, no response).
 */
static void relayfence_request_cb (flux_t *h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *name;
    int nprocs, flags;
    json_t *ops = NULL;
    fence_t *f;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }
    /* FIXME: generate a kvs.fence.abort (or similar) if an error
     * occurs after we know the fence name
     */
    if (!(f = commit_mgr_lookup_fence (ctx->cm, name))) {
        if (!(f = fence_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: fence_create", __FUNCTION__);
            return;
        }
        if (commit_mgr_add_fence (ctx->cm, f) < 0) {
            flux_log_error (h, "%s: commit_mgr_add_fence", __FUNCTION__);
            fence_destroy (f);
            return;
        }
    }
    else
        fence_set_flags (f, fence_get_flags (f) | flags);

    if (fence_add_request_data (f, ops) < 0) {
        flux_log_error (h, "%s: fence_add_request_data", __FUNCTION__);
        return;
    }

    if (commit_mgr_process_fence_request (ctx->cm, f) < 0) {
        flux_log_error (h, "%s: commit_mgr_process_fence_request", __FUNCTION__);
        return;
    }

    return;
}

/* kvs.fence
 * Sent from users to local kvs module.
 */
static void fence_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *name;
    int saved_errno, nprocs, flags;
    json_t *ops = NULL;
    fence_t *f;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (!(f = commit_mgr_lookup_fence (ctx->cm, name))) {
        if (!(f = fence_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: fence_create", __FUNCTION__);
            goto error;
        }
        if (commit_mgr_add_fence (ctx->cm, f) < 0) {
            saved_errno = errno;
            flux_log_error (h, "%s: commit_mgr_add_fence", __FUNCTION__);
            fence_destroy (f);
            errno = saved_errno;
            goto error;
        }
    }
    else
        fence_set_flags (f, fence_get_flags (f) | flags);

    if (fence_add_request_copy (f, msg) < 0)
        goto error;
    if (ctx->rank == 0) {
        if (fence_add_request_data (f, ops) < 0) {
            flux_log_error (h, "%s: fence_add_request_data", __FUNCTION__);
            goto error;
        }

        if (commit_mgr_process_fence_request (ctx->cm, f) < 0) {
            flux_log_error (h, "%s: commit_mgr_process_fence_request",
                            __FUNCTION__);
            goto error;
        }
    }
    else {
        flux_future_t *f;

        if (!(f = flux_rpc_pack (h, "kvs.relayfence", 0, FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:i s:i }",
                                 "ops", ops,
                                 "name", name,
                                 "flags", flags,
                                 "nprocs", nprocs))) {
            flux_log_error (h, "%s: flux_rpc_pack", __FUNCTION__);
            goto error;
        }
        flux_future_destroy (f);
    }
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}


/* For wait_version().
 */
static void sync_request_cb (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int saved_errno, rootseq;
    wait_t *wait = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:i }",
                             "rootseq", &rootseq) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (ctx->rootseq < rootseq) {
        if (!(wait = wait_create_msg_handler (h, w, msg, sync_request_cb, arg)))
            goto error;
        if (wait_addqueue (ctx->watchlist, wait) < 0) {
            saved_errno = errno;
            wait_destroy (wait);
            errno = saved_errno;
            goto error;
        }
        return; /* stall */
    }
    if (flux_respond_pack (h, msg, "{ s:i s:s }",
                           "rootseq", ctx->rootseq,
                           "rootdir", ctx->rootdir) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void getroot_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{ s:i s:s }",
                           "rootseq", ctx->rootseq,
                           "rootdir", ctx->rootdir) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static int getroot_rpc (kvs_ctx_t *ctx, int *rootseq, href_t rootdir)
{
    flux_future_t *f;
    const char *ref;
    int saved_errno, rc = -1;

    if (!(f = flux_rpc (ctx->h, "kvs.getroot", NULL, FLUX_NODEID_UPSTREAM, 0))) {
        saved_errno = errno;
        goto done;
    }
    if (flux_rpc_get_unpack (f, "{ s:i s:s }",
                             "rootseq", rootseq,
                             "rootdir", &ref) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto done;
    }
    if (strlen (ref) > sizeof (href_t) - 1) {
        saved_errno = EPROTO;
        goto done;
    }
    strcpy (rootdir, ref);
    rc = 0;
done:
    flux_future_destroy (f);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void error_event_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_t *names = NULL;
    int errnum;

    if (flux_event_unpack (msg, NULL, "{ s:o s:i }",
                           "names", &names,
                           "errnum", &errnum) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }
    finalize_fences_bynames (ctx, names, errnum);
}

static int error_event_send (kvs_ctx_t *ctx, json_t *names, int errnum)
{
    flux_msg_t *msg = NULL;
    int saved_errno, rc = -1;

    if (!(msg = flux_event_pack ("kvs.error", "{ s:O s:i }",
                                 "names", names,
                                 "errnum", errnum))) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_event_pack", __FUNCTION__);
        goto done;
    }
    if (flux_msg_set_private (msg) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (flux_send (ctx->h, msg, 0) < 0) {
        saved_errno = errno;
        goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

/* Alter the (rootdir, rootseq) in response to a setroot event.
 */
static void setroot_event_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int rootseq;
    const char *rootdir;
    json_t *root = NULL;
    json_t *names = NULL;

    if (flux_event_unpack (msg, NULL, "{ s:i s:s s:o s:o }",
                           "rootseq", &rootseq,
                           "rootdir", &rootdir,
                           "names", &names,
                           "rootdirval", &root) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    finalize_fences_bynames (ctx, names, 0);
    /* Copy of root object (corresponding to rootdir blobref) was included
     * in the setroot event as an optimization, since it would otherwise
     * be loaded from the content store on next KVS access - immediate
     * if there are watchers.  Store this object in the KVS cache
     * with clear dirty bit as it is already valid in the content store.
     */
    if (!json_is_null (root)) {
        struct cache_entry *hp;
        if ((hp = cache_lookup (ctx->cache, rootdir, ctx->epoch))) {
            if (!cache_entry_get_valid (hp)) {
                /* On error, bad that we can't cache new root, but
                 * no consistency issue by not caching.  We will still
                 * set new root below via setroot().
                 */
                if (cache_entry_set_json (hp, json_incref (root)) < 0) {
                    flux_log_error (ctx->h, "%s: cache_entry_set_json",
                                    __FUNCTION__);
                    json_decref (root);
                }
            }
            if (cache_entry_get_dirty (hp)) {
                /* If it was dirty, an RPC is already in process, so
                 * let that RPC handle any error handling with this
                 * cache entry, we just log this error.
                 */
                if (cache_entry_set_dirty (hp, false) < 0)
                    flux_log_error (ctx->h, "%s: cache_entry_set_dirty",
                                    __FUNCTION__);
            }
        } else {
            /* On error, bad that we can't cache new root, but
             * no consistency issue by not caching.  We will still
             * set new root below via setroot().
             */
            if (!(hp = cache_entry_create (json_incref (root)))) {
                flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
                json_decref (root);
                return;
            }
            cache_insert (ctx->cache, rootdir, hp);
        }
    }
    setroot (ctx, rootdir, rootseq);
}

static int setroot_event_send (kvs_ctx_t *ctx, json_t *names)
{
    json_t *root = NULL;
    json_t *nullobj = NULL;
    flux_msg_t *msg = NULL;
    int saved_errno, rc = -1;

    if (event_includes_rootdir) {
        bool stall;
        if (load (ctx, ctx->rootdir, NULL, &root, &stall) < 0) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: load", __FUNCTION__);
            goto done;
        }
        FASSERT (ctx->h, stall == false);
    }
    else {
        if (!(nullobj = json_null ())) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: json_null", __FUNCTION__);
            goto done;
        }
        root = nullobj;
    }
    if (!(msg = flux_event_pack ("kvs.setroot", "{ s:i s:s s:O s:O }",
                                 "rootseq", ctx->rootseq,
                                 "rootdir", ctx->rootdir,
                                 "names", names,
                                 "rootdirval", root))) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_event_pack", __FUNCTION__);
        goto done;
    }
    if (flux_msg_set_private (msg) < 0) {
        saved_errno = errno;
        goto done;
    }
    if (flux_send (ctx->h, msg, 0) < 0) {
        saved_errno = errno;
        goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    json_decref (nullobj);
    if (rc < 0)
        errno = saved_errno;
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
    /* N.B. impossible for a watch to be on watchlist and cache waiter
     * at the same time (i.e. on watchlist means we're watching, if on
     * cache waiter we're not done processing towards being on the
     * watchlist).  So if wait_destroy_msg() on the waitlist succeeds
     * but cache_wait_destroy_msg() fails, it's not that big of a
     * deal.  The current state is still maintained.
     */
    if (wait_destroy_msg (ctx->watchlist, disconnect_cmp, sender) < 0)
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
    if (cache_wait_destroy_msg (ctx->cache, disconnect_cmp, sender) < 0)
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
    free (sender);
}

static void stats_get_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_t *t = NULL;
    tstat_t ts;
    int size, incomplete, dirty;
    int rc = -1;
    double scale = 1E-3;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;

    memset (&ts, 0, sizeof (ts));
    if (cache_get_stats (ctx->cache, &ts, &size, &incomplete, &dirty) < 0)
        goto done;

    if (!(t = json_pack ("{ s:i s:f s:f s:f s:f }",
                         "count", tstat_count (&ts),
                         "min", tstat_min (&ts)*scale,
                         "mean", tstat_mean (&ts)*scale,
                         "stddev", tstat_stddev (&ts)*scale,
                         "max", tstat_max (&ts)*scale))) {
        errno = ENOMEM;
        goto done;
    }

    if (flux_respond_pack (h, msg,
                           "{ s:f s:O s:i s:i s:i s:i s:i s:i }",
                           "obj size total (MiB)", (double)size/1048576,
                           "obj size (KiB)", t,
                           "#obj dirty", dirty,
                           "#obj incomplete", incomplete,
                           "#watchers", wait_queue_length (ctx->watchlist),
                           "#no-op stores", commit_mgr_get_noop_stores (ctx->cm),
                           "#faults", ctx->faults,
                           "store revision", ctx->rootseq) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto done;
    }

    rc = 0;
 done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    json_decref (t);
}

static void stats_clear (kvs_ctx_t *ctx)
{
    ctx->faults = 0;
    commit_mgr_clear_noop_stores (ctx->cm);
}

static void stats_clear_event_cb (flux_t *h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    stats_clear (ctx);
}

static void stats_clear_request_cb (flux_t *h, flux_msg_handler_t *w,
                                    const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    stats_clear (ctx);

    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.get",  stats_get_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.clear",stats_clear_request_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.stats.clear",stats_clear_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.setroot",    setroot_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.error",      error_event_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",    getroot_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",  dropcache_request_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "kvs.dropcache",  dropcache_event_cb, 0, NULL },
    { FLUX_MSGTYPE_EVENT,   "hb",             heartbeat_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect", disconnect_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.unwatch",    unwatch_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.sync",       sync_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",        get_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",      watch_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.fence",      fence_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "kvs.relayfence", relayfence_request_cb, 0, NULL },
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

/* Store initial rootdir in local cache, and flush to content
 * cache synchronously.
 * Object reference is given to this function, it will either give it
 * to the cache or decref it.
 */
static int store_initial_rootdir (kvs_ctx_t *ctx, json_t *o, href_t ref)
{
    struct cache_entry *hp;
    int rc = -1;
    int saved_errno, ret;

    if (kvs_util_json_hash (ctx->hash_name, o, ref) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: kvs_util_json_hash",
                        __FUNCTION__);
        goto decref_done;
    }
    if (!(hp = cache_lookup (ctx->cache, ref, ctx->epoch))) {
        if (!(hp = cache_entry_create (NULL))) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
            goto decref_done;
        }
        cache_insert (ctx->cache, ref, hp);
    }
    if (!cache_entry_get_valid (hp)) {
        if (cache_entry_set_json (hp, o) < 0) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: cache_entry_set_json",
                            __FUNCTION__);
            ret = cache_remove_entry (ctx->cache, ref);
            assert (ret == 1);
            goto decref_done;
        }
        if (cache_entry_set_dirty (hp, true) < 0) {
            /* remove entry will decref object */
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: cache_entry_set_dirty", __FUNCTION__);
            ret = cache_remove_entry (ctx->cache, ref);
            assert (ret == 1);
            goto done_error;
        }
        if (content_store_request_send (ctx, o, true) < 0) {
            /* Must clean up, don't want cache entry to be assumed
             * valid.  Everything here is synchronous and w/o waiters,
             * so nothing should error here */
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: content_store_request_send",
                            __FUNCTION__);
            ret = cache_entry_clear_dirty (hp);
            assert (ret == 0);
            ret = cache_remove_entry (ctx->cache, ref);
            assert (ret == 1);
            goto done_error;
        }
    } else
        json_decref (o);
    rc = 0;
    return rc;

decref_done:
    json_decref (o);
done_error:
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    kvs_ctx_t *ctx = getctx (h);
    int rc = -1;

    if (!ctx) {
        flux_log_error (h, "error creating KVS context");
        goto done;
    }
    process_args (ctx, argc, argv);
    if (flux_event_subscribe (h, "hb") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_event_subscribe (h, "kvs.") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (ctx->rank == 0) {
        json_t *rootdir;
        href_t href;

        if (!(rootdir = json_object ())) {
            flux_log_error (h, "json_object");
            goto done;
        }

        if (store_initial_rootdir (ctx, rootdir, href) < 0) {
            flux_log_error (h, "storing root object");
            goto done;
        }
        setroot (ctx, href, 0);
    } else {
        href_t href;
        int rootseq;
        if (getroot_rpc (ctx, &rootseq, href) < 0) {
            flux_log_error (h, "getroot");
            goto done;
        }
        setroot (ctx, href, rootseq);
    }
    if (flux_msg_handler_addvec (h, handlers, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_delvec;
    }
    rc = 0;
done_delvec:
    flux_msg_handler_delvec (handlers);
done:
    return rc;
}

MOD_NAME ("kvs");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
