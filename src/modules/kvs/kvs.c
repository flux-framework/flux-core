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
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_txn_private.h"
#include "src/common/libkvs/kvs_util_private.h"

#include "waitqueue.h"
#include "cache.h"

#include "lookup.h"
#include "treq.h"
#include "kvstxn.h"
#include "kvsroot.h"

/* Expire cache_entry after 'max_lastuse_age' heartbeats.
 */
const int max_lastuse_age = 5;

/* Expire namespaces after 'max_namespace_age' heartbeats.
 *
 * If heartbeats are the default of 2 seconds, 1000 heartbeats is
 * about half an hour.
 */
const int max_namespace_age = 1000;

/* Include root directory in kvs.setroot event.
 */
const bool event_includes_rootdir = true;

typedef struct {
    struct cache *cache;    /* blobref => cache_entry */
    kvsroot_mgr_t *krm;
    int faults;                 /* for kvs.stats.get, etc. */
    flux_t *h;
    uint32_t rank;
    int epoch;              /* tracks current heartbeat epoch */
    flux_watcher_t *prep_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    int transaction_merge;
    bool events_init;            /* flag */
    const char *hash_name;
    unsigned int seq;           /* for commit transactions */
} kvs_ctx_t;

struct kvs_cb_data {
    kvs_ctx_t *ctx;
    struct kvsroot *root;
    wait_t *wait;
    int errnum;
    bool ready;
    char *sender;
};

static void transaction_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                                 int revents, void *arg);
static void transaction_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg);
static void start_root_remove (kvs_ctx_t *ctx, const char *namespace);

/*
 * kvs_ctx_t functions
 */
static void freectx (void *arg)
{
    kvs_ctx_t *ctx = arg;
    if (ctx) {
        cache_destroy (ctx->cache);
        kvsroot_mgr_destroy (ctx->krm);
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
        if (!(r = flux_get_reactor (h))) {
            saved_errno = errno;
            goto error;
        }
        if (!(ctx->hash_name = flux_attr_get (h, "content.hash"))) {
            saved_errno = errno;
            flux_log_error (h, "content.hash");
            goto error;
        }
        ctx->cache = cache_create ();
        if (!ctx->cache) {
            saved_errno = ENOMEM;
            goto error;
        }
        if (!(ctx->krm = kvsroot_mgr_create (ctx->h, ctx))) {
            saved_errno = ENOMEM;
            goto error;
        }
        ctx->h = h;
        if (flux_get_rank (h, &ctx->rank) < 0) {
            saved_errno = errno;
            goto error;
        }
        if (ctx->rank == 0) {
            ctx->prep_w = flux_prepare_watcher_create (r, transaction_prep_cb, ctx);
            if (!ctx->prep_w) {
                saved_errno = errno;
                goto error;
            }
            ctx->check_w = flux_check_watcher_create (r, transaction_check_cb, ctx);
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
        ctx->transaction_merge = 1;
        if (flux_aux_set (h, "kvssrv", ctx, freectx) < 0) {
            saved_errno = errno;
            goto error;
        }
    }
    return ctx;
error:
    freectx (ctx);
    errno = saved_errno;
    return NULL;
}

/*
 * event subscribe/unsubscribe
 */

static int event_subscribe (kvs_ctx_t *ctx, const char *namespace)
{
    char *setroot_topic = NULL;
    char *error_topic = NULL;
    char *removed_topic = NULL;
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

        ctx->events_init = true;
    }

    if (asprintf (&setroot_topic, "kvs.setroot-%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_subscribe (ctx->h, setroot_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    if (asprintf (&error_topic, "kvs.error-%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_subscribe (ctx->h, error_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    if (asprintf (&removed_topic, "kvs.namespace-removed-%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_subscribe (ctx->h, removed_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    rc = 0;
cleanup:
    free (setroot_topic);
    free (error_topic);
    free (removed_topic);
    return rc;
}

static int event_unsubscribe (kvs_ctx_t *ctx, const char *namespace)
{
    char *setroot_topic = NULL;
    char *error_topic = NULL;
    int rc = -1;

    if (asprintf (&setroot_topic, "kvs.setroot-%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (flux_event_unsubscribe (ctx->h, setroot_topic) < 0) {
        flux_log_error (ctx->h, "flux_event_subscribe");
        goto cleanup;
    }

    if (asprintf (&error_topic, "kvs.error-%s", namespace) < 0) {
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
 * security
 */

static int get_msg_cred (kvs_ctx_t *ctx,
                         const flux_msg_t *msg,
                         uint32_t *rolemask,
                         uint32_t *userid)
{
    if (flux_msg_get_rolemask (msg, rolemask) < 0) {
        flux_log_error (ctx->h, "flux_msg_get_rolemask");
        return -1;
    }

    if (flux_msg_get_userid (msg, userid) < 0) {
        flux_log_error (ctx->h, "flux_msg_get_userid");
        return -1;
    }

    return 0;
}

static int check_user (kvs_ctx_t *ctx, struct kvsroot *root,
                       const flux_msg_t *msg)
{
    uint32_t rolemask;
    uint32_t userid;

    if (get_msg_cred (ctx, msg, &rolemask, &userid) < 0)
        return -1;

    return kvsroot_check_user (ctx->krm, root, rolemask, userid);
}

/*
 * set/get root
 */

static void setroot (kvs_ctx_t *ctx, struct kvsroot *root,
                     const char *rootref, int rootseq)
{
    if (rootseq == 0 || rootseq > root->seq) {
        kvsroot_setroot (ctx->krm, root, rootref, rootseq);

        /* log error on wait_runqueue(), don't error out.  watchers
         * may miss value change, but will never get older one.
         * Maintains consistency model */
        if (wait_runqueue (root->watchlist) < 0)
            flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);
        root->watchlist_lastrun_epoch = ctx->epoch;
    }
}

static void getroot_completion (flux_future_t *f, void *arg)
{
    kvs_ctx_t *ctx = arg;
    flux_msg_t *msg = NULL;
    const char *namespace;
    int rootseq, flags;
    uint32_t owner;
    const char *ref;
    struct kvsroot *root;
    int save_errno;

    msg = flux_future_aux_get (f, "msg");
    assert (msg);

    if (flux_request_unpack (msg, NULL, "{ s:s }",
                             "namespace", &namespace) < 0) {
        flux_log_error (ctx->h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    /* N.B. owner read into uint32_t */
    if (flux_rpc_get_unpack (f, "{ s:i s:i s:s s:i }",
                             "owner", &owner,
                             "rootseq", &rootseq,
                             "rootref", &ref,
                             "flags", &flags) < 0) {
        if (errno != ENOTSUP)
            flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    /* possible root initialized by another message before we got this
     * response.  Not relevant if namespace in process of being removed. */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, namespace))) {

        if (!(root = kvsroot_mgr_create_root (ctx->krm,
                                              ctx->cache,
                                              ctx->hash_name,
                                              namespace,
                                              owner,
                                              flags))) {
            flux_log_error (ctx->h, "%s: kvsroot_mgr_create_root", __FUNCTION__);
            goto error;
        }

        if (event_subscribe (ctx, namespace) < 0) {
            save_errno = errno;
            kvsroot_mgr_remove_root (ctx->krm, namespace);
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

    /* flux_requeue_nocopy takes ownership of 'msg', no need to destroy */
    if (flux_requeue_nocopy (ctx->h, msg, FLUX_RQ_HEAD) < 0) {
        flux_log_error (ctx->h, "%s: flux_requeue_nocopy", __FUNCTION__);
        goto error;
    }

    flux_future_destroy (f);
    return;

error:
    if (flux_respond (ctx->h, msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (msg);
    flux_future_destroy (f);
}

static int getroot_request_send (kvs_ctx_t *ctx,
                                 const char *namespace,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 lookup_t *lh,
                                 flux_msg_handler_f cb)
{
    flux_future_t *f = NULL;
    flux_msg_t *msgcpy = NULL;
    int saved_errno;

    if (!(f = flux_rpc_pack (ctx->h, "kvs.getroot", FLUX_NODEID_UPSTREAM, 0,
                             "{ s:s }",
                             "namespace", namespace)))
        goto error;

    if (!(msgcpy = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    if (lh
        && flux_msg_aux_set (msg, "lookup_handle", lh, NULL) < 0) {
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

static struct kvsroot *getroot (kvs_ctx_t *ctx, const char *namespace,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                lookup_t *lh,
                                flux_msg_handler_f cb,
                                bool *stall)
{
    struct kvsroot *root;

    (*stall) = false;

    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace))) {
        if (ctx->rank == 0) {
            errno = ENOTSUP;
            return NULL;
        }
        else {
            if (getroot_request_send (ctx, namespace, mh, msg, lh, cb) < 0) {
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

/* Identical to getroot(), but will also check for namespace prefix in
 * key, and return result if warranted
 */
static struct kvsroot *getroot_namespace_prefix (kvs_ctx_t *ctx,
                                                 const char *namespace,
                                                 flux_msg_handler_t *mh,
                                                 const flux_msg_t *msg,
                                                 flux_msg_handler_f cb,
                                                 bool *stall,
                                                 json_t *ops,
                                                 char **namespace_prefix)
{
    struct kvsroot *root = NULL;
    char *ns_prefix = NULL;
    int len = json_array_size (ops);

    /* We only check the first operation to determine the namespace
     * all operations should belong to.  If the user specifies
     * multiple namespaces, it will error out when the operations are
     * applied.
     */

    if (len) {
        json_t *op = json_array_get (ops, 0);
        const char *key;

        if (txn_decode_op (op, &key, NULL, NULL) < 0)
            goto done;

        if (kvs_namespace_prefix (key, &ns_prefix, NULL) < 0)
            goto done;
    }

    if (!(root = getroot (ctx,
                          ns_prefix ? ns_prefix : namespace,
                          mh,
                          msg,
                          NULL,
                          cb,
                          stall)))
        goto done;

    /* return alt-namespace to caller */
    if (ns_prefix) {
        (*namespace_prefix) = ns_prefix;
        ns_prefix = NULL;
    }

done:
    free (ns_prefix);
    return root;
}

/*
 * load
 */

static void content_load_cache_entry_error (kvs_ctx_t *ctx,
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
    kvs_ctx_t *ctx = arg;
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
    if (!(entry = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
        flux_log (ctx->h, LOG_ERR, "%s: cache_lookup", __FUNCTION__);
        goto done;
    }

    if (flux_content_load_get (f, &data, &size) < 0) {
        flux_log_error (ctx->h, "%s: flux_content_load_get", __FUNCTION__);
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

/* Send content load request and setup contination to handle response.
 */
static int content_load_request_send (kvs_ctx_t *ctx, const char *ref)
{
    flux_future_t *f = NULL;
    char *refcpy;
    int saved_errno;

    if (!(f = flux_content_load (ctx->h, ref, 0))) {
        flux_log_error (ctx->h, "%s: flux_content_load", __FUNCTION__);
        goto error;
    }
    if (!(refcpy = strdup (ref))) {
        errno = ENOMEM;
        goto error;
    }
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
static int load (kvs_ctx_t *ctx, const char *ref, wait_t *wait, bool *stall)
{
    struct cache_entry *entry = cache_lookup (ctx->cache, ref, ctx->epoch);
    int saved_errno, ret;

    assert (wait != NULL);

    /* Create an incomplete hash entry if none found.
     */
    if (!entry) {
        if (!(entry = cache_entry_create (ref))) {
            flux_log_error (ctx->h, "%s: cache_entry_create",
                            __FUNCTION__);
            return -1;
        }
        if (cache_insert (ctx->cache, entry) < 0) {
            flux_log_error (ctx->h, "%s: cache_insert",
                            __FUNCTION__);
            cache_entry_destroy (entry);
            return -1;
        }
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
        /* Potential future optimization, if this load() is called
         * multiple times from the same kvstxn and on the same
         * reference, we're effectively adding identical waiters onto
         * this cache entry.  This is far better than sending multiple
         * RPCs (the cache entry chck above protects against this),
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
    kvs_ctx_t *ctx = arg;
    struct cache_entry *entry;
    const char *cache_blobref, *blobref;
    int ret;

    cache_blobref = flux_future_aux_get (f, "cache_blobref");
    assert (cache_blobref);

    if (flux_content_store_get (f, &blobref) < 0) {
        flux_log_error (ctx->h, "%s: flux_content_store_get", __FUNCTION__);
        goto error;
    }

    /* Double check that content store stored in the same blobref
     * location we calculated.
     * N.B. perhaps this check is excessive and could be removed
     */
    if (strcmp (blobref, cache_blobref)) {
        flux_log (ctx->h, LOG_ERR, "%s: inconsistent blobref returned",
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
    if (!(entry = cache_lookup (ctx->cache, blobref, ctx->epoch))) {
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
        flux_log_error (ctx->h, "%s: cache_entry_set_dirty",
                        __FUNCTION__);
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
    if (!(entry = cache_lookup (ctx->cache, cache_blobref, ctx->epoch))) {
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
        flux_log (ctx->h, LOG_ERR, "%s: cache_entry_set_errnum_on_notdirty",
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

static int content_store_request_send (kvs_ctx_t *ctx, const char *blobref,
                                       const void *data, int len)
{
    flux_future_t *f;
    int saved_errno, rc = -1;

    if (!(f = flux_content_store (ctx->h, data, len, 0)))
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
        flux_log_error (cbd->ctx->h, "%s: cache_entry_get_raw",
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
        flux_log_error (cbd->ctx->h, "%s: content_store_request_send",
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

static int setroot_event_send (kvs_ctx_t *ctx, struct kvsroot *root,
                               json_t *names, json_t *keys)
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

    if (asprintf (&setroot_topic, "kvs.setroot-%s", root->namespace) < 0) {
        saved_errno = ENOMEM;
        flux_log_error (ctx->h, "%s: asprintf", __FUNCTION__);
        goto done;
    }

    if (!(msg = flux_event_pack (setroot_topic,
                                 "{ s:s s:i s:s s:O s:O s:O s:i}",
                                 "namespace", root->namespace,
                                 "rootseq", root->seq,
                                 "rootref", root->ref,
                                 "names", names,
                                 "rootdir", root_dir,
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

    if (asprintf (&error_topic, "kvs.error-%s", namespace) < 0) {
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

static void kvstxn_wait_error_cb (wait_t *w, int errnum, void *arg)
{
    kvstxn_t *kt = arg;
    kvstxn_set_aux_errnum (kt, errnum);
}

/* Write all the ops for a particular commit/fence request (rank 0
 * only).  The setroot event will cause responses to be sent to the
 * transaction requests and clean up the treq_t state.  This
 * function is idempotent.
 */
static void kvstxn_apply (kvstxn_t *kt)
{
    kvs_ctx_t *ctx = kvstxn_get_aux (kt);
    const char *namespace;
    struct kvsroot *root = NULL;
    wait_t *wait = NULL;
    int errnum = 0;
    kvstxn_process_t ret;
    bool fallback = false;

    namespace = kvstxn_get_namespace (kt);
    assert (namespace);

    /* Between call to kvstxn_mgr_add_transaction() and here, possible
     * namespace marked for removal.  Also namespace could have been
     * removed if we waited and this is a replay.
     *
     * root should never be NULL, as it should not be garbage
     * collected until all ready transactions have been processed.
     */

    root = kvsroot_mgr_lookup_root (ctx->krm, namespace);
    assert (root);

    if (root->remove) {
        flux_log (ctx->h, LOG_DEBUG, "%s: namespace %s removed", __FUNCTION__,
                  namespace);
        errnum = ENOTSUP;
        goto done;
    }

    if ((errnum = kvstxn_get_aux_errnum (kt)))
        goto done;

    if ((ret = kvstxn_process (kt,
                               ctx->epoch,
                               root->ref)) == KVSTXN_PROCESS_ERROR) {
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
    /* else ret == KVSTXN_PROCESS_FINISHED */

    /* This finalizes the transaction by replacing root->ref with
     * newroot, incrementing root->seq, and sending out the setroot
     * event for "eventual consistency" of other nodes.
     */
done:
    if (errnum == 0) {
        json_t *names = kvstxn_get_names (kt);
        int count;
        if ((count = json_array_size (names)) > 1) {
            int opcount = 0;
            opcount = json_array_size (kvstxn_get_ops (kt));
            flux_log (ctx->h, LOG_DEBUG, "aggregated %d transactions (%d ops)",
                      count, opcount);
        }
        setroot (ctx, root, kvstxn_get_newroot_ref (kt), root->seq + 1);
        setroot_event_send (ctx, root, names, kvstxn_get_keys (kt));
    } else {
        fallback = kvstxn_fallback_mergeable (kt);

        /* if merged transaction is fallbackable, ignore the fallback option
         * if it's an extreme "death" like error.
         */
        if (errnum == ENOMEM || errnum == ENOTSUP)
            fallback = false;

        if (!fallback)
            error_event_send (ctx, root->namespace, kvstxn_get_names (kt),
                              errnum);
    }
    wait_destroy (wait);

    /* Completed: remove from 'ready' list.
     * N.B. treq_t remains in the treq_mgr_t hash until event is received.
     */
    kvstxn_mgr_remove_transaction (root->ktm, kt, fallback);
    return;

stall:
    return;
}

/*
 * pre/check event callbacks
 */

static int kvstxn_prep_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_cb_data *cbd = arg;

    if (kvstxn_mgr_transaction_ready (root->ktm)) {
        cbd->ready = true;
        return 1;
    }

    return 0;
}

static void transaction_prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                                 int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvs_cb_data cbd = { .ctx = ctx, .ready = false };

    if (kvsroot_mgr_iter_roots (ctx->krm, kvstxn_prep_root_cb, &cbd) < 0) {
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        return;
    }

    if (cbd.ready)
        flux_watcher_start (ctx->idle_w);
}

static int kvstxn_check_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_cb_data *cbd = arg;
    kvstxn_t *kt;

    if ((kt = kvstxn_mgr_get_ready_transaction (root->ktm))) {
        if (cbd->ctx->transaction_merge) {
            /* if merge fails, set errnum in txn_t, let
             * txn_apply() handle error handling.
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

    return 0;
}

static void transaction_check_cb (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvs_cb_data cbd = { .ctx = ctx, .ready = false };

    flux_watcher_stop (ctx->idle_w);

    if (kvsroot_mgr_iter_roots (ctx->krm, kvstxn_check_root_cb, &cbd) < 0) {
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        return;
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

static int heartbeat_root_cb (struct kvsroot *root, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (root->remove) {
        if (!wait_queue_length (root->watchlist)
            && !treq_mgr_transactions_count (root->trm)
            && !kvstxn_mgr_ready_transaction_count (root->ktm)) {

            if (event_unsubscribe (ctx, root->namespace) < 0)
                flux_log_error (ctx->h, "%s: event_unsubscribe",
                                __FUNCTION__);

            if (kvsroot_mgr_remove_root (ctx->krm, root->namespace) < 0)
                flux_log_error (ctx->h, "%s: kvsroot_mgr_remove_root",
                                __FUNCTION__);
        }
    }
    else if (ctx->rank != 0
             && !root->remove
             && strcasecmp (root->namespace, KVS_PRIMARY_NAMESPACE)
             && (ctx->epoch - root->watchlist_lastrun_epoch) > max_namespace_age
             && !wait_queue_length (root->watchlist)
             && !treq_mgr_transactions_count (root->trm)
             && !kvstxn_mgr_ready_transaction_count (root->ktm)) {
        /* remove a root if it not the primary one, has timed out
         * on a follower node, and it does not have any watchers,
         * and no one is trying to write/change something.
         */
        start_root_remove (ctx, root->namespace);
    }
    else {
        /* "touch" objects involved in watched keys */
        if (wait_queue_length (root->watchlist) > 0
            && (ctx->epoch - root->watchlist_lastrun_epoch) > max_lastuse_age) {
            /* log error on wait_runqueue(), don't error out.  watchers
             * may miss value change, but will never get older one.
             * Maintains consistency model */
            if (wait_runqueue (root->watchlist) < 0)
                flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);
            root->watchlist_lastrun_epoch = ctx->epoch;
        }
        /* "touch" root */
        (void)cache_lookup (ctx->cache, root->ref, ctx->epoch);
    }

    return 0;
}

static void heartbeat_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log_error (ctx->h, "%s: flux_heartbeat_decode", __FUNCTION__);
        return;
    }

    /* don't error return, fallthrough to deal with rest as necessary */
    if (kvsroot_mgr_iter_roots (ctx->krm, heartbeat_root_cb, ctx) < 0)
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);

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

static void lookup_wait_error_cb (wait_t *w, int errnum, void *arg)
{
    lookup_t *lh = arg;
    lookup_set_aux_errnum (lh, errnum);
}

static void lookup_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    int flags;
    const char *namespace;
    const char *key;
    json_t *val = NULL;
    json_t *root_dirent = NULL;
    lookup_t *lh = NULL;
    const char *root_ref = NULL;
    wait_t *wait = NULL;
    lookup_process_t lret;
    int rc = -1;
    int ret;

    /* if lookup_handle exists in msg as aux data, is a replay */
    lh = flux_msg_aux_get (msg, "lookup_handle");
    if (!lh) {
        uint32_t rolemask, userid;
        int root_seq = -1;

        if (flux_request_unpack (msg, NULL, "{ s:s s:s s:i }",
                                 "key", &key,
                                 "namespace", &namespace,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        /* rootdir is optional */
        (void)flux_request_unpack (msg, NULL, "{ s:o }",
                                   "rootdir", &root_dirent);

        /* rootseq is optional */
        (void)flux_request_unpack (msg, NULL, "{ s:i }",
                                   "rootseq", &root_seq);

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

        if (get_msg_cred (ctx, msg, &rolemask, &userid) < 0)
            goto done;

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->krm,
                                  ctx->epoch,
                                  namespace,
                                  root_ref ? root_ref : NULL,
                                  root_seq,
                                  key,
                                  rolemask,
                                  userid,
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

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);
    }

    lret = lookup (lh);

    if (lret == LOOKUP_PROCESS_ERROR) {
        errno = lookup_get_errnum (lh);
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE) {
        bool stall = false;
        struct kvsroot *root;

        namespace = lookup_missing_namespace (lh);
        assert (namespace);

        root = getroot (ctx, namespace, mh, msg, lh, lookup_request_cb,
                        &stall);
        assert (!root);

        if (stall)
            goto stall;
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_REFS) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create_msg_handler (h, mh, msg, ctx,
                                              lookup_request_cb)))
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

    if ((val = lookup_get_value (lh)) == NULL) {
        errno = ENOENT;
        goto done;
    }

    if (flux_respond_pack (h, msg, "{ s:O }",
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
    json_decref (val);
}

static void watch_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
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
    lookup_process_t lret;
    int rc = -1;
    int saved_errno, ret;

    /* if lookup_handle exists in msg as aux data, is a replay */
    lh = flux_msg_aux_get (msg, "lookup_handle");
    if (!lh) {
        uint32_t rolemask, userid;

        if (flux_request_unpack (msg, NULL, "{ s:s s:s s:o s:i }",
                                 "key", &key,
                                 "namespace", &namespace,
                                 "val", &oval,
                                 "flags", &flags) < 0) {
            flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
            goto done;
        }

        if (get_msg_cred (ctx, msg, &rolemask, &userid) < 0)
            goto done;

        if (!(lh = lookup_create (ctx->cache,
                                  ctx->krm,
                                  ctx->epoch,
                                  namespace,
                                  NULL,
                                  0,
                                  key,
                                  rolemask,
                                  userid,
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

        ret = lookup_set_current_epoch (lh, ctx->epoch);
        assert (ret == 0);

        isreplay = true;
    }

    lret = lookup (lh);
    if (lret == LOOKUP_PROCESS_ERROR) {
        errno = lookup_get_errnum (lh);
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE) {
        bool stall = false;
        struct kvsroot *root;

        namespace = lookup_missing_namespace (lh);
        assert (namespace);

        root = getroot (ctx, namespace, mh, msg, lh, lookup_request_cb,
                        &stall);
        assert (!root);

        if (stall)
            goto stall;
        goto done;
    }
    else if (lret == LOOKUP_PROCESS_LOAD_MISSING_REFS) {
        struct kvs_cb_data cbd;

        if (!(wait = wait_create_msg_handler (h, mh, msg, ctx,
                                              watch_request_cb)))
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
     * Arrange to wait on root->watchlist for each new transaction.
     * Reconstruct the payload with 'first' flag clear, and updated
     * value.
     */
    if (!out || !(flags & KVS_WATCH_ONCE)) {
        namespace = lookup_get_namespace (lh);
        assert (namespace);

        /* If lookup() succeeded, then namespace must still be valid */

        root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace);
        assert (root);

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

        if (!(watcher = wait_create_msg_handler (h, mh, cpy, ctx,
                                                 watch_request_cb)))
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
    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace)))
        goto done;

    if (check_user (ctx, root, msg) < 0)
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

static int finalize_transaction_req (treq_t *tr,
                                     const flux_msg_t *req,
                                     void *data)
{
    struct kvs_cb_data *cbd = data;

    if (flux_respond (cbd->ctx->h, req, cbd->errnum, NULL) < 0)
        flux_log_error (cbd->ctx->h, "%s: flux_respond", __FUNCTION__);

    return 0;
}

static void finalize_transaction_bynames (kvs_ctx_t *ctx, struct kvsroot *root,
                                          json_t *names, int errnum)
{
    int i, len;
    json_t *name;
    treq_t *tr;
    struct kvs_cb_data cbd = { .ctx = ctx, .errnum = errnum };

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
            if (treq_mgr_remove_transaction (root->trm, nameval) < 0)
                flux_log_error (ctx->h, "%s: treq_mgr_remove_transaction",
                                __FUNCTION__);
        }
    }
}

/* kvs.relaycommit (rank 0 only, no response).
 */
static void relaycommit_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                    const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    const char *name;
    int flags;
    json_t *ops = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:s s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &namespace,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }

    /* namespace must exist given we are on rank 0 */
    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace))) {
        flux_log (h, LOG_ERR, "%s: namespace %s not available",
                  __FUNCTION__, namespace);
        errno = ENOTSUP;
        goto error;
    }

    if (kvstxn_mgr_add_transaction (root->ktm, name, ops, flags) < 0) {
        flux_log_error (h, "%s: kvstxn_mgr_add_transaction",
                        __FUNCTION__);
        goto error;
    }

    return;

error:
    /* An error has occurred, so we will return an error similarly to
     * how an error would be returned via a transaction error in
     * kvstxn_apply().
     */
    if (error_event_send_to_name (ctx, namespace, name, errno) < 0)
        flux_log_error (h, "%s: error_event_send_to_name", __FUNCTION__);
}

/* kvs.commit
 * Sent from users to local kvs module.
 */
static void commit_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvsroot *root;
    const char *namespace;
    int saved_errno, flags;
    bool stall = false;
    json_t *ops = NULL;
    treq_t *tr;
    char *alt_ns = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:i }",
                             "ops", &ops,
                             "namespace", &namespace,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot_namespace_prefix (ctx,
                                           namespace,
                                           mh,
                                           msg,
                                           commit_request_cb,
                                           &stall,
                                           ops,
                                           &alt_ns))) {
        if (stall)
            goto stall;
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
                                        flags) < 0) {
            flux_log_error (h, "%s: kvstxn_mgr_add_transaction",
                            __FUNCTION__);
            goto error;
        }
    }
    else {
        flux_future_t *f;

        /* route to rank 0 as instance owner */
        if (!(f = flux_rpc_pack (h, "kvs.relaycommit", 0, FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:s s:i }",
                                 "ops", ops,
                                 "name", treq_get_name (tr),
                                 "namespace", alt_ns ? alt_ns :  namespace,
                                 "flags", flags))) {
            flux_log_error (h, "%s: flux_rpc_pack", __FUNCTION__);
            goto error;
        }
        flux_future_destroy (f);
    }
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
stall:
    free (alt_ns);
    return;
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
    int saved_errno, nprocs, flags;
    json_t *ops = NULL;
    treq_t *tr;

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
    if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace))) {
        flux_log (h, LOG_ERR, "%s: namespace %s not available",
                  __FUNCTION__, namespace);
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
                                        treq_get_flags (tr)) < 0) {
            flux_log_error (h, "%s: kvstxn_mgr_add_transaction",
                            __FUNCTION__);
            goto error;
        }
    }

    return;

error:
    /* An error has occurred, so we will return an error similarly to
     * how an error would be returned via a transaction error in
     * kvstxn_apply().
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
    bool stall = false;
    json_t *ops = NULL;
    treq_t *tr;
    char *alt_ns = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:o s:s s:s s:i s:i }",
                             "ops", &ops,
                             "name", &name,
                             "namespace", &namespace,
                             "flags", &flags,
                             "nprocs", &nprocs) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot_namespace_prefix (ctx,
                                           namespace,
                                           mh,
                                           msg,
                                           fence_request_cb,
                                           &stall,
                                           ops,
                                           &alt_ns))) {
        if (stall)
            goto stall;
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
                                            treq_get_flags (tr)) < 0) {
                flux_log_error (h, "%s: kvstxn_mgr_add_transaction",
                                __FUNCTION__);
                goto error;
            }
        }
    }
    else {
        flux_future_t *f;

        /* route to rank 0 as instance owner */
        if (!(f = flux_rpc_pack (h, "kvs.relayfence", 0, FLUX_RPC_NORESPONSE,
                                 "{ s:O s:s s:s s:i s:i }",
                                 "ops", ops,
                                 "name", name,
                                 "namespace", alt_ns ? alt_ns : namespace,
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
stall:
    free (alt_ns);
    return;
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
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{ s:i s:s }",
                             "rootseq", &rootseq,
                             "namespace", &namespace) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(root = getroot (ctx, namespace, mh, msg, NULL, sync_request_cb,
                          &stall))) {
        if (stall)
            goto stall;
        goto error;
    }

    if (root->seq < rootseq) {
        if (!(wait = wait_create_msg_handler (h, mh, msg, arg,
                                              sync_request_cb)))
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
stall:
    return;
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
        if (!(root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace))) {
            flux_log (h, LOG_DEBUG, "namespace %s not available", namespace);
            errno = ENOTSUP;
            goto error;
        }

        if (check_user (ctx, root, msg) < 0)
            goto error;
    }
    else {
        /* If root is not initialized, we have to intialize ourselves
         * first.
         */
        bool stall = false;
        if (!(root = getroot (ctx, namespace, mh, msg, NULL,
                              getroot_request_cb, &stall))) {
            if (stall)
                goto done;
            goto error;
        }
    }

    /* N.B. owner cast into int */
    if (flux_respond_pack (h, msg, "{ s:i s:i s:s s:i }",
                           "owner", root->owner,
                           "rootseq", root->seq,
                           "rootref", root->ref,
                           "flags", root->flags) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

done:
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
     *   cleaning up lingering transactions.
     */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, namespace))) {
        flux_log (ctx->h, LOG_ERR, "%s: received unknown namespace %s",
                  __FUNCTION__, namespace);
        return;
    }

    finalize_transaction_bynames (ctx, root, names, errnum);
}

/* Optimization: the current rootdir object is optionally included
 * in the kvs.setroot event.  Prime the local cache with it.
 * If there are complications, just skip it.  Not critical.
 */
static void prime_cache_with_rootdir (kvs_ctx_t *ctx, json_t *rootdir)
{
    struct cache_entry *entry;
    char ref[BLOBREF_MAX_STRING_SIZE];
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
    if (blobref_hash (ctx->hash_name, data, len, ref, sizeof (ref)) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto done;
    }
    if ((entry = cache_lookup (ctx->cache, ref, ctx->epoch)))
        goto done; // already in cache, possibly dirty/invalid - we don't care
    if (!(entry = cache_entry_create (ref))) {
        flux_log_error (ctx->h, "%s: cache_entry_create", __FUNCTION__);
        goto done;
    }
    if (cache_entry_set_raw (entry, data, len) < 0) {
        flux_log_error (ctx->h, "%s: cache_entry_set_raw", __FUNCTION__);
        cache_entry_destroy (entry);
        goto done;
    }
    if (cache_insert (ctx->cache, entry) < 0) {
        flux_log_error (ctx->h, "%s: cache_insert", __FUNCTION__);
        cache_entry_destroy (entry);
        goto done;
    }
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
     *   order (commit/fence completes before namespace removed, but
     *   namespace remove event received before setroot).
     */
    if (!(root = kvsroot_mgr_lookup_root (ctx->krm, namespace))) {
        flux_log (ctx->h, LOG_ERR, "%s: received unknown namespace %s",
                  __FUNCTION__, namespace);
        return;
    }

    /* in rare chance we receive setroot on removed namespace, return
     * ENOTSUP to client callers */
    if (root->remove)
        errnum = ENOTSUP;

    finalize_transaction_bynames (ctx, root, names, errnum);

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

static int disconnect_request_root_cb (struct kvsroot *root, void *arg)
{
    struct kvs_cb_data *cbd = arg;

    /* Log error, but don't return -1, can continue to iterate
     * remaining roots */
    if (wait_destroy_msg (root->watchlist, disconnect_cmp, cbd->sender) < 0)
        flux_log_error (cbd->ctx->h, "%s: wait_destroy_msg", __FUNCTION__);

    return 0;
}

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    struct kvs_cb_data cbd;
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
    cbd.ctx = ctx;
    cbd.sender = sender;
    if (kvsroot_mgr_iter_roots (ctx->krm, disconnect_request_root_cb, &cbd) < 0)
        flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);

    if (cache_wait_destroy_msg (ctx->cache, disconnect_cmp, sender) < 0)
        flux_log_error (h, "%s: wait_destroy_msg", __FUNCTION__);
    free (sender);
}

static int stats_get_root_cb (struct kvsroot *root, void *arg)
{
    json_t *nsstats = arg;
    json_t *s;

    if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                         "#watchers",
                         wait_queue_length (root->watchlist),
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

    json_object_set_new (nsstats, root->namespace, s);
    return 0;
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
    if (kvsroot_mgr_root_count (ctx->krm) > 0) {
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

    if (kvsroot_mgr_root_count (ctx->krm) > 0) {
        if (kvsroot_mgr_iter_roots (ctx->krm, stats_get_root_cb, nsstats) < 0) {
            flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
            goto done;
        }
    }
    else {
        json_t *s;

        if (!(s = json_pack ("{ s:i s:i s:i s:i s:i }",
                             "#watchers", 0,
                             "#no-op stores", 0,
                             "#transactions", 0,
                             "#readytransactions", 0,
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

static int stats_clear_root_cb (struct kvsroot *root, void *arg)
{
    kvstxn_mgr_clear_noop_stores (root->ktm);
    return 0;
}

static void stats_clear (kvs_ctx_t *ctx)
{
    ctx->faults = 0;

    if (kvsroot_mgr_iter_roots (ctx->krm, stats_clear_root_cb, NULL) < 0)
        flux_log_error (ctx->h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
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

static int namespace_create (kvs_ctx_t *ctx, const char *namespace,
                             uint32_t owner, int flags)
{
    struct kvsroot *root;
    json_t *rootdir = NULL;
    char ref[BLOBREF_MAX_STRING_SIZE];
    void *data = NULL;
    flux_msg_t *msg = NULL;
    char *topic = NULL;
    int len;
    int rv = -1;

    /* If namespace already exists, return EEXIST.  Doesn't matter if
     * namespace is in process of being removed */
    if (kvsroot_mgr_lookup_root (ctx->krm, namespace)) {
        errno = EEXIST;
        return -1;
    }

    if (!(root = kvsroot_mgr_create_root (ctx->krm,
                                          ctx->cache,
                                          ctx->hash_name,
                                          namespace,
                                          owner,
                                          flags))) {
        flux_log_error (ctx->h, "%s: kvsroot_mgr_create_root", __FUNCTION__);
        return -1;
    }

    if (!(rootdir = treeobj_create_dir ())) {
        flux_log_error (ctx->h, "%s: treeobj_create_dir", __FUNCTION__);
        goto cleanup;
    }

    if (!(data = treeobj_encode (rootdir))) {
        flux_log_error (ctx->h, "%s: treeobj_encode", __FUNCTION__);
        goto cleanup;
    }
    len = strlen (data);

    if (blobref_hash (ctx->hash_name, data, len, ref, sizeof (ref)) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto cleanup;
    }

    setroot (ctx, root, ref, 0);

    if (event_subscribe (ctx, namespace) < 0) {
        flux_log_error (ctx->h, "%s: event_subscribe", __FUNCTION__);
        goto cleanup;
    }

    if (asprintf (&topic, "kvs.namespace-created-%s", namespace) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (!(msg = flux_event_pack (topic,
                                 "{ s:s s:i s:s s:i }",
                                 "namespace", root->namespace,
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
        kvsroot_mgr_remove_root (ctx->krm, namespace);
    free (data);
    json_decref (rootdir);
    free (topic);
    flux_msg_destroy (msg);
    return rv;
}

static void namespace_create_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                         const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    const char *namespace;
    uint32_t owner;
    int flags;

    assert (ctx->rank == 0);

    /* N.B. owner read into uint32_t */
    if (flux_request_unpack (msg, NULL, "{ s:s s:i s:i }",
                             "namespace", &namespace,
                             "owner", &owner,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (owner == FLUX_USERID_UNKNOWN)
        owner = geteuid ();

    if (namespace_create (ctx, namespace, owner, flags) < 0)
        goto error;

    errno = 0;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
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
            flux_log_error (cbd->ctx->h, "%s: json_pack", __FUNCTION__);
            errno = ENOMEM;
            return -1;
        }

        finalize_transaction_bynames (cbd->ctx, cbd->root, names, ENOTSUP);
        json_decref (names);
    }
    return 0;
}

static void start_root_remove (kvs_ctx_t *ctx, const char *namespace)
{
    struct kvsroot *root;

    /* safe lookup, if root removal in process, let it continue */
    if ((root = kvsroot_mgr_lookup_root_safe (ctx->krm, namespace))) {
        struct kvs_cb_data cbd = { .ctx = ctx, .root = root };

        root->remove = true;

        /* Now that root has been marked for removal from roothash, run
         * the watchlist.  watch requests will notice root removed, return
         * ENOTSUP to watchers.
         */

        if (wait_runqueue (root->watchlist) < 0)
            flux_log_error (ctx->h, "%s: wait_runqueue", __FUNCTION__);

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

static int namespace_remove (kvs_ctx_t *ctx, const char *namespace)
{
    flux_msg_t *msg = NULL;
    int saved_errno, rc = -1;
    char *topic = NULL;

    /* Namespace doesn't exist or is already in process of being
     * removed */
    if (!kvsroot_mgr_lookup_root_safe (ctx->krm, namespace)) {
        /* silently succeed */
        goto done;
    }

    if (asprintf (&topic, "kvs.namespace-removed-%s", namespace) < 0) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    if (!(msg = flux_event_pack (topic, "{ s:s }", "namespace", namespace))) {
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
    free (topic);
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

static void namespace_removed_event_cb (flux_t *h, flux_msg_handler_t *mh,
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

static int namespace_list_cb (struct kvsroot *root, void *arg)
{
    json_t *namespaces = arg;
    json_t *o;

    /* do not list namespaces marked for removal */
    if (root->remove)
        return 0;

    if (!(o = json_pack ("{ s:s s:i s:i }",
                         "namespace", root->namespace,
                         "owner", root->owner,
                         "flags", root->flags))) {
        errno = ENOMEM;
        return -1;
    }

    json_array_append_new (namespaces, o);
    return 0;
}

static void namespace_list_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                       const flux_msg_t *msg, void *arg)
{
    kvs_ctx_t *ctx = arg;
    json_t *namespaces = NULL;
    int rc = -1;

    if (!(namespaces = json_array ())) {
        errno = ENOMEM;
        goto done;
    }

    if (kvsroot_mgr_iter_roots (ctx->krm, namespace_list_cb,
                                namespaces) < 0) {
        flux_log_error (h, "%s: kvsroot_mgr_iter_roots", __FUNCTION__);
        goto done;
    }

    if (flux_respond_pack (h, msg, "{ s:O }",
                           "namespaces",
                           namespaces) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto done;
    }

    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    json_decref (namespaces);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.get",  stats_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.clear",stats_clear_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.stats.clear",stats_clear_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.setroot-*",  setroot_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.error-*",    error_event_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",
                            getroot_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",  dropcache_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.dropcache",  dropcache_event_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "hb",             heartbeat_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect", disconnect_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.unwatch",
                            unwatch_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.sync",
                            sync_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.lookup",
                            lookup_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",
                            watch_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.commit",
                            commit_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.relaycommit", relaycommit_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.fence",
                            fence_request_cb, FLUX_ROLE_USER },
    { FLUX_MSGTYPE_REQUEST, "kvs.relayfence", relayfence_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.namespace-create",
                            namespace_create_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.namespace-remove",
                            namespace_remove_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "kvs.namespace-removed-*",
                            namespace_removed_event_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs.namespace-list",
                            namespace_list_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void process_args (kvs_ctx_t *ctx, int ac, char **av)
{
    int i;

    for (i = 0; i < ac; i++) {
        if (strncmp (av[i], "transaction-merge=", 13) == 0)
            ctx->transaction_merge = strtoul (av[i]+13, NULL, 10);
        else
            flux_log (ctx->h, LOG_ERR, "Unknown option `%s'", av[i]);
    }
}

/* Store initial root in local cache, and flush to content cache
 * synchronously.  The corresponding blobref is written into 'ref'.
 */
static int store_initial_rootdir (kvs_ctx_t *ctx, char *ref, int ref_len)
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
    if (blobref_hash (ctx->hash_name, data, len, ref, ref_len) < 0) {
        flux_log_error (ctx->h, "%s: blobref_hash", __FUNCTION__);
        goto error;
    }
    if (!(entry = cache_lookup (ctx->cache, ref, ctx->epoch))) {
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
        char rootref[BLOBREF_MAX_STRING_SIZE];
        uint32_t owner = geteuid ();

        if (store_initial_rootdir (ctx, rootref, sizeof (rootref)) < 0) {
            flux_log_error (h, "storing initial root object");
            goto done;
        }

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
