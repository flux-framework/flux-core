/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libccan/ccan/list/list.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libkvs/kvs_txn_private.h"
#include "src/common/libkvs/kvs_util_private.h"
#include "src/common/libcontent/content.h"
#include "src/common/libutil/fsd.h"
#include "src/common/librouter/msg_hash.h"

#include "waitqueue.h"
#include "cache.h"

#include "lookup.h"
#include "treq.h"
#include "kvstxn.h"
#include "kvsroot.h"
#include "kvs_wait_version.h"
#include "kvs_checkpoint.h"

/* heartbeat_sync_cb() is called periodically to manage cached content
 * and namespaces.  Synchronize with the system heartbeat if possible,
 * but keep the time between checks bounded by 'heartbeat_sync_min'
 * and 'heartbeat_sync_max' seconds.
 */
const double heartbeat_sync_min = 1.;
const double heartbeat_sync_max = 30.;

/* Expire cache_entry after 'max_lastuse_age' seconds.
 */
const double max_lastuse_age = 10.;

/* Expire namespaces after 'max_namespace_age' seconds.
 */
const double max_namespace_age = 3600.;

struct kvs_ctx {
    struct cache *cache;    /* blobref => cache_entry */
    kvsroot_mgr_t *krm;
    int faults;                 /* for kvs.stats-get, etc. */
    flux_t *h;
    uint32_t rank;
    flux_watcher_t *prep_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int transaction_merge;
    bool events_init;            /* flag */
    char *hash_name;
    unsigned int seq;           /* for commit transactions */
    kvs_checkpoint_t *kcp;
    zhashx_t *requests;         /* track unfinished requests */
    struct list_head work_queue;
};

struct kvs_cb_data {
    struct kvs_ctx *ctx;
    struct kvsroot *root;
    wait_t *wait;
    int errnum;
    const flux_msg_t *msg;
};

static void transaction_prep_cb (flux_reactor_t *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg);
static void transaction_check_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);
static void start_root_remove (struct kvs_ctx *ctx, const char *ns);
static void work_queue_check_append (struct kvs_ctx *ctx,
                                     struct kvsroot *root);
static void kvstxn_apply (kvstxn_t *kt);

/*
 * kvs_ctx functions
 */
static void kvs_ctx_destroy (struct kvs_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        cache_destroy (ctx->cache);
        kvsroot_mgr_destroy (ctx->krm);
        flux_watcher_destroy (ctx->prep_w);
        flux_watcher_destroy (ctx->check_w);
        flux_watcher_destroy (ctx->idle_w);
        kvs_checkpoint_destroy (ctx->kcp);
        free (ctx->hash_name);
        zhashx_destroy (&ctx->requests);
        free (ctx);
        errno = saved_errno;
    }
}

static void request_tracking_add (struct kvs_ctx *ctx, const flux_msg_t *msg)
{
    /* ignore if item already tracked */
    zhashx_insert (ctx->requests, msg, (flux_msg_t *)msg);
}

static void request_tracking_remove (struct kvs_ctx *ctx, const flux_msg_t *msg)
{
    zhashx_delete (ctx->requests, msg);
}

static void work_queue_check_append_wrapper (struct kvsroot *root, void *arg)
{
    struct kvs_ctx *ctx = arg;
    work_queue_check_append (ctx, root);
}

static struct kvs_ctx *kvs_ctx_create (flux_t *h)
{
    flux_reactor_t *r = flux_get_reactor (h);
    struct kvs_ctx *ctx;
    const char *s;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    if (!(s = flux_attr_get (h, "content.hash"))
        || !(ctx->hash_name = strdup (s))) {
        flux_log_error (h, "getattr content.hash");
        goto error;
    }
    if (!(ctx->cache = cache_create (r)))
        goto error;
    if (!(ctx->krm = kvsroot_mgr_create (ctx->h, ctx)))
        goto error;
    if (flux_get_rank (ctx->h, &ctx->rank) < 0)
        goto error;
    if (ctx->rank == 0) {
        ctx->prep_w = flux_prepare_watcher_create (r, transaction_prep_cb, ctx);
        if (!ctx->prep_w)
            goto error;
        ctx->check_w = flux_check_watcher_create (r, transaction_check_cb, ctx);
        if (!ctx->check_w)
            goto error;
        ctx->idle_w = flux_idle_watcher_create (r, NULL, NULL);
        if (!ctx->idle_w)
            goto error;
        flux_watcher_start (ctx->prep_w);
        flux_watcher_start (ctx->check_w);
        ctx->kcp = kvs_checkpoint_create (h,
                                          NULL, /* set later */
                                          0.0,  /* default 0.0, set later */
                                          work_queue_check_append_wrapper,
                                          ctx);
        if (!ctx->kcp)
            goto error;
    }
    ctx->transaction_merge = 1;
    if (!(ctx->requests = msg_hash_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        goto error;
    list_head_init (&ctx->work_queue);
    return ctx;
error:
    kvs_ctx_destroy (ctx);
    return NULL;
}

/*
 * event subscribe/unsubscribe
 */

static int event_subscribe (struct kvs_ctx *ctx, const char *ns)
{
    char *topic = NULL;
    int rc = -1;

    /* Below we subscribe to the kvs.namespace or kvs.namespace-<NS>
     * substring depending if we are on rank 0 or rank != 0.
     *
     * This substring encompasses four events at the moment.
     *
     * kvs.namespace-<NS>-setroot
     * kvs.namespace-<NS>-error
     * kvs.namespace-<NS>-removed
     * kvs.namespace-<NS>-created
     *
     * This module publishes all the above events, but only has
     * callbacks for the "setroot", "error", and "removed"
     * events. "created" events are dropped.
     *
     * While dropped events are "bad" performance wise, it is a net
     * win on performance to limit the number of calls to the
     * flux_event_subscribe() function.
     *
     * See issue #2779 for more information.
     */

    /* do not want to subscribe to events that are not within our
     * namespace, so we subscribe to only specific ones.
     */

    if (!(ctx->events_init)) {

        /* These belong to all namespaces, subscribe once the first
         * time we init a namespace */

        if (flux_event_subscribe (ctx->h, "kvs.stats.clear") < 0
            || flux_event_subscribe (ctx->h, "kvs.dropcache") < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }

        /* On rank 0, we need to listen for all of these namespace
         * events, all of the time.  So subscribe to them just once on
         * rank 0. */
        if (ctx->rank == 0) {
            if (flux_event_subscribe (ctx->h, "kvs.namespace") < 0) {
                flux_log_error (ctx->h, "flux_event_subscribe");
                goto cleanup;
            }
        }

        ctx->events_init = true;
    }

    if (ctx->rank != 0) {
        if (asprintf (&topic, "kvs.namespace-%s", ns) < 0)
            goto cleanup;

        if (flux_event_subscribe (ctx->h, topic) < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }
    }

    rc = 0;
cleanup:
    free (topic);
    return rc;
}

static int event_unsubscribe (struct kvs_ctx *ctx, const char *ns)
{
    char *topic = NULL;
    int rc = -1;

    if (ctx->rank != 0) {
        if (asprintf (&topic, "kvs.namespace-%s", ns) < 0)
            goto cleanup;

        if (flux_event_unsubscribe (ctx->h, topic) < 0) {
            flux_log_error (ctx->h, "flux_event_subscribe");
            goto cleanup;
        }
    }

    rc = 0;
cleanup:
    free (topic);
    return rc;
}

/*
 * security
 */

static int check_user (struct kvs_ctx *ctx,
                       struct kvsroot *root,
                       const flux_msg_t *msg)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0) {
        flux_log_error (ctx->h, "flux_msg_get_cred");
        return -1;
    }

    return kvsroot_check_user (ctx->krm, root, cred);
}

/*
 * set/get root
 */

static void setroot (struct kvs_ctx *ctx,
                     struct kvsroot *root,
                     const char *rootref,
                     int rootseq)
{
    if (rootseq == 0 || rootseq > root->seq) {
        kvsroot_setroot (ctx->krm, root, rootref, rootseq);
        kvs_wait_version_process (root, false);
        root->last_update_time = flux_reactor_now (flux_get_reactor (ctx->h));
    }
}

static void getroot_completion (flux_future_t *f, void *arg)
{
    struct kvs_ctx *ctx = arg;
    flux_msg_t *msg = NULL;
    const char *ns;
    int rootseq, flags;
    uint32_t owner;
    const char *ref;
    struct kvsroot *root;
    int save_errno;

    msg = flux_future_aux_get (f, "msg");
    assert (msg);

    /* N.B. owner read into uint32_t */
    if (flux_rpc_get_unpack (f,
                             "{ s:i s:i s:s s:i s:s }",
                             "owner", &owner,
                             "rootseq", &rootseq,
                             "rootref", &ref,
                             "flags", &flags,
                             "namespace", &ns) < 0) {
        if (errno != ENOTSUP)
            flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    /* possible root initialized by another message before we got this
     * response.  Not relevant if namespace in process of being removed. */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, ns))) {

        if (!(root = kvsroot_mgr_create_root (ctx->krm,
                                              ctx->cache,
                                              ctx->hash_name,
                                              ns,
                                              owner,
                                              flags))) {
            flux_log_error (ctx->h, "%s: kvsroot_mgr_create_root", __FUNCTION__);
            goto error;
        }

        if (event_subscribe (ctx, ns) < 0) {
            save_errno = errno;
            kvsroot_mgr_remove_root (ctx->krm, ns);
            errno = save_errno;
            flux_log_error (ctx->h, "%s: event_subscribe", __FUNCTION__);
            goto error;
        }
    }

    /* if root now in process of being removed, error will be handled via
     * the original callback
     */
    if (!root->remove)
        setroot (ctx, root, ref, rootseq);

    if (flux_requeue (ctx->h, msg, FLUX_RQ_HEAD) < 0) {
        flux_log_error (ctx->h, "%s: flux_requeue", __FUNCTION__);
        goto error;
    }

    flux_msg_destroy (msg);
    flux_future_destroy (f);
    return;

error:
    if (flux_respond_error (ctx->h, msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
    /* N.B. getroot request from other requests (e.g. lookup, commit)
     * may stall and be tracked.  So we need to remove tracking of the
     * request if there is an error.  We do not remove tracking on
     * getroot success, as the original request (e.g. lookup, commit)
     * will deal with the success case.
     */
    request_tracking_remove (ctx, msg);
    flux_msg_destroy (msg);
    flux_future_destroy (f);
}

static int getroot_request_send (struct kvs_ctx *ctx,
                                 const char *ns,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 lookup_t *lh)
{
    flux_future_t *f = NULL;
    flux_msg_t *msgcpy = NULL;
    int saved_errno;

    if (!(f = flux_rpc_pack (ctx->h,
                             "kvs.getroot",
                             FLUX_NODEID_UPSTREAM,
                             0,
                             "{ s:s }",
                             "namespace", ns)))
        goto error;

    if (!(msgcpy = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    if (lh
        && flux_msg_aux_set (msgcpy, "lookup_handle", lh, NULL) < 0) {
        flux_log_error (ctx->h, "%s: flux_msg_aux_set", __FUNCTION__);
        goto error;
    }

    /* we will manage destruction of the 'msg' on errors */
    if (flux_future_aux_set (f, "msg", msgcpy, NULL) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_aux_set", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (f, -1., getroot_completion, ctx) < 0)
        goto error;

    return 0;
error:
    saved_errno = errno;
    flux_msg_destroy (msgcpy);
    flux_future_destroy (f);
    errno = saved_errno;
    return -1;
}

static struct kvsroot *getroot (struct kvs_ctx *ctx,
                                const char *ns,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                lookup_t *lh,
                                bool *stall)
{
    struct kvsroot *root;

    (*stall) = false;

    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, ns))) {
        if (ctx->rank == 0) {
            errno = ENOTSUP;
            return NULL;
        }
        else {
            if (getroot_request_send (ctx, ns, mh, msg, lh) < 0) {
                flux_log_error (ctx->h, "getroot_request_send");
                return NULL;
            }
            (*stall) = true;
            return NULL;
        }
    }

    if (check_user (ctx, root, msg) < 0)
        return NULL;

    return root;
}

/*
 * load
 */

static void content_load_cache_entry_error (struct kvs_ctx *ctx,
                                            struct cache_entry *entry,
                                            int errnum,
                                            const char *blobref)
{
    /* failure on load, inform all waiters, attempt to destroy
     * entry afterwards, as future loads may believe a load is in
     * transit.  cache_remove_entry() will not work if a waiter is
     * still there.  If assert hits, it's because we did not set
     * up a wait error cb correctly.
     */
    (void)cache_entry_set_errnum_on_valid (entry, errnum);
    if (cache_remove_entry (ctx->cache, blobref) < 0)
        flux_log (ctx->h, LOG_ERR, "%s: cache_remove_entry", __FUNCTION__);
}

static void content_load_completion (flux_future_t *f, void *arg)
{
    struct kvs_ctx *ctx = arg;
    const void *data;
    int size;
    const char *blobref;
    struct cache_entry *entry;

    blobref = flux_future_aux_get (f, "ref");

    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it is not yet valid.  But check and log incase there is
     * logic error dealng with error paths using cache_remove_entry().
     */
    if (!(entry = cache_lookup (ctx->cache, blobref))) {
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto done;
    }

    if (content_load_get (f, &data, &size) < 0) {
        flux_log_error (ctx->h, "%s: content_load_get", __FUNCTION__);
        content_load_cache_entry_error (ctx, entry, errno, blobref);
        goto done;
    }

    /* If cache_entry_set_raw() fails, it's a pretty terrible error
     * case, where we've loaded an object from the content store, but
     * can't put it in the cache.
     */
    if (cache_entry_set_raw (entry, data, size) < 0) {
        flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
        content_load_cache_entry_error (ctx, entry, errno, blobref);
        goto done;
    }

done:
    flux_future_destroy (f);
}

/* Send content load request and setup continuation to handle response.
 */
static int content_load_request_send (struct kvs_ctx *ctx, const char *ref)
{
    flux_future_t *f = NULL;
    char *refcpy;
    int saved_errno;

    if (!(f = content_load_byblobref (ctx->h, ref, 0))) {
        flux_log_error (ctx->h, "%s: content_load_byblobref", __FUNCTION__);
        goto error;
    }
    if (!(refcpy = strdup (ref)))
        goto error;
    if (flux_future_aux_set (f, "ref", refcpy, free) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_aux_set", __FUNCTION__);
        free (refcpy);
        goto error;
    }
    if (flux_future_then (f, -1., content_load_completion, ctx) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }
    return 0;
error:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return -1;
}

/* Return 0 on success, -1 on error.  Set stall variable appropriately
 */
static int load (struct kvs_ctx *ctx,
                 const char *ref,
                 wait_t *wait,
                 bool *stall)
{
    struct cache_entry *entry = cache_lookup (ctx->cache, ref);
    int saved_errno;
    __attribute__((unused)) int ret;

    assert (wait != NULL);

    /* Create an incomplete hash entry if none found.
     */
    if (!entry) {
        if (!(entry = cache_entry_create (ref))) {
            flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
            return -1;
        }
        if (cache_insert (ctx->cache, entry) < 0) {
            flux_log_error (ctx->h, "%s: cache_insert", __FUNCTION__);
            cache_entry_destroy (entry);
            return -1;
        }
        if (content_load_request_send (ctx, ref) < 0) {
            saved_errno = errno;
            flux_log_error (ctx->h,
                            "%s: content_load_request_send",
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
        /* Potential future optimization, if this load() is called
         * multiple times from the same kvstxn and on the same
         * reference, we're effectively adding identical waiters onto
         * this cache entry.  This is far better than sending multiple
         * RPCs (the cache entry check above protects against this),
         * but could be improved later.  See Issue #1751.
         */
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
 * store/write
 */

static void content_store_completion (flux_future_t *f, void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct cache_entry *entry;
    const char *cache_blobref, *blobref;
    __attribute__((unused)) int ret;

    cache_blobref = flux_future_aux_get (f, "cache_blobref");
    assert (cache_blobref);

    if (content_store_get_blobref (f, ctx->hash_name, &blobref) < 0) {
        flux_log_error (ctx->h, "%s: content_store_get_blobref", __FUNCTION__);
        goto error;
    }

    /* Double check that content store stored in the same blobref
     * location we calculated.
     * N.B. perhaps this check is excessive and could be removed
     */
    if (!streq (blobref, cache_blobref)) {
        flux_log (ctx->h,
                  LOG_ERR, "%s: inconsistent blobref returned",
                  __FUNCTION__);
        errno = EPROTO;
        goto error;
    }

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    /* should be impossible for lookup to fail, cache entry created
     * earlier, and cache_expire_entries() could not have removed it
     * b/c it was dirty.  But check and log incase there is logic
     * error dealng with error paths using cache_remove_entry().
     */
    if (!(entry = cache_lookup (ctx->cache, blobref))) {
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto error;
    }

    /* This is a pretty terrible error case, where we've received
     * verification that a dirty cache entry has been flushed to the
     * content store, but we can't notify waiters that it has been
     * flushed.  In addition, if we can't notify waiters by clearing
     * the dirty bit, what are the odds the error handling below would
     * work as well.
     */
    if (cache_entry_set_dirty (entry, false) < 0) {
        flux_log_error (ctx->h, "%s: cache_entry_set_dirty", __FUNCTION__);
        goto error;
    }

    flux_future_destroy (f);
    return;

error:
    flux_future_destroy (f);

    /* failure on store, inform all waiters, must destroy entry
     * afterwards, as future loads/stores may believe content is ok.
     * cache_remove_entry() will not work if a waiter is still there.
     * If assert hits, it's because we did not set up a wait error cb
     * correctly.
     */

    /* we can't do anything if this cache_lookup fails */
    if (!(entry = cache_lookup (ctx->cache, cache_blobref))) {
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        return;
    }

    /* In the case this fails, we'll mark the cache entry not dirty,
     * so that memory can be reclaimed at a later time.  But we can't
     * do that with cache_entry_clear_dirty() b/c that will only clear
     * dirty for entries without waiters.  So in this rare case, we
     * must call cache_entry_force_clear_dirty().  flushed.
     */
    if (cache_entry_set_errnum_on_notdirty (entry, errno) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s: cache_entry_set_errnum_on_notdirty",
                  __FUNCTION__);
        ret = cache_entry_force_clear_dirty (entry);
        assert (ret == 0);
        return;
    }

    /* this can't fail, otherwise we shouldn't be in this function */
    ret = cache_entry_force_clear_dirty (entry);
    assert (ret == 0);

    if (cache_remove_entry (ctx->cache, cache_blobref) < 0)
        flux_log (ctx->h, LOG_ERR, "%s: cache_remove_entry", __FUNCTION__);
}

static int content_store_request_send (struct kvs_ctx *ctx,
                                       const char *blobref,
                                       const void *data,
                                       int len)
{
    flux_future_t *f;
    int saved_errno, rc = -1;

    if (!(f = content_store (ctx->h, data, len, 0)))
        goto error;
    if (flux_future_aux_set (f, "cache_blobref", (void *)blobref, NULL) < 0) {
        saved_errno = errno;
        flux_future_destroy (f);
        errno = saved_errno;
        goto error;
    }
    if (flux_future_then (f, -1., content_store_completion, ctx) < 0) {
        saved_errno = errno;
        flux_future_destroy (f);
        errno = saved_errno;
        goto error;
    }

    rc = 0;
error:
    return rc;
}

static int kvstxn_load_cb (kvstxn_t *kt, const char *ref, void *data)
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
 * object's wait queue.
 */
static int kvstxn_cache_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    struct kvs_cb_data *cbd = data;
    const char *blobref;
    const void *storedata;
    int storedatalen = 0;

    assert (cache_entry_get_dirty (entry));

    if (cache_entry_get_raw (entry, &storedata, &storedatalen) < 0) {
        flux_log_error (cbd->ctx->h,
                        "%s: cache_entry_get_raw",
                        __FUNCTION__);
        kvstxn_cleanup_dirty_cache_entry (kt, entry);
        return -1;
    }

    /* must be true, otherwise we didn't insert entry in cache */
    blobref = cache_entry_get_blobref (entry);
    assert (blobref);

    if (content_store_request_send (cbd->ctx,
                                    blobref,
                                    storedata,
                                    storedatalen) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h,
                        "%s: content_store_request_send",
                        __FUNCTION__);
        kvstxn_cleanup_dirty_cache_entry (kt, entry);
        return -1;
    }
    if (cache_entry_wait_notdirty (entry, cbd->wait) < 0) {
        cbd->errnum = errno;
        flux_log_error (cbd->ctx->h, "cache_entry_wait_notdirty");
        kvstxn_cleanup_dirty_cache_entry (kt, entry);
        return -1;
    }
    return 0;
}

static int setroot_event_send (struct kvs_ctx *ctx,
                               struct kvsroot *root,
                               json_t *names,
                               json_t *keys)
{
    flux_msg_t *msg = NULL;
    char *setroot_topic = NULL;
    int saved_errno, rc = -1;

    assert (ctx->rank == 0);

    if (asprintf (&setroot_topic,
                  "kvs.namespace-%s-setroot",
                  root->ns_name) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: asprintf", __FUNCTION__);
        goto done;
    }

    if (!(msg = flux_event_pack (setroot_topic,
                                 "{ s:s s:i s:s s:O s:O s:i}",
                                 "namespace", root->ns_name,
                                 "rootseq", root->seq,
                                 "rootref", root->ref,
                                 "names", names,
                                 "keys", keys,
                                 "owner", root->owner))) {
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
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static int error_event_send (struct kvs_ctx *ctx,
                             const char *ns,
                             json_t *names,
                             int errnum)
{
    flux_msg_t *msg = NULL;
    char *error_topic = NULL;
    int saved_errno, rc = -1;

    if (asprintf (&error_topic, "kvs.namespace-%s-error", ns) < 0) {
        saved_errno = errno;
        flux_log_error (ctx->h, "%s: asprintf", __FUNCTION__);
        goto done;
    }

    if (!(msg = flux_event_pack (error_topic,
                                 "{ s:s s:O s:i }",
                                 "namespace", ns,
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

static int error_event_send_to_name (struct kvs_ctx *ctx,
                                     const char *ns,
                                     const char *name,
                                     int errnum)
{
    json_t *names = NULL;
    int rc = -1;

    if (!(names = json_pack ("[ s ]", name))) {
        errno = ENOMEM;
        flux_log_error (ctx->h, "%s: json_pack", __FUNCTION__);
        goto done;
    }

    rc = error_event_send (ctx, ns, names, errnum);
done:
    json_decref (names);
    return rc;
}

static void kvstxn_wait_error_cb (wait_t *w, int errnum, void *arg)
{
    kvstxn_t *kt = arg;
    kvstxn_set_aux_errnum (kt, errnum);
}

/* ccan list doesn't appear to have a macro to check if a node exists
 * on a list */
static inline bool root_on_work_queue (struct list_node *n)
{
    return !(n->next == n->prev && n->next == n);
}

static void work_queue_append (struct kvs_ctx *ctx, struct kvsroot *root)
{
    if (!root_on_work_queue (&root->work_queue_node))
        list_add_tail (&ctx->work_queue, &root->work_queue_node);
}

static void work_queue_remove (struct kvsroot *root)
{
    list_del_init (&root->work_queue_node);
}

static void work_queue_check_append (struct kvs_ctx *ctx, struct kvsroot *root)
{
    if (kvstxn_mgr_transaction_ready (root->ktm))
        work_queue_append (ctx, root);
}

static void kvstxn_apply_cb (flux_future_t *f, void *arg)
{
    kvstxn_t *kt = arg;
    kvstxn_apply (kt);
}

/* Write all the ops for a particular commit/fence request (rank 0
 * only).  The setroot event will cause responses to be sent to the
 * transaction requests and clean up the treq_t state.  This
 * function is idempotent.
 */
static void kvstxn_apply (kvstxn_t *kt)
{
    struct kvs_ctx *ctx = kvstxn_get_aux (kt);
    const char *ns;
    struct kvsroot *root = NULL;
    wait_t *wait = NULL;
    int errnum = 0;
    kvstxn_process_t ret;
    bool fallback = false;

    ns = kvstxn_get_namespace (kt);
    assert (ns);

    /* Between call to kvstxn_mgr_add_transaction() and here, possible
     * namespace marked for removal.  Also namespace could have been
     * removed if we waited and this is a replay.
     *
     * root should never be NULL, as it should not be garbage
     * collected until all ready transactions have been processed.
     */

    root = kvsroot_mgr_lookup_root (ctx->krm, ns);
    assert (root);

    if (root->remove) {
        errnum = ENOTSUP;
        goto done;
    }

    if ((errnum = kvstxn_get_aux_errnum (kt)))
        goto done;

    if ((ret = kvstxn_process (kt,
                               root->ref,
                               root->seq)) == KVSTXN_PROCESS_ERROR) {
        errnum = kvstxn_get_errnum (kt);
        goto done;
    }

    if (ret == KVSTXN_PROCESS_LOAD_MISSING_REFS) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create ((wait_cb_f)kvstxn_apply, kt))) {
            errnum = errno;
            goto done;
        }

        if (wait_set_error_cb (wait, kvstxn_wait_error_cb, kt) < 0)
            goto done;

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (kvstxn_iter_missing_refs (kt, kvstxn_load_cb, &cbd) < 0) {
            errnum = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                kvstxn_set_aux_errnum (kt, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
        goto stall;
    }
    else if (ret == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create ((wait_cb_f)kvstxn_apply, kt))) {
            errnum = errno;
            goto done;
        }

        if (wait_set_error_cb (wait, kvstxn_wait_error_cb, kt) < 0)
            goto done;

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (kvstxn_iter_dirty_cache_entries (kt, kvstxn_cache_cb, &cbd) < 0) {
            errnum = cbd.errnum;

            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                kvstxn_set_aux_errnum (kt, cbd.errnum);
                goto stall;
            }

            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
        goto stall;
    }
    else if (ret == KVSTXN_PROCESS_SYNC_CONTENT_FLUSH) {
        /* N.B. futre is managed by kvstxn, should not call
         * flux_future_destroy() on it */
        flux_future_t *f = kvstxn_sync_content_flush (kt);
        if (!f) {
            errnum = errno;
            goto done;
        }
        if (flux_future_then (f, -1., kvstxn_apply_cb, kt) < 0) {
            errnum = errno;
            goto done;
        }
        goto stall;
    }
    else if (ret == KVSTXN_PROCESS_SYNC_CHECKPOINT) {
        /* N.B. futre is managed by kvstxn, should not call
         * flux_future_destroy() on it */
        flux_future_t *f = kvstxn_sync_checkpoint (kt);
        if (!f) {
            errnum = errno;
            goto done;
        }
        if (flux_future_then (f, -1., kvstxn_apply_cb, kt) < 0) {
            errnum = errno;
            goto done;
        }
        goto stall;
    }
    /* else ret == KVSTXN_PROCESS_FINISHED */

    /* This finalizes the transaction by replacing root->ref with
     * newroot, incrementing root->seq, and sending out the setroot
     * event for "eventual consistency" of other nodes.
     */
done:
    if (errnum == 0) {
        json_t *names = kvstxn_get_names (kt);
        int internal_flags = kvstxn_get_internal_flags (kt);
        int count;
        if ((count = json_array_size (names)) > 1) {
            int opcount = 0;
            opcount = json_array_size (kvstxn_get_ops (kt));
            flux_log (ctx->h,
                      LOG_DEBUG,
                      "aggregated %d transactions (%d ops)",
                      count,
                      opcount);
        }
        if (!(internal_flags & KVSTXN_INTERNAL_FLAG_NO_PUBLISH)) {
            setroot (ctx, root, kvstxn_get_newroot_ref (kt), root->seq + 1);
            setroot_event_send (ctx, root, names, kvstxn_get_keys (kt));
        }
    }
    else {
        fallback = kvstxn_fallback_mergeable (kt);

        /* if merged transaction is fallbackable, ignore the fallback option
         * if it's an extreme "death" like error.
         */
        if (errnum == ENOMEM || errnum == ENOTSUP)
            fallback = false;

        if (!fallback) {
            error_event_send (ctx,
                              root->ns_name,
                              kvstxn_get_names (kt),
                              errnum);
        }
    }
    wait_destroy (wait);

    /* Completed: remove from 'ready' list.
     * N.B. treq_t remains in the treq_mgr_t hash until event is received.
     */
    kvstxn_mgr_remove_transaction (root->ktm, kt, fallback);

stall:
    if (kvstxn_mgr_transaction_ready (root->ktm))
        work_queue_append (ctx, root);
    else
        work_queue_remove (root);
    return;
}

/*
 * pre/check event callbacks
 */

static void transaction_prep_cb (flux_reactor_t *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg)
{
    struct kvs_ctx *ctx = arg;

    if (!list_empty (&ctx->work_queue))
        flux_watcher_start (ctx->idle_w);
}

static void kvstxn_check_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_ctx *ctx = arg;
    kvstxn_t *kt;

    if ((kt = kvstxn_mgr_get_ready_transaction (root->ktm))) {
        if (ctx->transaction_merge) {
            /* if merge fails, set errnum in kvstxn_t, let
             * kvstxn_apply() handle error handling.
             */
            if (kvstxn_mgr_merge_ready_transactions (root->ktm) < 0)
                kvstxn_set_aux_errnum (kt, errno);
            else {
                /* grab new head ready commit, if above succeeds, this
                 * must succeed */
                kt = kvstxn_mgr_get_ready_transaction (root->ktm);
                assert (kt);
            }
        }

        /* It does not matter if root has been marked for removal,
         * we want to process and clear all lingering ready
         * transactions in this kvstxn manager
         */
        kvstxn_apply (kt);
    }
}

static void transaction_check_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root = NULL;
    struct kvsroot *next = NULL;

    flux_watcher_stop (ctx->idle_w);

    list_for_each_safe (&ctx->work_queue, root, next, work_queue_node)
        kvstxn_check_root_cb (root, ctx);
}

/*
 * rpc/event callbacks
 */

static void dropcache_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                  const flux_msg_t *msg, void *arg)
{
    struct kvs_ctx *ctx = arg;
    int size, expcount = 0;

    /* irrelevant if root not initialized, drop cache entries */

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    size = cache_count_entries (ctx->cache);
    if ((expcount = cache_expire_entries (ctx->cache, 0)) < 0) {
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
        goto error;
    }
    else {
        flux_log (h,
                  LOG_ALERT,
                  "dropped %d of %d cache entries",
                  expcount,
                  size);
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void dropcache_event_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct kvs_ctx *ctx = arg;
    int size, expcount = 0;

    /* irrelevant if root not initialized, drop cache entries */

    if (flux_event_decode (msg, NULL, NULL) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        return;
    }
    size = cache_count_entries (ctx->cache);
    if ((expcount = cache_expire_entries (ctx->cache, 0)) < 0)
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);
    else {
        flux_log (h,
                  LOG_ALERT,
                  "dropped %d of %d cache entries",
                  expcount,
                  size);
    }
}

static int heartbeat_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_ctx *ctx = arg;
    double now = flux_reactor_now (flux_get_reactor (ctx->h));

    if (root->remove) {
        if (!zlist_size (root->wait_version_list)
            && !treq_mgr_transactions_count (root->trm)
            && !kvstxn_mgr_ready_transaction_count (root->ktm)) {

            if (event_unsubscribe (ctx, root->ns_name) < 0)
                flux_log_error (ctx->h, "%s: event_unsubscribe", __FUNCTION__);

            if (kvsroot_mgr_remove_root (ctx->krm, root->ns_name) < 0) {
                flux_log_error (ctx->h,
                                "%s: kvsroot_mgr_remove_root",
                                __FUNCTION__);
            }
        }
    }
    else if (ctx->rank != 0
        && !root->remove
        && !root->is_primary
        && (now - root->last_update_time) > max_namespace_age
        && !zlist_size (root->wait_version_list)
        && !treq_mgr_transactions_count (root->trm)
        && !kvstxn_mgr_ready_transaction_count (root->ktm)) {
        /* remove a root if it not the primary one, has timed out
         * on a follower node, and it does not have any watchers,
         * and no one is trying to write/change something.
         */
        start_root_remove (ctx, root->ns_name);
    }
    else /* "touch" root */
        (void)cache_lookup (ctx->cache, root->ref);

    return 0;
}

static void heartbeat_sync_cb (flux_future_t *f, void *arg)
{
    struct kvs_ctx *ctx = arg;

    /* don't error return, fallthrough to deal with rest as necessary */
    if (kvsroot_mgr_iter_roots (ctx->krm, heartbeat_root_cb, ctx) < 0)
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);

    if (cache_expire_entries (ctx->cache, max_lastuse_age) < 0)
        flux_log_error (ctx->h, "%s: cache_expire_entries", __FUNCTION__);

    flux_future_reset (f);
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

static void lookup_wait_error_cb (wait_t *w, int errnum, void *arg)
{
    lookup_t *lh = arg;
    lookup_set_aux_errnum (lh, errnum);
}

static lookup_t *lookup_common (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                struct kvs_ctx *ctx,
                                flux_msg_handler_f replay_cb,
                                bool *stall)
{
    int flags;
    const char *ns = NULL;
    const char *key;
    json_t *val = NULL;
    json_t *root_dirent = NULL;
    lookup_t *lh = NULL;
    const char *root_ref = NULL;
    wait_t *wait = NULL;
    lookup_process_t lret;
    int rc = -1;

    /* if lookup_handle exists in msg as aux data, is a replay */
    lh = flux_msg_aux_get (msg, "lookup_handle");
    if (!lh) {
        struct flux_msg_cred cred;
        int root_seq = -1;

        /* namespace, rootdir, and rootseq optional */
        if (flux_request_unpack (msg,
                                 NULL,
                                 "{ s:s s:i s?s s?o s?i}",
                                 "key", &key,
                                 "flags", &flags,
                                 "namespace", &ns,
                                 "rootdir", &root_dirent,
                                 "rootseq", &root_seq) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        /* either namespace or rootdir must be specified */
        if (!ns && !root_dirent) {
            errno = EPROTO;
            goto done;
        }

        /* If root dirent was specified, lookup corresponding
         * 'root' directory.  Otherwise, use the current root.
         */
        if (root_dirent) {
            if (treeobj_validate (root_dirent) < 0
                || !treeobj_is_dirref (root_dirent)
                || !(root_ref = treeobj_get_blobref (root_dirent, 0))) {
                errno = EINVAL;
                goto done;
            }
        }

        if (flux_msg_get_cred (msg, &cred) < 0) {
            flux_log_error (ctx->h, "flux_msg_get_cred");
            goto done;
        }

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->krm,
                                  ns,
                                  root_ref,
                                  root_seq,
                                  key,
                                  cred,
                                  flags,
                                  h)))
            goto done;
    }
    else {
        int err;

        /* error in prior load(), waited for in flight rpcs to complete */
        if ((err = lookup_get_aux_errnum (lh))) {
            errno = err;
            goto done;
        }
    }

    lret = lookup (lh);

    if (lret == LOOKUP_PROCESS_ERROR) {
        errno = lookup_get_errnum (lh);
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE) {
        bool stall = false;
        __attribute__((unused)) struct kvsroot *root;

        ns = lookup_missing_namespace (lh);
        assert (ns);

        root = getroot (ctx, ns, mh, msg, lh, &stall);
        assert (!root);

        if (stall)
            goto stall;
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_REFS) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create_msg_handler (h, mh, msg, ctx, replay_cb)))
            goto done;

        if (wait_set_error_cb (wait, lookup_wait_error_cb, lh) < 0)
            goto done;

        /* do not destroy lookup_handle on message destruction, we
         * manage it in here */
        if (wait_msg_aux_set (wait, "lookup_handle", lh, NULL) < 0)
            goto done;

        cbd.ctx = ctx;
        cbd.wait = wait;
        cbd.errnum = 0;

        if (lookup_iter_missing_refs (lh, lookup_load_cb, &cbd) < 0) {
            /* rpcs already in flight, stall for them to complete */
            if (wait_get_usecount (wait) > 0) {
                lookup_set_aux_errnum (lh, cbd.errnum);
                goto stall;
            }

            errno = cbd.errnum;
            goto done;
        }

        assert (wait_get_usecount (wait) > 0);
        goto stall;
    }
    /* else lret == LOOKUP_PROCESS_FINISHED, fallthrough */

    rc = 0;
done:
    wait_destroy (wait);
    if (rc < 0) {
        lookup_destroy (lh);
        json_decref (val);
    }
    (*stall) = false;
    return (rc == 0) ? lh : NULL;

stall:
    (*stall) = true;
    return NULL;
}

static void lookup_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct kvs_ctx *ctx = arg;
    lookup_t *lh;
    json_t *val;
    bool stall = false;

    if (!(lh = lookup_common (h, mh, msg, ctx, lookup_request_cb, &stall))) {
        if (stall) {
            request_tracking_add (ctx, msg);
            return;
        }
        goto error;
    }

    if (!(val = lookup_get_value (lh))) {
        errno = ENOENT;
        goto error;
    }
    if (flux_respond_pack (h, msg, "{ s:O }", "val", val) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    lookup_destroy (lh);
    json_decref (val);
    request_tracking_remove (ctx, msg);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
    lookup_destroy (lh);
}

/* similar to kvs.lookup, but root_ref / root_seq returned to caller.
 * Also, ENOENT handle special case, returned as error number to
 * caller.  This request is a special rpc predominantly used by the
 * kvs-watch module.  The kvs-watch module requires root information
 * on lookups (including ENOENT failed lookups) to determine what
 * lookups can be considered to be read-your-writes consistency safe.
 */
static void lookup_plus_request_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct kvs_ctx *ctx = arg;
    lookup_t *lh;
    json_t *val = NULL;
    const char *root_ref;
    int root_seq;
    bool stall = false;

    if (!(lh = lookup_common (h,
                              mh,
                              msg,
                              ctx,
                              lookup_plus_request_cb,
                              &stall))) {
        if (stall) {
            request_tracking_add (ctx, msg);
            return;
        }
        goto error;
    }

    root_ref = lookup_get_root_ref (lh);
    assert (root_ref);
    root_seq = lookup_get_root_seq (lh);
    assert (root_seq >= 0);

    if (!(val = lookup_get_value (lh))) {
        if (flux_respond_pack (h,
                               msg,
                               "{ s:i s:i s:s }",
                               "errno", ENOENT,
                               "rootseq", root_seq,
                               "rootref", root_ref) < 0)
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    }
    else {
        if (flux_respond_pack (h,
                               msg,
                               "{ s:O s:i s:s }",
                               "val", val,
                               "rootseq", root_seq,
                               "rootref", root_ref) < 0)
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    }
    lookup_destroy (lh);
    json_decref (val);
    request_tracking_remove (ctx, msg);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
}


static int finalize_transaction_req (treq_t *tr,
                                     const flux_msg_t *req,
                                     void *data)
{
    struct kvs_cb_data *cbd = data;

    if (cbd->errnum) {
        if (flux_respond_error (cbd->ctx->h, req, cbd->errnum, NULL) < 0) {
            flux_log_error (cbd->ctx->h,
                            "%s: flux_respond_error",
                            __FUNCTION__);
        }
    }
    else {
        if (flux_respond_pack (cbd->ctx->h,
                               req,
                               "{ s:s s:i }",
                               "rootref", cbd->root->ref,
                               "rootseq", cbd->root->seq) < 0)
            flux_log_error (cbd->ctx->h, "%s: flux_respond_pack", __FUNCTION__);
    }

    request_tracking_remove (cbd->ctx, req);
    return 0;
}

static void finalize_transaction_bynames (struct kvs_ctx *ctx,
                                          struct kvsroot *root,
                                          json_t *names, int errnum)
{
    int i, len;
    json_t *name;
    treq_t *tr;
    struct kvs_cb_data cbd = { .ctx = ctx, .root = root, .errnum = errnum };

    if (!(len = json_array_size (names))) {
        flux_log_error (ctx->h, "%s: parsing array", __FUNCTION__);
        return;
    }
    for (i = 0; i < len; i++) {
        const char *nameval;
        if (!(name = json_array_get (names, i))) {
            flux_log_error (ctx->h, "%s: parsing array[%d]", __FUNCTION__, i);
            return;
        }
        nameval = json_string_value (name);
        if ((tr = treq_mgr_lookup_transaction (root->trm, nameval))) {
            treq_iter_request_copies (tr, finalize_transaction_req, &cbd);
            if (treq_mgr_remove_transaction (root->trm, nameval) < 0) {
                flux_log_error (ctx->h,
                                "%s: treq_mgr_remove_transaction",
                                __FUNCTION__);
            }
        }
    }
}

static int guest_treeobj_authorize (json_t *treeobj, flux_error_t *error)
{
    if (json_is_null (treeobj)
        || treeobj_is_val (treeobj)
        || (treeobj_is_dir (treeobj) && treeobj_get_count (treeobj) == 0))
        return 0;

    const char *type = treeobj_get_type (treeobj);
    errprintf (error,
               "guests may not commit %s%s objects",
               treeobj_is_dir (treeobj) ? "non-empty " : "",
               type ? type : "???");
    errno = EPERM;
    return -1;
}

static int guest_commit_authorize (json_t *ops, flux_error_t *error)
{
    size_t index;
    json_t *op;

    json_array_foreach (ops, index, op) {
        json_t *treeobj;
        if (txn_decode_op (op, NULL, NULL, &treeobj) < 0) {
            errprintf (error, "could not decode commit operation");
            return -1;
        }
        if (guest_treeobj_authorize (treeobj, error) < 0)
            return -1;
    }
    return 0;
}

/* kvs.relaycommit (rank 0 only, no response).
 */
static void relaycommit_request_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    const char *name;
    int flags;
    json_t *ops = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:o s:s s:s s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &ns,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }

    /* namespace must exist given we are on rank 0 */
    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, ns))) {
        errno = ENOTSUP;
        goto error;
    }

    if (kvstxn_mgr_add_transaction (root->ktm, name, ops, flags, 0) < 0) {
        flux_log_error (h, "%s: kvstxn_mgr_add_transaction", __FUNCTION__);
        goto error;
    }

    /* N.B. no request tracking for relay.  The relay does not get a
     * response, only the original via finalize_transaction_bynames().
     */
    work_queue_check_append (ctx, root);
    return;

error:
    /* An error has occurred, so we will return an error similarly to
     * how an error would be returned via a transaction error in
     * kvstxn_apply().
     */
    if (error_event_send_to_name (ctx, ns, name, errno) < 0)
        flux_log_error (h, "%s: error_event_send_to_name", __FUNCTION__);
}

/* kvs.commit
 * Sent from users to local kvs module.
 */
static void commit_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    int saved_errno, flags;
    bool stall = false;
    json_t *ops = NULL;
    treq_t *tr;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:o s:s s:i }",
                             "ops", &ops,
                             "namespace", &ns,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0
        && guest_commit_authorize (ops, &error) < 0) {
        errmsg = error.text;
        goto error;
    }

    if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
        if (stall) {
            request_tracking_add (ctx, msg);
            return;
        }
        goto error;
    }

    if (!(tr = treq_create_rank (ctx->rank, ctx->seq++, 1, flags))) {
        flux_log_error (h, "%s: treq_create_rank", __FUNCTION__);
        goto error;
    }
    if (treq_mgr_add_transaction (root->trm, tr) < 0) {
        saved_errno = errno;
        flux_log_error (h, "%s: treq_mgr_add_transaction", __FUNCTION__);
        treq_destroy (tr);
        errno = saved_errno;
        goto error;
    }

    /* save copy of request, will be used later via
     * finalize_transaction_bynames() to send error code to original
     * send.
     */
    if (treq_add_request_copy (tr, msg) < 0)
        goto error;

    if (ctx->rank == 0) {
        /* we use this flag to indicate if a treq has been added to
         * the ready queue.  We don't need to call
         * treq_count_reached() b/c this is a commit and nprocs is 1
         */
        treq_set_processed (tr, true);

        if (kvstxn_mgr_add_transaction (root->ktm,
                                        treq_get_name (tr),
                                        ops,
                                        flags,
                                        0) < 0) {
            flux_log_error (h, "%s: kvstxn_mgr_add_transaction",
                            __FUNCTION__);
            goto error;
        }

        work_queue_check_append (ctx, root);
    }
    else {
        flux_future_t *f;

        /* route to rank 0 as instance owner */
        if (!(f = flux_rpc_pack (h,
                                 "kvs.relaycommit",
                                 0,
                                 FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:s s:i }",
                                 "ops", ops,
                                 "name", treq_get_name (tr),
                                 "namespace", ns,
                                 "flags", flags))) {
            flux_log_error (h, "%s: flux_rpc_pack", __FUNCTION__);
            goto error;
        }
        flux_future_destroy (f);
    }
    request_tracking_add (ctx, msg);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
}


/* kvs.relayfence (rank 0 only, no response).
 */
static void relayfence_request_cb (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    const char *name;
    int saved_errno, nprocs, flags;
    json_t *ops = NULL;
    treq_t *tr;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:o s:s s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &ns,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }

    /* namespace must exist given we are on rank 0 */
    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, ns))) {
        errno = ENOTSUP;
        goto error;
    }

    if (!(tr = treq_mgr_lookup_transaction (root->trm, name))) {
        if (!(tr = treq_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: treq_create", __FUNCTION__);
            goto error;
        }
        if (treq_mgr_add_transaction (root->trm, tr) < 0) {
            saved_errno = errno;
            flux_log_error (h, "%s: treq_mgr_add_transaction", __FUNCTION__);
            treq_destroy (tr);
            errno = saved_errno;
            goto error;
        }
    }

    if (treq_get_flags (tr) != flags
        || treq_get_nprocs (tr) != nprocs) {
        errno = EINVAL;
        goto error;
    }

    if (treq_add_request_ops (tr, ops) < 0) {
        flux_log_error (h, "%s: treq_add_request_ops", __FUNCTION__);
        goto error;
    }

    if (treq_count_reached (tr)) {

        /* If user called fence > nprocs time, should have been caught
         * earlier */
        assert (!treq_get_processed (tr));

        /* we use this flag to indicate if a treq has been added to
         * the ready queue */
        treq_set_processed (tr, true);

        if (kvstxn_mgr_add_transaction (root->ktm,
                                        treq_get_name (tr),
                                        treq_get_ops (tr),
                                        treq_get_flags (tr),
                                        0) < 0) {
            flux_log_error (h, "%s: kvstxn_mgr_add_transaction", __FUNCTION__);
            goto error;
        }

        work_queue_check_append (ctx, root);
    }

    /* N.B. no request tracking for relay.  The relay does not get a
     * response, only the original via finalize_transaction_bynames().
     */
    return;

error:
    /* An error has occurred, so we will return an error similarly to
     * how an error would be returned via a transaction error in
     * kvstxn_apply().
     */
    if (error_event_send_to_name (ctx, ns, name, errno) < 0)
        flux_log_error (h, "%s: error_event_send_to_name", __FUNCTION__);
}

/* kvs.fence
 * Sent from users to local kvs module.
 */
static void fence_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    const char *name;
    int saved_errno, nprocs, flags;
    bool stall = false;
    json_t *ops = NULL;
    treq_t *tr;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:o s:s s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &ns,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0
        && guest_commit_authorize (ops, &error) < 0) {
        errno = EPERM;
        errmsg = error.text;
        goto error;
    }

    if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
        if (stall) {
            request_tracking_add (ctx, msg);
            goto stall;
        }
        goto error;
    }

    if (!(tr = treq_mgr_lookup_transaction (root->trm, name))) {
        if (!(tr = treq_create (name, nprocs, flags))) {
            flux_log_error (h, "%s: treq_create", __FUNCTION__);
            goto error;
        }
        if (treq_mgr_add_transaction (root->trm, tr) < 0) {
            saved_errno = errno;
            flux_log_error (h, "%s: treq_mgr_add_transaction", __FUNCTION__);
            treq_destroy (tr);
            errno = saved_errno;
            goto error;
        }
    }

    if (treq_get_flags (tr) != flags
        || treq_get_nprocs (tr) != nprocs) {
        errno = EINVAL;
        goto error;
    }

    /* save copy of request, will be used later via
     * finalize_transaction_bynames() to send error code to original
     * send.
     */
    if (treq_add_request_copy (tr, msg) < 0)
        goto error;

    /* If we happen to be on rank 0, perform equivalent of
     * relayfence_request_cb() here instead of sending an RPC
     */
    if (ctx->rank == 0) {

        if (treq_add_request_ops (tr, ops) < 0) {
            flux_log_error (h, "%s: treq_add_request_ops", __FUNCTION__);
            goto error;
        }

        if (treq_count_reached (tr)) {

            /* If user called fence > nprocs time, should have been caught
             * earlier */
            assert (!treq_get_processed (tr));

            /* we use this flag to indicate if a treq has been added to
             * the ready queue */
            treq_set_processed (tr, true);

            if (kvstxn_mgr_add_transaction (root->ktm,
                                            treq_get_name (tr),
                                            treq_get_ops (tr),
                                            treq_get_flags (tr),
                                            0) < 0) {
                flux_log_error (h,
                                "%s: kvstxn_mgr_add_transaction",
                                __FUNCTION__);
                goto error;
            }

            work_queue_check_append (ctx, root);
        }
    }
    else {
        flux_future_t *f;

        /* route to rank 0 as instance owner */
        if (!(f = flux_rpc_pack (h,
                                 "kvs.relayfence",
                                 0,
                                 FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:s s:i s:i }",
                                 "ops", ops,
                                 "name", name,
                                 "namespace", ns,
                                 "flags", flags,
                                 "nprocs", nprocs))) {
            flux_log_error (h, "%s: flux_rpc_pack", __FUNCTION__);
            goto error;
        }
        flux_future_destroy (f);
    }
    request_tracking_add (ctx, msg);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
stall:
    return;
}

static void wait_version_request_cb (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns;
    struct kvsroot *root;
    int rootseq;
    bool stall = false;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s:s }",
                             "rootseq", &rootseq,
                             "namespace", &ns) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
        if (stall) {
            request_tracking_add (ctx, msg);
            return;
        }
        goto error;
    }

    if (root->seq < rootseq) {
        if (kvs_wait_version_add (root,
                                  wait_version_request_cb,
                                  h,
                                  mh,
                                  msg,
                                  ctx,
                                  rootseq) < 0) {
            flux_log_error (h, "%s: kvs_wait_version_add", __FUNCTION__);
            goto error;
        }
        request_tracking_add (ctx, msg);
        return; /* stall */
    }

    if (flux_respond_pack (h,
                           msg,
                           "{ s:i s:s }",
                           "rootseq", root->seq,
                           "rootref", root->ref) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    request_tracking_remove (ctx, msg);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
}

static void getroot_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                const flux_msg_t *msg, void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns;
    struct kvsroot *root;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "namespace", &ns) < 0) {
        flux_log_error (ctx->h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (ctx->rank == 0) {
        /* namespace must exist given we are on rank 0 */
        if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, ns))) {
            errno = ENOTSUP;
            goto error;
        }

        if (check_user (ctx, root, msg) < 0)
            goto error;
    }
    else {
        /* If root is not initialized, we have to initialize ourselves
         * first.
         */
        bool stall = false;
        if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
            if (stall) {
                request_tracking_add (ctx, msg);
                return;
            }
            goto error;
        }
    }

    /* N.B. owner cast into int */
    if (flux_respond_pack (h,
                           msg,
                           "{ s:i s:i s:s s:i s:s }",
                           "owner", root->owner,
                           "rootseq", root->seq,
                           "rootref", root->ref,
                           "flags", root->flags,
                           "namespace", root->ns_name) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    request_tracking_remove (ctx, msg);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    request_tracking_remove (ctx, msg);
}

static void error_event_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    json_t *names = NULL;
    int errnum;

    if (flux_event_unpack (msg,
                           NULL,
                           "{ s:s s:o s:i }",
                           "namespace", &ns,
                           "names", &names,
                           "errnum", &errnum) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    /* if root not initialized, nothing to do
     * - note that it is possible the namespace has been marked for
     *   removal, we may be cleaning up lingering transactions and
     *   need to report to those callers that namespace not available
     *   via finalize_transaction_bynames() below.
     * - i.e. we're calling kvsroot_mgr_lookup_root() not
     *   kvsroot_mgr_lookup_root_safe().
     */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, ns)))
        return;

    finalize_transaction_bynames (ctx, root, names, errnum);
}

/* Alter the (rootref, rootseq) in response to a setroot event.
 */
static void setroot_event_process (struct kvs_ctx *ctx,
                                   struct kvsroot *root,
                                   json_t *names,
                                   const char *rootref,
                                   int rootseq)
{
    int errnum = 0;

    /* in rare chance we receive setroot on removed namespace, return
     * ENOTSUP to client callers */
    if (root->remove)
        errnum = ENOTSUP;

    finalize_transaction_bynames (ctx, root, names, errnum);

    /* if error, not need to complete setroot */
    if (errnum)
        return;

    setroot (ctx, root, rootref, rootseq);
}

static void setroot_event_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvsroot *root;
    const char *ns;
    int rootseq;
    const char *rootref;
    json_t *names = NULL;

    if (flux_event_unpack (msg,
                           NULL,
                           "{ s:s s:i s:s s:o }",
                           "namespace", &ns,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "names", &names) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    /* if root not initialized, nothing to do
     * - small chance we could receive setroot event on namespace that
     *   is being removed.  Would require events to be received out of
     *   order (commit/fence completes before namespace removed, but
     *   namespace remove event received before setroot).
     */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, ns)))
        return;

    if (root->setroot_pause) {
        assert (root->setroot_queue);
        if (flux_msglist_append (root->setroot_queue, msg) < 0) {
            flux_log_error (ctx->h, "%s: flux_msglist_append", __FUNCTION__);
            return;
        }
        return;
    }

    setroot_event_process (ctx, root, names, rootref, rootseq);
}

static bool disconnect_cmp (const flux_msg_t *msg, void *arg)
{
    flux_msg_t *msgreq = arg;
    return flux_msg_route_match_first (msgreq, msg);
}

static int disconnect_request_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_cb_data *cbd = arg;

    /* Log error, but don't return -1, can continue to iterate
     * remaining roots */
    if (kvs_wait_version_remove_msg (root,
                                     disconnect_cmp,
                                     (void *)cbd->msg) < 0) {
        flux_log_error (cbd->ctx->h,
                        "%s: kvs_wait_version_remove_msg",
                        __FUNCTION__);
    }
    return 0;
}

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct kvs_ctx *ctx = arg;
    struct kvs_cb_data cbd;

    cbd.ctx = ctx;
    cbd.msg = msg;
    if (kvsroot_mgr_iter_roots (ctx->krm, disconnect_request_root_cb, &cbd) < 0)
        flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);

    if (cache_wait_destroy_msg (ctx->cache, disconnect_cmp, (void *)msg) < 0)
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
}

static int stats_get_root_cb (struct kvsroot *root, void *arg)
{
    json_t *nsstats = arg;
    json_t *s;

    if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                         "#versionwaiters",
                         zlist_size (root->wait_version_list),
                         "#no-op stores",
                         kvstxn_mgr_get_noop_stores (root->ktm),
                         "#transactions",
                         treq_mgr_transactions_count (root->trm),
                         "#readytransactions",
                         kvstxn_mgr_ready_transaction_count (root->ktm),
                         "store revision", root->seq))) {
        errno = ENOMEM;
        return -1;
    }

    json_object_set_new (nsstats, root->ns_name, s);
    return 0;
}

static void stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct kvs_ctx *ctx = arg;
    json_t *tstats = NULL;
    json_t *cstats = NULL;
    json_t *nsstats = NULL;
    tstat_t ts = { 0 };
    int size = 0, incomplete = 0, dirty = 0;
    double scale = 1E-3;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;

    /* if no roots are initialized, respond with all zeroes as stats */
    if (kvsroot_mgr_root_count (ctx->krm) > 0) {
        if (cache_get_stats (ctx->cache, &ts, &size, &incomplete, &dirty) < 0)
            goto error;
    }

    if (!(tstats = json_pack ("{ s:i s:f s:f s:f s:f }",
                              "count", tstat_count (&ts),
                              "min", tstat_min (&ts)*scale,
                              "mean", tstat_mean (&ts)*scale,
                              "stddev", tstat_stddev (&ts)*scale,
                              "max", tstat_max (&ts)*scale)))
        goto nomem;

    if (!(cstats = json_pack ("{ s:f s:O s:i s:i s:i }",
                              "obj size total (MiB)", (double)size/1048576,
                              "obj size (KiB)", tstats,
                              "#obj dirty", dirty,
                              "#obj incomplete", incomplete,
                              "#faults", ctx->faults)))
        goto nomem;

    if (!(nsstats = json_object ()))
        goto nomem;

    if (kvsroot_mgr_root_count (ctx->krm) > 0) {
        if (kvsroot_mgr_iter_roots (ctx->krm, stats_get_root_cb, nsstats) < 0) {
            flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
            goto error;
        }
    }
    else {
        json_t *s;

        if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                             "#watchers", 0,
                             "#no-op stores", 0,
                             "#transactions", 0,
                             "#readytransactions", 0,
                             "store revision", 0)))
            goto nomem;

        if (json_object_set_new (nsstats, KVS_PRIMARY_NAMESPACE, s) < 0) {
            json_decref (s);
            goto nomem;
        }
    }

    if (flux_respond_pack (h,
                           msg,
                           "{ s:O s:O s:i }",
                           "cache", cstats,
                           "namespace", nsstats,
                           "pending_requests", zhashx_size (ctx->requests)) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (tstats);
    json_decref (cstats);
    json_decref (nsstats);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (tstats);
    json_decref (cstats);
    json_decref (nsstats);
}

static int stats_clear_root_cb (struct kvsroot *root, void *arg)
{
    kvstxn_mgr_clear_noop_stores (root->ktm);
    return 0;
}

static void stats_clear (struct kvs_ctx *ctx)
{
    ctx->faults = 0;

    if (kvsroot_mgr_iter_roots (ctx->krm, stats_clear_root_cb, NULL) < 0)
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
}

static void stats_clear_event_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct kvs_ctx *ctx = arg;

    stats_clear (ctx);
}

static void stats_clear_request_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct kvs_ctx *ctx = arg;

    stats_clear (ctx);

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static int namespace_create (struct kvs_ctx *ctx,
                             const char *ns,
                             const char *rootref,
                             uint32_t owner,
                             int flags,
                             const char **errmsg)
{
    struct kvsroot *root;
    flux_msg_t *msg = NULL;
    char *topic = NULL;
    int rv = -1;

    /* If namespace already exists, return EEXIST.  Doesn't matter if
     * namespace is in process of being removed */
    if ((root = kvsroot_mgr_lookup_root (ctx->krm, ns))) {
        if (root->remove)
            (*errmsg) = "namespace with identical name in process "
                "of being removed. Try again later";
        errno = EEXIST;
        return -1;
    }

    if (!(root = kvsroot_mgr_create_root (ctx->krm,
                                          ctx->cache,
                                          ctx->hash_name,
                                          ns,
                                          owner,
                                          flags))) {
        flux_log_error (ctx->h, "%s: kvsroot_mgr_create_root", __FUNCTION__);
        return -1;
    }

    setroot (ctx, root, rootref, 0);

    if (event_subscribe (ctx, ns) < 0) {
        flux_log_error (ctx->h, "%s: event_subscribe", __FUNCTION__);
        goto cleanup;
    }

    if (asprintf (&topic, "kvs.namespace-%s-created", ns) < 0)
        goto cleanup;

    if (!(msg = flux_event_pack (topic,
                                 "{ s:s s:i s:s s:i }",
                                 "namespace", root->ns_name,
                                 "rootseq", root->seq,
                                 "rootref", root->ref,
                                 "owner", root->owner))) {
        flux_log_error (ctx->h, "%s: flux_event_pack", __FUNCTION__);
        goto cleanup;
    }

    if (flux_msg_set_private (msg) < 0) {
        flux_log_error (ctx->h, "%s: flux_msg_set_private", __FUNCTION__);
        goto cleanup;
    }

    if (flux_send (ctx->h, msg, 0) < 0) {
        flux_log_error (ctx->h, "%s: flux_send", __FUNCTION__);
        goto cleanup;
    }

    rv = 0;
cleanup:
    if (rv < 0)
        kvsroot_mgr_remove_root (ctx->krm, ns);
    free (topic);
    flux_msg_destroy (msg);
    return rv;
}

static void namespace_create_request_cb (flux_t *h,
                                         flux_msg_handler_t *mh,
                                         const flux_msg_t *msg,
                                         void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *errmsg = NULL;
    const char *ns;
    const char *rootref;
    uint32_t owner;
    int flags;

    assert (ctx->rank == 0);

    /* N.B. owner read into uint32_t */
    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:s s:s s:i s:i }",
                             "namespace", &ns,
                             "rootref", &rootref,
                             "owner", &owner,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (owner == FLUX_USERID_UNKNOWN)
        owner = getuid ();

    if (namespace_create (ctx, ns, rootref, owner, flags, &errmsg) < 0)
        goto error;

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static int root_remove_process_transactions (treq_t *tr, void *data)
{
    struct kvs_cb_data *cbd = data;

    /* Transactions that never reached their nprocs count will never
     * finish, must alert them with ENOTSUP that namespace removed.
     * Final call to treq_mgr_remove_transaction() done in
     * finalize_transaction_bynames() */
    if (!treq_get_processed (tr)) {
        json_t *names = NULL;

        if (!(names = json_pack ("[ s ]", treq_get_name (tr)))) {
            errno = ENOMEM;
            flux_log_error (cbd->ctx->h, "%s: json_pack", __FUNCTION__);
            return -1;
        }

        finalize_transaction_bynames (cbd->ctx, cbd->root, names, ENOTSUP);
        json_decref (names);
    }
    return 0;
}

static void start_root_remove (struct kvs_ctx *ctx, const char *ns)
{
    struct kvsroot *root;

    /* safe lookup, if root removal in process, let it continue */
    if ((root = kvsroot_mgr_lookup_root_safe (ctx->krm, ns))) {
        struct kvs_cb_data cbd = { .ctx = ctx, .root = root };

        root->remove = true;

        work_queue_remove (root);

        /* Now that root has been marked for removal from roothash,
         * run through the whole wait_version_list.  requests will
         * notice root removed, return ENOTSUP to all those trying to
         * sync.
         */
        kvs_wait_version_process (root, true);

        /* Ready transactions will be processed and errors returned to
         * callers via the code path in kvstxn_apply().  But not ready
         * transactions must be dealt with separately here.
         *
         * Note that now that the root has been marked as removable,
         * no new transactions can become ready in the future.  Checks
         * in commit_request_cb() and relaycommit_request_cb() ensure
         * this.
         */

        if (treq_mgr_iter_transactions (root->trm,
                                        root_remove_process_transactions,
                                        &cbd) < 0)
            flux_log_error (ctx->h, "%s: treq_mgr_iter_transactions",
                            __FUNCTION__);
    }
}

static int namespace_remove (struct kvs_ctx *ctx, const char *ns)
{
    flux_msg_t *msg = NULL;
    int saved_errno, rc = -1;
    char *topic = NULL;

    /* Namespace doesn't exist or is already in process of being
     * removed */
    if (!kvsroot_mgr_lookup_root_safe (ctx->krm, ns)) {
        /* silently succeed */
        goto done;
    }

    if (asprintf (&topic, "kvs.namespace-%s-removed", ns) < 0) {
        saved_errno = errno;
        goto cleanup;
    }
    if (!(msg = flux_event_pack (topic, "{ s:s }", "namespace", ns))) {
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

    start_root_remove (ctx, ns);
done:
    rc = 0;
cleanup:
    flux_msg_destroy (msg);
    free (topic);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

static void namespace_remove_request_cb (flux_t *h,
                                         flux_msg_handler_t *mh,
                                         const flux_msg_t *msg,
                                         void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns;

    assert (ctx->rank == 0);

    if (flux_request_unpack (msg, NULL, "{ s:s }", "namespace", &ns) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!strcasecmp (ns, KVS_PRIMARY_NAMESPACE)) {
        errno = ENOTSUP;
        goto error;
    }

    if (namespace_remove (ctx, ns) < 0) {
        flux_log_error (h, "%s: namespace_remove", __FUNCTION__);
        goto error;
    }

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void namespace_removed_event_cb (flux_t *h,
                                        flux_msg_handler_t *mh,
                                        const flux_msg_t *msg,
                                        void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns;

    if (flux_event_unpack (msg, NULL, "{ s:s }", "namespace", &ns) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    assert (strcasecmp (ns, KVS_PRIMARY_NAMESPACE));

    start_root_remove (ctx, ns);
}

static int namespace_list_cb (struct kvsroot *root, void *arg)
{
    json_t *namespaces = arg;
    json_t *o;

    /* do not list namespaces marked for removal */
    if (root->remove)
        return 0;

    if (!(o = json_pack ("{ s:s s:i s:i }",
                         "namespace", root->ns_name,
                         "owner", root->owner,
                         "flags", root->flags))) {
        errno = ENOMEM;
        return -1;
    }

    json_array_append_new (namespaces, o);
    return 0;
}

static void namespace_list_request_cb (flux_t *h,
                                       flux_msg_handler_t *mh,
                                       const flux_msg_t *msg,
                                       void *arg)
{
    struct kvs_ctx *ctx = arg;
    json_t *namespaces = NULL;

    if (!(namespaces = json_array ()))
        goto nomem;

    if (kvsroot_mgr_iter_roots (ctx->krm, namespace_list_cb, namespaces) < 0) {
        flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        goto error;
    }

    if (flux_respond_pack (h, msg, "{ s:O }", "namespaces", namespaces) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (namespaces);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (namespaces);
}

/* This RPC request is specifically used as a test hook.  It pauses
 * the processing of setroot events.  Any setroot events that are
 * received, are put onto a queue to be processed after an unpause. By
 * doing so, a particular rank will not be kept up to date on changes
 * to the KVS.  This can be used for testing purposes, such as testing
 * if read-your-writes consistency is working.
 */
static void setroot_pause_request_cb (flux_t *h,
                                      flux_msg_handler_t *mh,
                                      const flux_msg_t *msg,
                                      void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns = NULL;
    struct kvsroot *root;
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "namespace", &ns) < 0) {
        flux_log_error (ctx->h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
        if (stall)
            return;
        goto error;
    }

    root->setroot_pause = true;

    if (!root->setroot_queue) {
        if (!(root->setroot_queue = flux_msglist_create ()))
            goto error;
    }

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void setroot_unpause_process_msg (struct kvs_ctx *ctx,
                                         struct kvsroot *root,
                                         const flux_msg_t *msg)
{
    const char *ns;
    int rootseq;
    const char *rootref;
    json_t *names = NULL;

    if (flux_event_unpack (msg,
                           NULL,
                           "{ s:s s:i s:s s:o }",
                           "namespace", &ns,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "names", &names) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    setroot_event_process (ctx, root, names, rootref, rootseq);
    return;
}

/* This RPC request is specifically used as a test hook.  It
 * unpauses/allows the processing of setroot events.  Any setroot
 * events that were received during a pause will be processed in the
 * order they were received.
 */
static void setroot_unpause_request_cb (flux_t *h,
                                        flux_msg_handler_t *mh,
                                        const flux_msg_t *msg,
                                        void *arg)
{
    struct kvs_ctx *ctx = arg;
    const char *ns = NULL;
    struct kvsroot *root;
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "namespace", &ns) < 0) {
        flux_log_error (ctx->h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, ns, mh, msg, NULL, &stall))) {
        if (stall)
            return;
        goto error;
    }

    root->setroot_pause = false;

    /* user never called pause if !root->setroot_queue*/
    if (root->setroot_queue) {
        const flux_msg_t *m;
        while ((m = flux_msglist_pop (root->setroot_queue))) {
            setroot_unpause_process_msg (ctx, root, m);
            flux_msg_decref (m);
        }
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct kvs_ctx *ctx = arg;
    const flux_conf_t *conf;
    const char *errstr = NULL;
    flux_error_t error;

    if (flux_conf_reload_decode (msg, &conf) < 0)
        goto error;
    if (kvs_checkpoint_reload (ctx->kcp, conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
 error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

/* see comments above in event_subscribe() regarding event
 * subscriptions to kvs.namespace */
static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.stats-get",
        stats_get_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.stats-clear",
        stats_clear_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_EVENT,
        "kvs.stats-clear",
        stats_clear_event_cb,
        0
    },
    {
        FLUX_MSGTYPE_EVENT,
        "kvs.namespace-*-setroot",
        setroot_event_cb,
        0
    },
    {
        FLUX_MSGTYPE_EVENT,
        "kvs.namespace-*-error",
        error_event_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.getroot",
        getroot_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.dropcache",
        dropcache_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_EVENT,
        "kvs.dropcache",
        dropcache_event_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.disconnect",
        disconnect_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.wait-version",
        wait_version_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.lookup",
        lookup_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.lookup-plus",
        lookup_plus_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.commit",
        commit_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.relaycommit",
        relaycommit_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.fence",
        fence_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.relayfence",
        relayfence_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.namespace-create",
        namespace_create_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.namespace-remove",
        namespace_remove_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_EVENT,
        "kvs.namespace-*-removed",
        namespace_removed_event_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.namespace-list",
        namespace_list_request_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.setroot-pause",
        setroot_pause_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.setroot-unpause",
        setroot_unpause_request_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kvs.config-reload",
        config_reload_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static int process_config (struct kvs_ctx *ctx)
{
    flux_error_t error;
    if (kvs_checkpoint_config_parse (ctx->kcp,
                                     flux_get_conf (ctx->h),
                                     &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s", error.text);
        return -1;
    }
    return 0;
}

static int process_args (struct kvs_ctx *ctx, int ac, char **av)
{
    int i;

    for (i = 0; i < ac; i++) {
        if (strstarts (av[i], "transaction-merge=")) {
            char *endptr;
            errno = 0;
            ctx->transaction_merge = strtoul (av[i]+18, &endptr, 10);
            if (errno != 0 || *endptr != '\0') {
                errno = EINVAL;
                return -1;
            }
        }
        else {
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

/* Synchronously get checkpoint data by key from checkpoint service.
 * Copy rootref buf with '\0' termination.
 * Return 0 on success, -1 on failure,
 */
static int checkpoint_get (flux_t *h, char *buf, size_t len, int *seq)
{
    flux_future_t *f = NULL;
    const char *rootref;
    double timestamp = 0;
    char datestr[128] = "N/A";
    int rv = -1;

    if (!(f = kvs_checkpoint_lookup (h, NULL, 0)))
        return -1;

    if (kvs_checkpoint_lookup_get_rootref (f, &rootref) < 0)
        goto error;

    if (strlen (rootref) >= len) {
        errno = EINVAL;
        goto error;
    }
    strcpy (buf, rootref);

    (void)kvs_checkpoint_lookup_get_sequence (f, seq);

    (void)kvs_checkpoint_lookup_get_timestamp (f, &timestamp);
    if (timestamp > 0)
        timestamp_tostr (timestamp, datestr, sizeof (datestr));

    flux_log (h, LOG_INFO,
              "restored KVS from checkpoint on %s", datestr);

    rv = 0;
error:
    flux_future_destroy (f);
    return rv;
}

/* Synchronously store checkpoint to checkpoint service.
 * Returns 0 on success, -1 on failure.
 */
static int checkpoint_put (flux_t *h, const char *rootref, int rootseq)
{
    flux_future_t *f = NULL;
    int rv = -1;

    if (!(f = kvs_checkpoint_commit (h, NULL, rootref, rootseq, 0, 0))
        || flux_rpc_get (f, NULL) < 0)
        goto error;
    rv = 0;
error:
    flux_future_destroy (f);
    return rv;
}

/* Store initial root in local cache, and flush to content cache
 * synchronously.  The corresponding blobref is written into 'ref'.
 *
 * N.B. The code for creating a new / empty kvs namespace assumes that
 * an empty RFC 11 dir object was already created / stored, but
 * offline garbage collection could remove it.
 */
static int store_initial_rootdir (struct kvs_ctx *ctx, char *ref, int ref_len)
{
    struct cache_entry *entry;
    int saved_errno;
    __attribute__((unused)) int ret;
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
    if (blobref_hash (ctx->hash_name, data, len, ref, ref_len) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (ctx->cache, ref))) {
        if (!(entry = cache_entry_create (ref))) {
            flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
            goto error;
        }
        if (cache_insert (ctx->cache, entry) < 0) {
            flux_log_error (ctx->h, "%s: cache_insert", __FUNCTION__);
            cache_entry_destroy (entry);
            goto error;
        }
    }
    if (!cache_entry_get_valid (entry)) {
        if (cache_entry_set_raw (entry, data, len) < 0) { // makes entry valid
            flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
            goto error_uncache;
        }
        if (!(f = content_store (ctx->h, data, len, 0))
                || content_store_get_blobref (f, ctx->hash_name, &newref) < 0) {
            flux_log_error (ctx->h, "%s: content_store", __FUNCTION__);
            goto error_uncache;
        }
        /* Sanity check that content cache is using the same hash alg as KVS.
         * It should suffice to do this once at startup.
         */
        if (!streq (newref, ref)) {
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
    struct kvs_ctx *ctx;
    flux_msg_handler_t **handlers = NULL;
    flux_future_t *f_heartbeat_sync = NULL;
    int rc = -1;

    if (!(ctx = kvs_ctx_create (h))) {
        flux_log_error (h, "error creating KVS context");
        goto done;
    }
    if (process_config (ctx) < 0)
        goto done;
    if (process_args (ctx, argc, argv) < 0)
        goto done;
    if (ctx->rank == 0) {
        struct kvsroot *root;
        char empty_dir_rootref[BLOBREF_MAX_STRING_SIZE];
        char rootref[BLOBREF_MAX_STRING_SIZE];
        int seq = 0;
        uint32_t owner = getuid ();

        if (store_initial_rootdir (ctx,
                                   empty_dir_rootref,
                                   sizeof (empty_dir_rootref)) < 0) {
            flux_log_error (h, "store_initial_rootdir");
            goto done;
        }

        /* Look for a checkpoint and use it if found.
         * Otherwise start the primary root namespace with an empty directory
         * and seq = 0.
         */
        if (checkpoint_get (h, rootref, sizeof (rootref), &seq) < 0)
            memcpy (rootref, empty_dir_rootref, sizeof (empty_dir_rootref));

        /* primary namespace must always be there and not marked
         * for removal
         */
        if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm,
                                                   KVS_PRIMARY_NAMESPACE))) {

            if (!(root = kvsroot_mgr_create_root (ctx->krm,
                                                  ctx->cache,
                                                  ctx->hash_name,
                                                  KVS_PRIMARY_NAMESPACE,
                                                  owner,
                                                  0))) {
                flux_log_error (h, "kvsroot_mgr_create_root");
                goto done;
            }
        }

        setroot (ctx, root, rootref, seq);

        if (event_subscribe (ctx, KVS_PRIMARY_NAMESPACE) < 0) {
            flux_log_error (h, "event_subscribe");
            goto done;
        }

        kvs_checkpoint_update_root_primary (ctx->kcp, root);
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if (!(f_heartbeat_sync = flux_sync_create (h, heartbeat_sync_min))
            || flux_future_then (f_heartbeat_sync,
                                 heartbeat_sync_max,
                                 heartbeat_sync_cb,
                                 ctx) < 0) {
        flux_log_error (h, "error starting heartbeat synchronization");
        goto done;
    }
    kvs_checkpoint_start (ctx->kcp);
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    if (zhashx_size (ctx->requests) > 0) {
        /* anything that has not yet completed gets an ENOSYS */
        const flux_msg_t *msg = zhashx_first (ctx->requests);
        while (msg) {
            const char *topic = "unknown";
            if (flux_msg_get_topic (msg, &topic) < 0)
                flux_log_error (ctx->h, "%s: flux_msg_get_topic", __FUNCTION__);
            if (flux_respond_error (ctx->h, msg, ENOSYS, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            flux_log (ctx->h, LOG_ERR, "failing pending '%s' request", topic);
            msg = zhashx_next (ctx->requests);
        }
    }
    /* Checkpoint the KVS root to the content backing store.
     * If backing store is not loaded, silently proceed without checkpoint.
     */
    if (ctx->rank == 0) {
        struct kvsroot *root;

        if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm,
                                                   KVS_PRIMARY_NAMESPACE))) {
            flux_log_error (h, "error looking up primary root");
            goto done;
        }
        if (checkpoint_put (ctx->h, root->ref, root->seq) < 0) {
            if (errno != ENOSYS) { // service not loaded is not an error
                flux_log_error (h, "error saving primary KVS checkpoint");
                goto done;
            }
        }
    }
    rc = 0;
done:
    flux_future_destroy (f_heartbeat_sync);
    flux_msg_handler_delvec (handlers);
    kvs_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
