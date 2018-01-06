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

#include "src/common/libutil/base64.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libkvs/treeobj.h"

#include "waitqueue.h"
#include "cache.h"
#include "kvs_util.h"

#include "lookup.h"
#include "fence.h"
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
    zhash_t *roothash;
    zlist_t *removelist;        /* temp for removing items */
    int faults;                 /* for kvs.stats.get, etc. */
    flux_t *h;
    uint32_t rank;
    int epoch;              /* tracks current heartbeat epoch */
    flux_watcher_t *prep_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int commit_merge;
    bool events_init;            /* flag */
    const char *hash_name;
} kvs_ctx_t;

struct kvsroot {
    char *namespace;
    int seq;
    blobref_t ref;
    commit_mgr_t *cm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int flags;
    bool remove;
};

struct kvs_cb_data {
    kvs_ctx_t *ctx;
    struct kvsroot *root;
    wait_t *wait;
    int errnum;
};

static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg);
static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg);

/*
 * kvs_ctx_t functions
 */
static void freectx (void *arg)
{
    kvs_ctx_t *ctx = arg;
    if (ctx) {
        cache_destroy (ctx->cache);
        zhash_destroy (&ctx->roothash);
        zlist_destroy (&ctx->removelist);
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
        ctx->roothash = zhash_new ();
        ctx->removelist = zlist_new ();
        if (!ctx->cache || !ctx->roothash || !ctx->removelist) {
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

/*
 * kvsroot functions
 */

static void destroy_root (void *data)
{
    if (data) {
        struct kvsroot *root = data;
        if (root->namespace)
            free (root->namespace);
        if (root->cm)
            commit_mgr_destroy (root->cm);
        if (root->watchlist)
            wait_queue_destroy (root->watchlist);
        free (data);
    }
}

static void remove_root (kvs_ctx_t *ctx, const char *namespace)
{
    zhash_delete (ctx->roothash, namespace);
}

static struct kvsroot *lookup_root (kvs_ctx_t *ctx, const char *namespace)
{
    return zhash_lookup (ctx->roothash, namespace);
}

static struct kvsroot *lookup_root_safe (kvs_ctx_t *ctx, const char *namespace)
{
    struct kvsroot *root;

    if ((root = lookup_root (ctx, namespace))) {
        if (root->remove)
            root = NULL;
    }
    return root;
}

static struct kvsroot *create_root (kvs_ctx_t *ctx, const char *namespace,
                                    int flags)
{
    struct kvsroot *root;
    int save_errnum;

    if (!(root = calloc (1, sizeof (*root)))) {
        flux_log_error (ctx->h, "calloc");
        return NULL;
    }

    if (!(root->namespace = strdup (namespace))) {
        flux_log_error (ctx->h, "strdup");
        goto error;
    }

    if (!(root->cm = commit_mgr_create (ctx->cache,
                                        root->namespace,
                                        ctx->hash_name,
                                        ctx->h,
                                        ctx))) {
        flux_log_error (ctx->h, "commit_mgr_create");
        goto error;
    }

    if (!(root->watchlist = wait_queue_create ())) {
        flux_log_error (ctx->h, "wait_queue_create");
        goto error;
    }

    root->flags = flags;
    root->remove = false;

    if (zhash_insert (ctx->roothash, namespace, root) < 0) {
        flux_log_error (ctx->h, "zhash_insert");
        goto error;
    }

    if (!zhash_freefn (ctx->roothash, namespace, destroy_root)) {
        flux_log_error (ctx->h, "zhash_freefn");
        save_errnum = errno;
        zhash_delete (ctx->roothash, namespace);
        errno = save_errnum;
        goto error;
    }

    return root;

 error:
    save_errnum = errno;
    destroy_root (root);
    errno = save_errnum;
    return NULL;
}

/*
 * event subscribe/unsubscribe
 */

static int event_subscribe (kvs_ctx_t *ctx, const char *namespace)
{
    char *setroot_topic = NULL;
    char *error_topic = NULL;
    int rc = -1;

    /* do not want to subscribe to events that are not within our
     * namespace, so we subscribe to only specific ones.
     */

    if (!(ctx->events_init)) {

        /* These belong to all namespaces, subscribe once the first
         * time we init a namespace */

        if (flux_event_subscribe (ctx->h, "hb") < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }

        if (flux_event_subscribe (ctx->h, "kvs.stats.clear") < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }

        if (flux_event_subscribe (ctx->h, "kvs.dropcache") < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }

        if (flux_event_subscribe (ctx->h, "kvs.namespace.remove") < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }

        ctx->events_init = true;
    }

    if (asprintf (&setroot_topic, "kvs.setroot.%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_subscribe (ctx->h, setroot_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    if (asprintf (&error_topic, "kvs.error.%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_subscribe (ctx->h, error_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    rc = 0;
cleanup:
    free (setroot_topic);
    free (error_topic);
    return rc;
}

static int event_unsubscribe (kvs_ctx_t *ctx, const char *namespace)
{
    char *setroot_topic = NULL;
    char *error_topic = NULL;
    int rc = -1;

    if (asprintf (&setroot_topic, "kvs.setroot.%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_unsubscribe (ctx->h, setroot_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    if (asprintf (&error_topic, "kvs.error.%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_unsubscribe (ctx->h, error_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    rc = 0;
cleanup:
    free (setroot_topic);
    free (error_topic);
    return rc;
}

/*
 * set/get root
 */

static void setroot (kvs_ctx_t *ctx, struct kvsroot *root,
                     const char *rootref, int rootseq)
{
    if (rootseq == 0 || rootseq > root->seq) {
        assert (strlen (rootref) < sizeof (blobref_t));
        strcpy (root->ref, rootref);
        root->seq = rootseq;
        /* log error on wait_runqueue(), don't error out.  watchers
         * may miss value change, but will never get older one.
         * Maintains consistency model */
        if (wait_runqueue (root->watchlist) < 0)
            flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);
        root->watchlist_lastrun_epoch = ctx->epoch;
    }
}

static int getroot_rpc (kvs_ctx_t *ctx, const char *namespace, int *rootseq,
                        blobref_t rootref, int *flagsp)
{
    flux_future_t *f;
    const char *ref;
    int flags;
    int saved_errno, rc = -1;

    /* XXX: future make asynchronous */
    if (!(f = flux_rpc_pack (ctx->h, "kvs.getroot", FLUX_NODEID_UPSTREAM, 0,
                             "{ s:s }",
                             "namespace", namespace))) {
        saved_errno = errno;
        goto done;
    }
    if (flux_rpc_get_unpack (f, "{ s:i s:s s:i }",
                             "rootseq", rootseq,
                             "rootref", &ref,
                             "flags", &flags) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto done;
    }
    if (strlen (ref) > sizeof (blobref_t) - 1) {
        saved_errno = EPROTO;
        goto done;
    }
    strcpy (rootref, ref);
    if (flagsp)
        (*flagsp) = flags;
    rc = 0;
done:
    flux_future_destroy (f);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static struct kvsroot *getroot (kvs_ctx_t *ctx, const char *namespace)
{
    struct kvsroot *root;
    blobref_t rootref;
    int save_errno, rootseq, flags;

    if (!(root = lookup_root_safe (ctx, namespace))) {
        if (ctx->rank == 0) {
            flux_log (ctx->h, LOG_DEBUG, "namespace %s not available",
                      namespace);
            errno = ENOTSUP;
            return NULL;
        }
        else {
            if (getroot_rpc (ctx, namespace, &rootseq, rootref, &flags) < 0) {
                flux_log_error (ctx->h, "getroot_rpc");
                return NULL;
            }

            if (!(root = create_root (ctx, namespace, flags))) {
                flux_log_error (ctx->h, "create_root");
                return NULL;
            }

            setroot (ctx, root, rootref, rootseq);

            if (event_subscribe (ctx, namespace) < 0) {
                save_errno = errno;
                remove_root (ctx, namespace);
                errno = save_errno;
                flux_log_error (ctx->h, "event_subscribe");
                return NULL;
            }
        }
    }
    return root;
}

/*
 * load
 */

static void content_load_completion (flux_future_t *f, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const void *data;
    int size;
    const char *blobref;
    struct cache_entry *entry;

    if (flux_content_load_get (f, &data, &size) < 0) {
        flux_log_error (ctx->h, "%s: flux_content_load_get", __FUNCTION__);
        goto done;
    }
    blobref = flux_future_aux_get (f, "ref");
    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it is not yet valid.  But check and log incase there is
     * logic error dealng with error paths using cache_remove_entry().
     */
    if (!(entry = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto done;
    }

    /* If cache_entry_set_raw() fails, it's a pretty terrible error
     * case, where we've loaded an object from the content store, but
     * can't put it in the cache.
     *
     * If there was a waiter on this cache entry waiting for it to be
     * valid, the load() will ultimately hang.  The caller will
     * timeout or eventually give up, so the KVS can continue along
     * its merry way.  So we just log the error.
     */
    if (cache_entry_set_raw (entry, data, size) < 0) {
        flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
        goto done;
    }

done:
    flux_future_destroy (f);
}

/* Send content load request and setup contination to handle response.
 */
static int content_load_request_send (kvs_ctx_t *ctx, const blobref_t ref)
{
    flux_future_t *f = NULL;
    char *refcpy;
    int saved_errno;

    if (!(f = flux_content_load (ctx->h, ref, 0)))
        goto error;
    if (!(refcpy = strdup (ref))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_future_aux_set (f, "ref", refcpy, free) < 0) {
        free (refcpy);
        goto error;
    }
    if (flux_future_then (f, -1., content_load_completion, ctx) < 0)
        goto error;
    return 0;
error:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return -1;
}

/* Return 0 on success, -1 on error.  Set stall variable appropriately
 */
static int load (kvs_ctx_t *ctx, const blobref_t ref, wait_t *wait, bool *stall)
{
    struct cache_entry *entry = cache_lookup (ctx->cache, ref, ctx->epoch);
    int saved_errno, ret;

    assert (wait != NULL);

    /* Create an incomplete hash entry if none found.
     */
    if (!entry) {
        if (!(entry = cache_entry_create ())) {
            flux_log_error (ctx->h, "%s: cache_entry_create",
                            __FUNCTION__);
            return -1;
        }
        cache_insert (ctx->cache, ref, entry);
        if (content_load_request_send (ctx, ref) < 0) {
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
     * arrange to stall caller.
     */
    if (!cache_entry_get_valid (entry)) {
        if (cache_entry_wait_valid (entry, wait) < 0) {
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

    if (stall)
        *stall = false;
    return 0;
}

/*
 * store/commit
 */

static int content_store_get (flux_future_t *f, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct cache_entry *entry;
    const char *blobref;
    int rc = -1;
    int saved_errno, ret;

    if (flux_content_store_get (f, &blobref) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_content_store_get", __FUNCTION__);
        goto done;
    }
    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it was dirty.  But check and log incase there is logic
     * error dealng with error paths using cache_remove_entry().
     */
    if (!(entry = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
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
    if (cache_entry_set_dirty (entry, false) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: cache_entry_set_dirty",
                        __FUNCTION__);
        ret = cache_entry_force_clear_dirty (entry);
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

static int content_store_request_send (kvs_ctx_t *ctx, const void *data,
                                       int len, bool now)
{
    flux_future_t *f;
    int saved_errno, rc = -1;

    if (!(f = flux_content_store (ctx->h, data, len, 0)))
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
    return rc;
}

static int commit_load_cb (commit_t *c, const char *ref, void *data)
{
    struct kvs_cb_data *cbd = data;
    bool stall;

    if (load (cbd->ctx, ref, cbd->wait, &stall) < 0) {
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
static int commit_cache_cb (commit_t *c, struct cache_entry *entry, void *data)
{
    struct kvs_cb_data *cbd = data;
    const void *storedata;
    int storedatalen = 0;

    assert (cache_entry_get_dirty (entry));

    if (cache_entry_get_raw (entry, &storedata, &storedatalen) < 0) {
        flux_log_error (cbd->ctx->h, "%s: cache_entry_get_raw",
                        __FUNCTION__);
        commit_cleanup_dirty_cache_entry (c, entry);
        return -1;
    }
    if (content_store_request_send (cbd->ctx,
                                    storedata,
                                    storedatalen,
                                    false) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "%s: content_store_request_send",
                        __FUNCTION__);
        commit_cleanup_dirty_cache_entry (c, entry);
        return -1;
    }
    if (cache_entry_wait_notdirty (entry, cbd->wait) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "cache_entry_wait_notdirty");
        commit_cleanup_dirty_cache_entry (c, entry);
        return -1;
    }
    return 0;
}

static int setroot_event_send (kvs_ctx_t *ctx, struct kvsroot *root,
                               json_t *names)
{
    const json_t *root_dir = NULL;
    json_t *nullobj = NULL;
    flux_msg_t *msg = NULL;
    char *setroot_topic = NULL;
    int saved_errno, rc = -1;

    assert (ctx->rank == 0);

    if (event_includes_rootdir) {
        struct cache_entry *entry;

        if ((entry = cache_lookup (ctx->cache, root->ref, ctx->epoch)))
            root_dir = cache_entry_get_treeobj (entry);
        assert (root_dir != NULL); // root entry is always in cache on rank 0
    }
    else {
        if (!(nullobj = json_null ())) {
            saved_errno = errno;
            flux_log_error (ctx->h, "%s: json_null", __FUNCTION__);
            goto done;
        }
        root_dir = nullobj;
    }

    if (asprintf (&setroot_topic, "kvs.setroot.%s", root->namespace) < 0) {
        saved_errno = ENOMEM;
        flux_log_error (ctx->h, "%s: asprintf", __FUNCTION__);
        goto done;
    }

    if (!(msg = flux_event_pack (setroot_topic, "{ s:s s:i s:s s:O s:O }",
                                 "namespace", root->namespace,
                                 "rootseq", root->seq,
                                 "rootref", root->ref,
                                 "names", names,
                                 "rootdir", root_dir))) {
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
    free (setroot_topic);
    flux_msg_destroy (msg);
    json_decref (nullobj);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static int error_event_send (kvs_ctx_t *ctx, const char *namespace,
                             json_t *names, int errnum)
{
    flux_msg_t *msg = NULL;
    char *error_topic = NULL;
    int saved_errno, rc = -1;

    if (asprintf (&error_topic, "kvs.error.%s", namespace) < 0) {
        saved_errno = ENOMEM;
        flux_log_error (ctx->h, "%s: asprintf", __FUNCTION__);
        goto done;
    }

    if (!(msg = flux_event_pack (error_topic, "{ s:s s:O s:i }",
                                 "namespace", namespace,
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
    free (error_topic);
    flux_msg_destroy (msg);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static int error_event_send_to_name (kvs_ctx_t *ctx, const char *namespace,
                                     const char *name, int errnum)
{
    json_t *names = NULL;
    int rc = -1;

    if (!(names = json_pack ("[ s ]", name))) {
        flux_log_error (ctx->h, "%s: json_pack", __FUNCTION__);
        errno = ENOMEM;
        goto done;
    }

    rc = error_event_send (ctx, namespace, names, errnum);
done:
    json_decref (names);
    return rc;
}

/* Commit all the ops for a particular commit/fence request (rank 0 only).
 * The setroot event will cause responses to be sent to the fence requests
 * and clean up the fence_t state.  This function is idempotent.
 */
static void commit_apply (commit_t *c)
{
    kvs_ctx_t *ctx = commit_get_aux (c);
    const char *namespace;
    struct kvsroot *root = NULL;
    wait_t *wait = NULL;
    int errnum = 0;
    commit_process_t ret;

    namespace = commit_get_namespace (c);
    assert (namespace);

    /* Between call to commit_mgr_process_fence_request() and here,
     * possible namespace marked for removal.  Also namespace could
     * have been removed if we waited and this is a replay.
     *
     * root should never be NULL, as it should not be garbage
     * collected until all ready commits have been processed.
     */

    root = lookup_root (ctx, namespace);
    assert (root);

    if (root->remove) {
        flux_log (ctx->h, LOG_DEBUG, "%s: namespace %s removed", __FUNCTION__,
                  namespace);
        errnum = ENOTSUP;
        goto done;
    }

    if ((errnum = commit_get_aux_errnum (c)))
        goto done;

    if ((ret = commit_process (c,
                               ctx->epoch,
                               root->ref)) == COMMIT_PROCESS_ERROR) {
        errnum = commit_get_errnum (c);
        goto done;
    }

    if (ret == COMMIT_PROCESS_LOAD_MISSING_REFS) {
        struct kvs_cb_data cbd;

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
        struct kvs_cb_data cbd;

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
     * root->ref with newroot, incrementing root->seq, and sending out
     * the setroot event for "eventual consistency" of other nodes.
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
        setroot (ctx, root, commit_get_newroot_ref (c), root->seq + 1);
        setroot_event_send (ctx, root, fence_get_json_names (f));
    } else {
        fence_t *f = commit_get_fence (c);
        flux_log (ctx->h, LOG_ERR, "commit failed: %s",
                  flux_strerror (errnum));
        error_event_send (ctx, root->namespace, fence_get_json_names (f),
                          errnum);
    }
    wait_destroy (wait);

    /* Completed: remove from 'ready' list.
     * N.B. fence_t remains in the fences hash until event is received.
     */
    commit_mgr_remove_commit (root->cm, c);
    return;

stall:
    return;
}

/*
 * pre/check event callbacks
 */

static void commit_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    bool ready = false;

    root = zhash_first (ctx->roothash);
    while (root) {

        if (commit_mgr_commits_ready (root->cm)) {
            ready = true;
            break;
        }

        root = zhash_next (ctx->roothash);
    }

    if (ready)
        flux_watcher_start (ctx->idle_w);
}

static void commit_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    commit_t *c;

    flux_watcher_stop (ctx->idle_w);

    root = zhash_first (ctx->roothash);
    while (root) {
        if ((c = commit_mgr_get_ready_commit (root->cm))) {
            if (ctx->commit_merge) {
                /* if merge fails, set errnum in commit_t, let
                 * commit_apply() handle error handling.
                 */
                if (commit_mgr_merge_ready_commits (root->cm) < 0)
                    commit_set_aux_errnum (c, errno);
            }

            /* It does not matter if root has been marked for removal,
             * we want to process and clear all lingering ready
             * commits in this commit mgr
             */
            commit_apply (c);
        }

        root = zhash_next (ctx->roothash);
    }
}

/*
 * rpc/event callbacks
 */

static void dropcache_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                  const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int size, expcount = 0;
    int rc = -1;

    /* irrelevant if root not initialized, drop cache entries */

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

static void dropcache_event_cb (flux_t *h, flux_msg_handler_t *mh,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int size, expcount = 0;

    /* irrelevant if root not initialized, drop cache entries */

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

static void heartbeat_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log_error (ctx->h, "%s: flux_heartbeat_decode", __FUNCTION__);
        return;
    }

    root = zhash_first (ctx->roothash);
    while (root) {

        if (root->remove) {
            if (!wait_queue_length (root->watchlist)
                && !commit_mgr_fences_count (root->cm)
                && !commit_mgr_ready_commit_count (root->cm)) {

                if (event_unsubscribe (ctx, root->namespace) < 0)
                    flux_log_error (ctx->h, "%s: event_unsubscribe",
                                    __FUNCTION__);

                /* can't delete items while iterating through hash,
                 * put on temp removelist */
                if (zlist_append (ctx->removelist, root) < 0)
                    flux_log_error (ctx->h, "%s: zlist_append",
                                    __FUNCTION__);
            }
        }
        else {
            /* "touch" objects involved in watched keys */
            if (ctx->epoch - root->watchlist_lastrun_epoch > max_lastuse_age) {
                /* log error on wait_runqueue(), don't error out.  watchers
                 * may miss value change, but will never get older one.
                 * Maintains consistency model */
                if (wait_runqueue (root->watchlist) < 0)
                    flux_log_error (h, "%s: wait_runqueue", __FUNCTION__);
                root->watchlist_lastrun_epoch = ctx->epoch;
            }
            /* "touch" root */
            (void)cache_lookup (ctx->cache, root->ref, ctx->epoch);
        }

        root = zhash_next (ctx->roothash);
    }

    while ((root = zlist_pop (ctx->removelist)))
        remove_root (ctx, root->namespace);

    if (cache_expire_entries (ctx->cache, ctx->epoch, max_lastuse_age) < 0)
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
}

static int lookup_load_cb (lookup_t *lh, const char *ref, void *data)
{
    struct kvs_cb_data *cbd = data;
    bool stall;

    if (load (cbd->ctx, ref, cbd->wait, &stall) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "%s: load", __FUNCTION__);
        return -1;
    }
    /* if not stalling, logic issue within code */
    assert (stall);
    return 0;
}

static void get_request_cb (flux_t *h, flux_msg_handler_t *mh,
                            const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = NULL;
    int flags;
    const char *namespace;
    const char *key;
    json_t *val = NULL;
    json_t *root_dirent = NULL;
    json_t *tmp_dirent = NULL;
    lookup_t *lh = NULL;
    const char *root_ref = NULL;
    wait_t *wait = NULL;
    int rc = -1;
    int ret;

    /* if bad lh, then first time rpc and not a replay */
    if (lookup_validate (arg) == false) {
        struct kvsroot *root;

        ctx = arg;

        if (flux_request_unpack (msg, NULL, "{ s:s s:s s:i }",
                                 "key", &key,
                                 "namespace", &namespace,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        if (!(root = getroot (ctx, namespace)))
            goto done;

        /* rootdir is optional */
        (void)flux_request_unpack (msg, NULL, "{ s:o }",
                                   "rootdir", &root_dirent);

        /* If root dirent was specified, lookup corresponding 'root' directory.
         * Otherwise, use the current root.
         */
        if (root_dirent) {
            if (treeobj_validate (root_dirent) < 0
                || !treeobj_is_dirref (root_dirent)
                || !(root_ref = treeobj_get_blobref (root_dirent, 0))) {
                errno = EINVAL;
                goto done;
            }
        }

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->epoch,
                                  namespace,
                                  root_ref ? root_ref : root->ref,
                                  key,
                                  h,
                                  flags)))
            goto done;

        ret = lookup_set_aux_data (lh, ctx);
        assert (ret == 0);
    }
    else {
        int err;

        lh = arg;

        /* error in prior load(), waited for in flight rpcs to complete */
        if ((err = lookup_get_aux_errnum (lh))) {
            errno = err;
            goto done;
        }

        namespace = lookup_get_namespace (lh);
        assert (namespace);

        ctx = lookup_get_aux_data (lh);
        assert (ctx);

        /* Chance kvsroot removed while we waited */

        if (!lookup_root_safe (ctx, namespace)) {
            flux_log (h, LOG_DEBUG, "%s: namespace %s lost", __FUNCTION__,
                      namespace);
            errno = ENOTSUP;
            goto done;
        }

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);
    }

    if (!lookup (lh)) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create_msg_handler (h, mh, msg, get_request_cb, lh)))
            goto done;

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (lookup_iter_missing_refs (lh, lookup_load_cb, &cbd) < 0) {
            errno = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                lookup_set_aux_errnum (lh, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
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
        if (!(tmp_dirent = treeobj_create_dirref (tmprootref))) {
            flux_log_error (h, "%s: treeobj_create_dirref", __FUNCTION__);
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

static void watch_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = NULL;
    json_t *oval = NULL;
    json_t *val = NULL;
    flux_msg_t *cpy = NULL;
    struct kvsroot *root = NULL;
    const char *key;
    const char *namespace;
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

        if (flux_request_unpack (msg, NULL, "{ s:s s:s s:o s:i }",
                                 "key", &key,
                                 "namespace", &namespace,
                                 "val", &oval,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        if (!(root = getroot (ctx, namespace)))
            goto done;

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->epoch,
                                  namespace,
                                  root->ref,
                                  key,
                                  h,
                                  flags)))
            goto done;

        ret = lookup_set_aux_data (lh, ctx);
        assert (ret == 0);
    }
    else {
        int err;

        lh = arg;

        /* error in prior load(), waited for in flight rpcs to complete */
        if ((err = lookup_get_aux_errnum (lh))) {
            errno = err;
            goto done;
        }

        namespace = lookup_get_namespace (lh);
        assert (namespace);

        ctx = lookup_get_aux_data (lh);
        assert (ctx);

        /* Chance kvsroot removed while we waited */

        if (!(root = lookup_root_safe (ctx, namespace))) {
            flux_log (h, LOG_DEBUG, "%s: namespace %s lost", __FUNCTION__,
                      namespace);
            errno = ENOTSUP;
            goto done;
        }

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);

        isreplay = true;
    }

    if (!lookup (lh)) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create_msg_handler (h, mh, msg,
                                              watch_request_cb, lh)))
            goto done;

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (lookup_iter_missing_refs (lh, lookup_load_cb, &cbd) < 0) {
            errno = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                lookup_set_aux_errnum (lh, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
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
        if (flux_request_unpack (msg, NULL, "{ s:s s:s s:o s:i }",
                                 "key", &key,
                                 "namespace", &namespace,
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
     * Arrange to wait on root->watchlist for each new commit.
     * Reconstruct the payload with 'first' flag clear, and updated value.
     */
    if (!out || !(flags & KVS_WATCH_ONCE)) {
        if (!(cpy = flux_msg_copy (msg, false)))
            goto done;

        if (flux_msg_pack (cpy, "{ s:s s:s s:O s:i }",
                           "key", key,
                           "namespace", namespace,
                           "val", val,
                           "flags", flags & ~KVS_WATCH_FIRST) < 0) {
            flux_log_error (h, "%s: flux_msg_pack", __FUNCTION__);
            goto done;
        }

        if (!(watcher = wait_create_msg_handler (h, mh, cpy,
                                                 watch_request_cb, ctx)))
            goto done;
        if (wait_addqueue (root->watchlist, watcher) < 0) {
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
    char *key;
    char *sender;
} unwatch_param_t;

static bool unwatch_cmp (const flux_msg_t *msg, void *arg)
{
    unwatch_param_t *p = arg;
    char *sender = NULL;
    char *normkey = NULL;
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
    if (!(normkey = kvs_util_normalize_key (key, NULL)))
        goto done;
    if (strcmp (p->key, normkey) != 0)
        goto done;
    match = true;
done:
    free (sender);
    free (normkey);
    return match;
}

static void unwatch_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    const char *key;
    unwatch_param_t p = { NULL, NULL };
    int errnum = 0;

    if (flux_request_unpack (msg, NULL, "{ s:s s:s }",
                             "namespace", &namespace,
                             "key", &key) < 0) {
        errnum = errno;
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto done;
    }

    /* if root not initialized, success automatically
     * - any lingering watches on a namespace that is in the process
     *   of removal will be cleaned up through other means.
     */
    if (!(root = lookup_root_safe (ctx, namespace)))
        goto done;

    if (!(p.key = kvs_util_normalize_key (key, NULL))) {
        errnum = errno;
        goto done;
    }
    if (flux_msg_get_route_first (msg, &p.sender) < 0) {
        errnum = errno;
        goto done;
    }
    /* N.B. impossible for a watch to be on watchlist and cache waiter
     * at the same time (i.e. on watchlist means we're watching, if on
     * cache waiter we're not done processing towards being on the
     * watchlist).  So if wait_destroy_msg() on the waitlist succeeds
     * but cache_wait_destroy_msg() fails, it's not that big of a
     * deal.  The current state is still maintained.
     */
    if (wait_destroy_msg (root->watchlist, unwatch_cmp, &p) < 0) {
        errnum = errno;
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
        goto done;
    }
    if (cache_wait_destroy_msg (ctx->cache, unwatch_cmp, &p) < 0) {
        errnum = errno;
        flux_log_error (h, "%s: cache_wait_destroy_msg", __FUNCTION__);
        goto done;
    }
done:
    if (flux_respond (h, msg, errnum, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (p.key);
    free (p.sender);
}

static int finalize_fence_req (fence_t *f, const flux_msg_t *req, void *data)
{
    struct kvs_cb_data *cbd = data;

    if (flux_respond (cbd->ctx->h, req, cbd->errnum, NULL) < 0)
        flux_log_error (cbd->ctx->h, "%s: flux_respond", __FUNCTION__);

    return 0;
}

static void finalize_fences_bynames (kvs_ctx_t *ctx, struct kvsroot *root,
                                     json_t *names, int errnum)
{
    int i, len;
    json_t *name;
    fence_t *f;
    struct kvs_cb_data cbd = { .ctx = ctx, .errnum = errnum };

    if (!(len = json_array_size (names))) {
        flux_log_error (ctx->h, "%s: parsing array", __FUNCTION__);
        return;
    }
    for (i = 0; i < len; i++) {
        if (!(name = json_array_get (names, i))) {
            flux_log_error (ctx->h, "%s: parsing array[%d]", __FUNCTION__, i);
            return;
        }
        if ((f = commit_mgr_lookup_fence (root->cm, json_string_value (name)))) {
            fence_iter_request_copies (f, finalize_fence_req, &cbd);
            if (commit_mgr_remove_fence (root->cm, json_string_value (name)) < 0)
                flux_log_error (ctx->h, "%s: commit_mgr_remove_fence",
                                __FUNCTION__);
        }
    }
}

/* kvs.relayfence (rank 0 only, no response).
 */
static void relayfence_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    const char *name;
    int nprocs, flags;
    json_t *ops = NULL;
    fence_t *f;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &namespace,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }

    /* namespace must exist given we are on rank 0 */
    if (!(root = lookup_root_safe (ctx, namespace))) {
        flux_log (h, LOG_ERR, "%s: namespace %s not available",
                  __FUNCTION__, namespace);
        errno = ENOTSUP;
        goto error;
    }

    if (!(f = commit_mgr_lookup_fence (root->cm, name))) {
        if (!(f = fence_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: fence_create", __FUNCTION__);
            goto error;
        }
        if (commit_mgr_add_fence (root->cm, f) < 0) {
            flux_log_error (h, "%s: commit_mgr_add_fence", __FUNCTION__);
            fence_destroy (f);
            goto error;
        }
    }
    else
        fence_set_flags (f, fence_get_flags (f) | flags);

    if (fence_add_request_data (f, ops) < 0) {
        flux_log_error (h, "%s: fence_add_request_data", __FUNCTION__);
        goto error;
    }

    if (commit_mgr_process_fence_request (root->cm, name) < 0) {
        flux_log_error (h, "%s: commit_mgr_process_fence_request", __FUNCTION__);
        goto error;
    }

    return;

error:
    /* An error has occurred, so we will return an error similarly to
     * how an error would be returned via a commit error.
     */
    if (error_event_send_to_name (ctx, namespace, name, errno) < 0)
        flux_log_error (h, "%s: error_event_send_to_name", __FUNCTION__);
}

/* kvs.fence
 * Sent from users to local kvs module.
 */
static void fence_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    const char *name;
    int saved_errno, nprocs, flags;
    json_t *ops = NULL;
    fence_t *f;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &namespace,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, namespace)))
        goto error;

    if (!(f = commit_mgr_lookup_fence (root->cm, name))) {
        if (!(f = fence_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: fence_create", __FUNCTION__);
            goto error;
        }
        if (commit_mgr_add_fence (root->cm, f) < 0) {
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

        if (commit_mgr_process_fence_request (root->cm, name) < 0) {
            flux_log_error (h, "%s: commit_mgr_process_fence_request",
                            __FUNCTION__);
            goto error;
        }
    }
    else {
        flux_future_t *f;

        if (!(f = flux_rpc_pack (h, "kvs.relayfence", 0, FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:s s:i s:i }",
                                 "ops", ops,
                                 "name", name,
                                 "namespace", namespace,
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
static void sync_request_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;
    struct kvsroot *root;
    int saved_errno, rootseq;
    wait_t *wait = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:i s:s }",
                             "rootseq", &rootseq,
                             "namespace", &namespace) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, namespace)))
        goto error;

    if (root->seq < rootseq) {
        if (!(wait = wait_create_msg_handler (h, mh, msg,
                                              sync_request_cb, arg)))
            goto error;
        if (wait_addqueue (root->watchlist, wait) < 0) {
            saved_errno = errno;
            wait_destroy (wait);
            errno = saved_errno;
            goto error;
        }
        return; /* stall */
    }
    if (flux_respond_pack (h, msg, "{ s:i s:s }",
                           "rootseq", root->seq,
                           "rootref", root->ref) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void getroot_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;
    struct kvsroot *root;

    if (flux_request_unpack (msg, NULL, "{ s:s }",
                             "namespace", &namespace) < 0) {
        flux_log_error (ctx->h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (ctx->rank == 0) {
        /* namespace must exist given we are on rank 0 */
        if (!(root = lookup_root_safe (ctx, namespace))) {
            flux_log (h, LOG_DEBUG, "namespace %s not available", namespace);
            errno = ENOTSUP;
            goto error;
        }
    }
    else {
        /* If root is not initialized, we have to intialize ourselves
         * first.
         */
        if (!(root = getroot (ctx, namespace)))
            goto error;
    }

    if (flux_respond_pack (h, msg, "{ s:i s:s s:i }",
                           "rootseq", root->seq,
                           "rootref", root->ref,
                           "flags", root->flags) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void error_event_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    json_t *names = NULL;
    int errnum;

    if (flux_event_unpack (msg, NULL, "{ s:s s:o s:i }",
                           "namespace", &namespace,
                           "names", &names,
                           "errnum", &errnum) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    /* if root not initialized, nothing to do
     * - it is ok that the namespace be marked for removal, we may be
     *   cleaning up lingering commits.
     */
    if (!(root = lookup_root (ctx, namespace))) {
        flux_log (ctx->h, LOG_ERR, "%s: received unknown namespace %s",
                  __FUNCTION__, namespace);
        return;
    }

    finalize_fences_bynames (ctx, root, names, errnum);
}

/* Optimization: the current rootdir object is optionally included
 * in the kvs.setroot event.  Prime the local cache with it.
 * If there are complications, just skip it.  Not critical.
 */
static void prime_cache_with_rootdir (kvs_ctx_t *ctx, json_t *rootdir)
{
    struct cache_entry *entry;
    blobref_t ref;
    void *data = NULL;
    int len;

    if (treeobj_validate (rootdir) < 0 || !treeobj_is_dir (rootdir)) {
        flux_log (ctx->h, LOG_ERR, "%s: invalid rootdir", __FUNCTION__);
        goto done;
    }
    if (!(data = treeobj_encode (rootdir))) {
        flux_log_error (ctx->h, "%s: treeobj_encode", __FUNCTION__);
        goto done;
    }
    len = strlen (data);
    if (blobref_hash (ctx->hash_name, data, len, ref) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto done;
    }
    if ((entry = cache_lookup (ctx->cache, ref, ctx->epoch)))
        goto done; // already in cache, possibly dirty/invalid - we don't care
    if (!(entry = cache_entry_create ())) {
        flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
        goto done;
    }
    if (cache_entry_set_raw (entry, data, len) < 0) {
        flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
        cache_entry_destroy (entry);
        goto done;
    }
    cache_insert (ctx->cache, ref, entry);
done:
    free (data);
}

/* Alter the (rootref, rootseq) in response to a setroot event.
 */
static void setroot_event_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    int rootseq;
    const char *rootref;
    json_t *rootdir = NULL;
    json_t *names = NULL;
    int errnum = 0;

    if (flux_event_unpack (msg, NULL, "{ s:s s:i s:s s:o s:o }",
                           "namespace", &namespace,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "names", &names,
                           "rootdir", &rootdir) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    /* if root not initialized, nothing to do
     * - small chance we could receive setroot event on namespace that
     *   is being removed.  Would require events to be received out of
     *   order (commit completes before namespace removed, but
     *   namespace remove event received before setroot).
     */
    if (!(root = lookup_root (ctx, namespace))) {
        flux_log (ctx->h, LOG_ERR, "%s: received unknown namespace %s",
                  __FUNCTION__, namespace);
        return;
    }

    /* in rare chance we receive setroot on removed namespace, return
     * ENOTSUP to client callers */
    if (root->remove)
        errnum = ENOTSUP;

    finalize_fences_bynames (ctx, root, names, errnum);

    /* if error, not need to complete setroot */
    if (errnum)
        return;

    /* Optimization: prime local cache with directory object, if provided
     * in event message.  Ignore failure here - object will be fetched on
     * demand from content cache if not in local cache.
     */
    if (!json_is_null (rootdir))
        prime_cache_with_rootdir (ctx, rootdir);

    setroot (ctx, root, rootref, rootseq);
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

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
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
    root = zhash_first (ctx->roothash);
    while (root) {

        if (wait_destroy_msg (root->watchlist, disconnect_cmp, sender) < 0)
            flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);

        root = zhash_next (ctx->roothash);
    }
    if (cache_wait_destroy_msg (ctx->cache, disconnect_cmp, sender) < 0)
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
    free (sender);
}

static void stats_get_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_t *tstats = NULL;
    json_t *cstats = NULL;
    json_t *nsstats = NULL;
    tstat_t ts = { .min = 0.0, .max = 0.0, .M = 0.0, .S = 0.0, .newM = 0.0,
                   .newS = 0.0, .n = 0 };
    int size = 0, incomplete = 0, dirty = 0;
    int rc = -1;
    double scale = 1E-3;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;

    /* if no roots are initialized, respond with all zeroes as stats */
    if (zhash_size (ctx->roothash)) {
        if (cache_get_stats (ctx->cache, &ts, &size, &incomplete, &dirty) < 0)
            goto done;
    }

    if (!(tstats = json_pack ("{ s:i s:f s:f s:f s:f }",
                              "count", tstat_count (&ts),
                              "min", tstat_min (&ts)*scale,
                              "mean", tstat_mean (&ts)*scale,
                              "stddev", tstat_stddev (&ts)*scale,
                              "max", tstat_max (&ts)*scale))) {
        errno = ENOMEM;
        goto done;
    }

    if (!(cstats = json_pack ("{ s:f s:O s:i s:i s:i }",
                              "obj size total (MiB)", (double)size/1048576,
                              "obj size (KiB)", tstats,
                              "#obj dirty", dirty,
                              "#obj incomplete", incomplete,
                              "#faults", ctx->faults))) {
        errno = ENOMEM;
        goto done;
    }

    if (!(nsstats = json_object ())) {
        errno = ENOMEM;
        goto done;
    }

    if (zhash_size (ctx->roothash)) {
        struct kvsroot *root;

        root = zhash_first (ctx->roothash);
        while (root) {
            json_t *s;

            if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                                 "#watchers",
                                     wait_queue_length (root->watchlist),
                                 "#no-op stores",
                                     commit_mgr_get_noop_stores (root->cm),
                                 "#fences",
                                     commit_mgr_fences_count (root->cm),
                                 "#readycommits",
                                     commit_mgr_ready_commit_count (root->cm),
                                 "store revision", root->seq))) {
                errno = ENOMEM;
                goto done;
            }

            json_object_set_new (nsstats, root->namespace, s);

            root = zhash_next (ctx->roothash);
        }
    }
    else {
        json_t *s;

        if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                             "#watchers", 0,
                             "#no-op stores", 0,
                             "#fences", 0,
                             "#readycommits", 0,
                             "store revision", 0))) {
            errno = ENOMEM;
            goto done;
        }

        json_object_set_new (nsstats, KVS_PRIMARY_NAMESPACE, s);
    }

    if (flux_respond_pack (h, msg,
                           "{ s:O s:O }",
                           "cache", cstats,
                           "namespace", nsstats) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto done;
    }

    rc = 0;
 done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    json_decref (tstats);
    json_decref (cstats);
    json_decref (nsstats);
}

static void stats_clear (kvs_ctx_t *ctx)
{
    struct kvsroot *root;

    ctx->faults = 0;

    root = zhash_first (ctx->roothash);
    while (root) {
        commit_mgr_clear_noop_stores (root->cm);
        root = zhash_next (ctx->roothash);
    }
}

static void stats_clear_event_cb (flux_t *h, flux_msg_handler_t *mh,
                                  const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    stats_clear (ctx);
}

static void stats_clear_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                    const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    stats_clear (ctx);

    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static int namespace_create (kvs_ctx_t *ctx, const char *namespace, int flags)
{
    struct kvsroot *root;
    json_t *rootdir = NULL;
    blobref_t ref;
    void *data = NULL;
    int len;

    /* If namespace already exists, return EEXIST.  Doesn't matter if
     * namespace is in process of being removed */
    if (lookup_root (ctx, namespace)) {
        errno = EEXIST;
        goto cleanup;
    }

    if (!(root = create_root (ctx, namespace, flags))) {
        flux_log_error (ctx->h, "%s: create_root", __FUNCTION__);
        goto cleanup;
    }

    if (!(rootdir = treeobj_create_dir ())) {
        flux_log_error (ctx->h, "%s: treeobj_create_dir", __FUNCTION__);
        goto cleanup_remove_root;
    }

    if (!(data = treeobj_encode (rootdir))) {
        flux_log_error (ctx->h, "%s: treeobj_encode", __FUNCTION__);
        goto cleanup_remove_root;
    }
    len = strlen (data);

    if (blobref_hash (ctx->hash_name, data, len, ref) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto cleanup_remove_root;
    }

    setroot (ctx, root, ref, 0);

    if (event_subscribe (ctx, namespace) < 0) {
        flux_log_error (ctx->h, "%s: event_subscribe", __FUNCTION__);
        goto cleanup_remove_root;
    }

    return 0;

cleanup_remove_root:
    remove_root (ctx, namespace);
cleanup:
    free (data);
    json_decref (rootdir);
    return -1;
}

static void namespace_create_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                         const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;
    int flags;

    assert (ctx->rank == 0);

    if (flux_request_unpack (msg, NULL, "{ s:s s:i }",
                             "namespace", &namespace,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (namespace_create (ctx, namespace, flags) < 0) {
        flux_log_error (h, "%s: namespace_create", __FUNCTION__);
        goto error;
    }

    errno = 0;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static int root_remove_process_fences (fence_t *f, void *data)
{
    struct kvs_cb_data *cbd = data;

    /* Not ready fences will never finish, must alert them with
     * ENOTSUP that namespace removed.  Final call to
     * commit_mgr_remove_fence() done in finalize_fences_bynames() */
    finalize_fences_bynames (cbd->ctx, cbd->root, fence_get_json_names (f),
                             ENOTSUP);
    return 0;
}

static void start_root_remove (kvs_ctx_t *ctx, const char *namespace)
{
    struct kvsroot *root;

    /* safe lookup, if root removal in process, let it continue */
    if ((root = lookup_root_safe (ctx, namespace))) {
        struct kvs_cb_data cbd = { .ctx = ctx, .root = root };

        root->remove = true;

        /* Now that root has been marked for removal from roothash, run
         * the watchlist.  watch requests will notice root removed, return
         * ENOTSUP to watchers.
         */

        if (wait_runqueue (root->watchlist) < 0)
            flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);

        /* Ready fences will be processed and errors returned to
         * callers via the code path in commit_apply().  But not ready
         * fences must be dealt with separately here.
         *
         * Note that now that the root has been marked as removable, no
         * new fences can become ready in the future.  Checks in
         * fence_request_cb() and relayfence_request_cb() ensure this.
         */

        if (commit_mgr_iter_not_ready_fences (root->cm,
                                              root_remove_process_fences,
                                              &cbd) < 0)
            flux_log_error (ctx->h, "%s: commit_mgr_iter_fences", __FUNCTION__);
    }
}

static int namespace_remove (kvs_ctx_t *ctx, const char *namespace)
{
    flux_msg_t *msg = NULL;
    int saved_errno, rc = -1;

    /* Namespace doesn't exist or is already in process of being
     * removed */
    if (!lookup_root_safe (ctx, namespace)) {
        /* silently succeed */
        goto done;
    }

    if (!(msg = flux_event_pack ("kvs.namespace.remove", "{ s:s }",
                                 "namespace", namespace))) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: flux_event_pack", __FUNCTION__);
        goto cleanup;
    }
    if (flux_msg_set_private (msg) < 0) {
        saved_errno = errno;
        goto cleanup;
    }
    if (flux_send (ctx->h, msg, 0) < 0) {
        saved_errno = errno;
        goto cleanup;
    }

    start_root_remove (ctx, namespace);
done:
    rc = 0;
cleanup:
    flux_msg_destroy (msg);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void namespace_remove_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                         const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;

    assert (ctx->rank == 0);

    if (flux_request_unpack (msg, NULL, "{ s:s }",
                             "namespace", &namespace) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!strcasecmp (namespace, KVS_PRIMARY_NAMESPACE)) {
        errno = ENOTSUP;
        goto error;
    }

    if (namespace_remove (ctx, namespace) < 0) {
        flux_log_error (h, "%s: namespace_remove", __FUNCTION__);
        goto error;
    }

    errno = 0;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void namespace_remove_event_cb (flux_t *h, flux_msg_handler_t *mh,
                                       const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;

    if (flux_event_unpack (msg, NULL, "{ s:s }",
                           "namespace", &namespace) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    assert (strcasecmp (namespace, KVS_PRIMARY_NAMESPACE));

    start_root_remove (ctx, namespace);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.get",  stats_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.clear",stats_clear_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.stats.clear",stats_clear_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.setroot.*",  setroot_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.error.*",    error_event_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",    getroot_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",  dropcache_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.dropcache",  dropcache_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "hb",             heartbeat_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect", disconnect_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.unwatch",    unwatch_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.sync",       sync_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",        get_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",      watch_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.fence",      fence_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.relayfence", relayfence_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.namespace.create",
                            namespace_create_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.namespace.remove",
                            namespace_remove_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.namespace.remove",
                            namespace_remove_event_cb, 0 },
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

/* Store initial root in local cache, and flush to content cache
 * synchronously.  The corresponding blobref is written into 'ref'.
 */
static int store_initial_rootdir (kvs_ctx_t *ctx, blobref_t ref)
{
    struct cache_entry *entry;
    int saved_errno, ret;
    void *data = NULL;
    int len;
    flux_future_t *f = NULL;
    const char *newref;
    json_t *rootdir = NULL;

    if (!(rootdir = treeobj_create_dir ())) {
        flux_log_error (ctx->h, "%s: treeobj_create_dir", __FUNCTION__);
        goto error;
    }
    if (!(data = treeobj_encode (rootdir)))
        goto error;
    len = strlen (data);
    if (blobref_hash (ctx->hash_name, data, len, ref) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (ctx->cache, ref, ctx->epoch))) {
        if (!(entry = cache_entry_create ())) {
            flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
            goto error;
        }
        cache_insert (ctx->cache, ref, entry);
    }
    if (!cache_entry_get_valid (entry)) {
        if (cache_entry_set_raw (entry, data, len) < 0) { // makes entry valid
            flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
            goto error_uncache;
        }
        if (!(f = flux_content_store (ctx->h, data, len, 0))
                || flux_content_store_get (f, &newref) < 0) {
            flux_log_error (ctx->h, "%s: flux_content_store", __FUNCTION__);
            goto error_uncache;
        }
        /* Sanity check that content cache is using the same hash alg as KVS.
         * It should suffice to do this once at startup.
         */
        if (strcmp (newref, ref) != 0) {
            errno = EPROTO;
            flux_log_error (ctx->h, "%s: hash mismatch kvs=%s content=%s",
                            __FUNCTION__, ref, newref);
            goto error_uncache;
        }
    }
    free (data);
    flux_future_destroy (f);
    json_decref (rootdir);
    return 0;
error_uncache:
    saved_errno = errno;
    ret = cache_remove_entry (ctx->cache, ref);
    assert (ret == 1);
error:
    saved_errno = errno;
    free (data);
    flux_future_destroy (f);
    json_decref (rootdir);
    errno = saved_errno;
    return -1;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    kvs_ctx_t *ctx = getctx (h);
    flux_msg_handler_t **handlers = NULL;
    int rc = -1;

    if (!ctx) {
        flux_log_error (h, "error creating KVS context");
        goto done;
    }
    process_args (ctx, argc, argv);
    if (ctx->rank == 0) {
        struct kvsroot *root;
        blobref_t rootref;

        if (store_initial_rootdir (ctx, rootref) < 0) {
            flux_log_error (h, "storing initial root object");
            goto done;
        }

        /* primary namespace must always be there and not marked
         * for removal
         */
        if (!(root = lookup_root_safe (ctx, KVS_PRIMARY_NAMESPACE))) {
            if (!(root = create_root (ctx, KVS_PRIMARY_NAMESPACE, 0))) {
                flux_log_error (h, "create_root");
                goto done;
            }
        }

        setroot (ctx, root, rootref, 0);

        if (event_subscribe (ctx, KVS_PRIMARY_NAMESPACE) < 0) {
            flux_log_error (h, "event_subscribe");
            goto done;
        }
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    return rc;
}

MOD_NAME ("kvs");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
