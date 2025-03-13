/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* kvs-watcher - track KVS changes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_util_private.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "src/common/libutil/errprintf.h"

/* State for one watcher */
struct watcher {
    const flux_msg_t *request;  // request message
    struct flux_msg_cred cred;  // request cred
    int rootseq;                // last root sequence number sent
    bool canceled;              // true if watcher has been canceled
    bool mute;                  // true if response should be suppressed
    bool responded;             // true if watcher has responded atleast once
    bool initial_rpc_sent;      // flag is initial watch rpc sent
    bool initial_rpc_received;  // flag is initial watch rpc received
    bool finished;              // flag indicates if watcher is finished
    int initial_rootseq;        // initial rootseq returned by initial rpc
    char *key;                  // lookup key
    int flags;                  // kvs_lookup flags
    zlist_t *lookups;           // list of futures, in commit order
    zlist_t *loads;             // list of futures, content loads in ref order

    struct ns_monitor *nsm;     // back pointer for removal
    json_t *prev;               // previous watch value for KVS_WATCH_FULL/UNIQ
    bool index_valid;           // flag if prev_start_index/prev_end_index set
    int prev_start_index;       // previous start index loaded
    int prev_end_index;         // previous end index loaded
    int loaded_blob_count;      // number of indices loaded (for FLUX_KVS_STREAM)
    void *handle;               // zlistx_t handle
};

/* Current KVS root.
 */
struct commit {
    char *rootref;              // current root blobref
    int rootseq;                // current root sequence number
    json_t *keys;               // keys changed by commit
                                //  empty if data originates from getroot RPC
                                //  or kvs.namespace-<NS>-created event
};


/* State for monitoring a KVS namespace.
 */
struct ns_monitor {
    char *ns_name;              // namespace name, hash key for ctx->namespaces
    uint32_t owner;             // namespace owner (userid)
    struct commit *commit;      // current commit data
    int fatal_errnum;           // non-skippable error pending for all watchers
    int errnum;                 // if non-zero, error pending for all watchers
    struct watch_ctx *ctx;      // back-pointer to watch_ctx
    zlistx_t *watchers;         // list of watchers of this namespace
    char *topic;                // topic string for subscription
    bool subscribed;            // subscription active
    flux_future_t *getrootf;    // initial getroot future
    flux_future_t *eventsubf;   // for event subscription
};

/* Module state.
 */
struct watch_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zhashx_t *namespaces;        // hash of monitored namespaces
};

static void watcher_destroy (struct watcher *w)
{
    if (w) {
        int saved_errno = errno;
        flux_msg_decref (w->request);
        free (w->key);
        if (w->lookups) {
            flux_future_t *f;
            while ((f = zlist_pop (w->lookups)))
                flux_future_destroy (f);
            zlist_destroy (&w->lookups);
        }
        if (w->loads) {
            flux_future_t *f;
            while ((f = zlist_pop (w->loads)))
                flux_future_destroy (f);
            zlist_destroy (&w->loads);
        }
        json_decref (w->prev);
        free (w);
        errno = saved_errno;
    }
}

static struct watcher *watcher_create (const flux_msg_t *msg,
                                       const char *key,
                                       int flags)
{
    struct watcher *w;

    if (!(w = calloc (1, sizeof (*w))))
        return NULL;
    w->request = flux_msg_incref (msg);
    if (flux_msg_get_cred (msg, &w->cred) < 0)
        goto error;
    if (!(w->key = kvs_util_normalize_key (key, NULL)))
        goto error;
    if (!(w->lookups = zlist_new ()))
        goto error_nomem;
    if (!(w->loads = zlist_new ()))
        goto error_nomem;
    w->flags = flags;
    w->rootseq = -1;
    return w;
error_nomem:
    errno = ENOMEM;
error:
    watcher_destroy (w);
    return NULL;
}

static void commit_destroy (struct commit *commit)
{
    if (commit) {
        int saved_errno = errno;
        free (commit->rootref);
        if (commit->keys)
            json_decref (commit->keys);
        free (commit);
        errno = saved_errno;
    }
}

static struct commit *commit_create (const char *rootref,
                                     int rootseq,
                                     json_t *keys)
{
    struct commit *commit = calloc (1, sizeof (*commit));
    if (!commit)
        return NULL;
    if (!(commit->rootref = strdup (rootref))) {
        commit_destroy (commit);
        return NULL;
    }
    /* keys can be NULL */
    commit->keys = json_incref (keys);
    commit->rootseq = rootseq;
    return commit;
}

static void namespace_destroy (void **data)
{
    if (data) {
        struct ns_monitor *nsm = *data;
        int saved_errno = errno;
        commit_destroy (nsm->commit);
        zlistx_destroy (&nsm->watchers);
        if (nsm->subscribed) {
            flux_future_t *f;
            if (!(f = flux_event_unsubscribe_ex (nsm->ctx->h,
                                                 nsm->topic,
                                                 FLUX_RPC_NORESPONSE)))
                flux_log_error (nsm->ctx->h, "flux_event_unsubscribe_ex");
            flux_future_destroy (f);
        }
        free (nsm->topic);
        free (nsm->ns_name);
        flux_future_destroy (nsm->getrootf);
        flux_future_destroy (nsm->eventsubf);
        free (nsm);
        errno = saved_errno;
    }
}

static void watcher_destructor (void **item)
{
    if (item) {
        struct watcher *w = *item;
        watcher_destroy (w);
        *item = NULL;
    }
}

static struct ns_monitor *namespace_create (struct watch_ctx *ctx,
                                            const char *ns)
{
    struct ns_monitor *nsm = calloc (1, sizeof (*nsm));
    if (!nsm)
        return NULL;
    nsm->ctx = ctx;
    if (!(nsm->watchers = zlistx_new ()))
        goto error;
    zlistx_set_destructor (nsm->watchers, watcher_destructor);
    if (!(nsm->ns_name = strdup (ns)))
        goto error;
    nsm->owner = FLUX_USERID_UNKNOWN;
    return nsm;
error:
    namespace_destroy ((void **)&nsm);
    return NULL;
}

/* Helper for watcher_respond - is key a member of object?
 * N.B. object 'o' can be NULL
 */
static bool key_match (json_t *o, const char *key)
{
    if (o && json_object_get (o, key))
        return true;
    return false;
}

static void watcher_cleanup (struct ns_monitor *nsm, struct watcher *w)
{
    /* it is possible lookups & loads are in flight, they will be
     * cleaned in watcher_destroy() */
    zlistx_delete (nsm->watchers, w->handle);
    /* under extremely racy scenarios, it is possible getroot or
     * event_subscribe is in flight and not complete, but we will destroy
     * namespace_monitor if there are no watchers.
     */
    if (zlistx_size (nsm->watchers) == 0)
        zhashx_delete (nsm->ctx->namespaces, nsm->ns_name);
}

static void handle_load_response (flux_future_t *f, struct watcher *w)
{
    flux_t *h = flux_future_get_flux (f);
    const void *data;
    size_t size;
    flux_error_t err;

    if (content_load_get (f, &data, &size) < 0) {
        errprintf (&err, "failed to load content data");
        goto error_respond;
    }

    if (!w->mute) {
        json_t *val = treeobj_create_val (data, size);
        if (!val) {
            errprintf (&err, "failed to create treeobj value");
            goto error_respond;
        }
        if (flux_respond_pack (h, w->request, "{ s:o }", "val", val) < 0) {
            flux_log_error (h,
                            "%s: failed to respond to kvs-watch.lookup",
                            __FUNCTION__);
            json_decref (val);
            goto finished;
        }
        w->loaded_blob_count++;
        w->responded = true;
    }

    return;
error_respond:
    if (!w->mute) {
        if (flux_respond_error (h, w->request, errno, err.text) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
finished:
    w->finished = true;
}

static void load_continuation (flux_future_t *f, void *arg)
{
    struct watcher *w = arg;
    struct ns_monitor *nsm = w->nsm;

    while ((f = zlist_first (w->loads)) && flux_future_is_ready (f)) {
        f = zlist_pop (w->loads);
        if (!w->finished)
            handle_load_response (f, w);
        flux_future_destroy (f);
        /* if WAITCREATE and (!WATCH and !STREAM), then we only care
         * about sending one response and being done.  We can use the
         * responded flag to indicate that condition.
         */
        if (w->responded
            && (w->flags & FLUX_KVS_WAITCREATE)
            && (!(w->flags & FLUX_KVS_WATCH)
                && !(w->flags & FLUX_KVS_STREAM)))
            w->finished = true;
    }
    if ((w->flags & FLUX_KVS_STREAM)
        && w->responded
        && w->index_valid
        && (w->loaded_blob_count
            == (w->prev_end_index - w->prev_start_index + 1))) {
        if (!w->mute) {
            if (flux_respond_error (w->nsm->ctx->h,
                                    w->request,
                                    ENODATA,
                                    NULL) < 0)
                flux_log_error (w->nsm->ctx->h,
                                "%s: flux_respond_error",
                                __FUNCTION__);
        }
        w->finished = true;
    }
    if (w->finished)
        watcher_cleanup (nsm, w);
}

static flux_future_t *load_ref (flux_t *h, struct watcher *w, const char *ref)
{
    flux_future_t *f = NULL;

    if (!(f = content_load_byblobref (h, ref, 0))
        || flux_future_then (f, -1., load_continuation, w) < 0)
        goto error;
    if (zlist_append (w->loads, f) < 0) {
        errno = ENOMEM;
        goto error;
    }

    return f;

error:
    flux_future_destroy (f);
    return NULL;
}

static int load_range (flux_t *h,
                       struct watcher *w,
                       int start_index,
                       int end_index,
                       json_t *val)
{
    int i;

    for (i = start_index; i <= end_index; i++) {
        flux_future_t *f;
        const char *ref = treeobj_get_blobref (val, i);
        if (!ref)
            return -1;
        if (!(f = load_ref (h, w, ref)))
            return -1;
    }
    return 0;
}

static int handle_initial_response (flux_t *h,
                                    struct watcher *w,
                                    json_t *val,
                                    const char *root_ref,
                                    int root_seq,
                                    const char *namespace)
{
    flux_error_t err;

    /* this is the first response case, store the first response
     * val */
    if ((w->flags & FLUX_KVS_WATCH_FULL)
        || (w->flags & FLUX_KVS_WATCH_UNIQ))
        w->prev = json_incref (val);

    if ((w->flags & FLUX_KVS_WATCH_APPEND)
        || (w->flags & FLUX_KVS_STREAM)) {
        /* The very first response may be a 'val' treeobj instead of
         * 'valref', if there have been no appends yet.
         */
        if (treeobj_is_val (val)) {
            w->index_valid = true;
            w->prev_start_index = 0;
            w->prev_end_index = 0;
            /* since this is a val object, we can just return it */
            w->loaded_blob_count++;
            goto out;
        }
        else if (treeobj_is_valref (val)) {
            w->index_valid = true;
            w->prev_start_index = 0;
            w->prev_end_index = treeobj_get_count (val) - 1;
        }
        else {
            if (w->flags & FLUX_KVS_WATCH_APPEND)
                errprintf (&err,
                           "%s cannot be watched with WATCH_APPEND",
                           treeobj_type_name (val));
            else
                errprintf (&err,
                           "%s cannot be streamed",
                           treeobj_type_name (val));
            if (treeobj_is_dir (val)
                || treeobj_is_dirref (val))
                errno = EISDIR;
            else
                errno = EINVAL;
            goto error_respond;
        }

        if (load_range (h,
                        w,
                        w->prev_start_index,
                        w->prev_end_index,
                        val) < 0) {
            errprintf (&err,
                       "error sending request for content blobs [%d:%d]",
                       w->prev_start_index,
                       w->prev_end_index);
            goto error_respond;
        }

        w->initial_rootseq = root_seq;
        return 0;
    }

out:
    if (flux_respond_pack (h, w->request, "{ s:O }", "val", val) < 0) {
        flux_log_error (h,
                        "%s: failed to respond to kvs-watch.lookup",
                        __FUNCTION__);
        return -1;
    }

    w->initial_rootseq = root_seq;
    w->responded = true;
    return 0;

error_respond:
    if (!w->mute) {
        if (flux_respond_error (h, w->request, errno, err.text) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
    return -1;
}

static int handle_compare_response (flux_t *h,
                                    struct watcher *w,
                                    json_t *val)
{
    if (!w->responded) {
        /* this is the first response case, store the first response
         * val.  This is here b/c initial response could have been
         * ENOENT case */
        w->prev = json_incref (val);

        if (flux_respond_pack (h, w->request, "{ s:O }", "val", val) < 0) {
            flux_log_error (h,
                            "%s: failed to respond to kvs-watch.lookup",
                            __FUNCTION__);
            return -1;
        }

        w->responded = true;
    }
    else {
        /* not first response case, compare to previous to see if
         * respond should be done, update if necessary */
        if (json_equal (w->prev, val))
            return 0;

        json_decref (w->prev);
        w->prev = json_incref (val);

        if (flux_respond_pack (h, w->request, "{ s:O }", "val", val) < 0) {
            flux_log_error (h,
                            "%s: failed to respond to kvs-watch.lookup",
                            __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

static int handle_append_response (flux_t *h,
                                   struct watcher *w,
                                   json_t *val,
                                   const char *root_ref,
                                   int root_seq,
                                   const char *namespace)
{
    flux_error_t err;

    if (!w->responded) {
        /* this is the first response case, store the first response
         * info.  This is here b/c initial response could have been
         * ENOENT case.
         *
         * The very first response may be a 'val' treeobj instead of
         * 'valref', if there have been no appends yet.
         */
        if (treeobj_is_val (val)) {
            w->index_valid = true;
            w->prev_start_index = 0;
            w->prev_end_index = 0;
            /* since this is a val object, we can just return it */
            if (flux_respond_pack (h, w->request, "{ s:O }", "val", val) < 0) {
                flux_log_error (h,
                                "%s: failed to respond to kvs-watch.lookup",
                                __FUNCTION__);
                goto error_out;
            }
            w->loaded_blob_count++;
            w->responded = true;
        }
        else if (treeobj_is_valref (val)) {
            /* N.B. It may not be obvious why we have to check
             * w->index_valid if we have not yet responded.  It is
             * possible we have received a setroot response and an
             * updated valref before loads from the content store have
             * returned to the caller.
             */
            if (w->index_valid) {
                int new_end_index;
                if (w->flags & FLUX_KVS_STREAM)
                    goto out;
                new_end_index = treeobj_get_count (val) - 1;
                if (new_end_index > w->prev_end_index) {
                    w->prev_start_index = w->prev_end_index + 1;
                    w->prev_end_index = new_end_index;
                }
                else if (new_end_index < w->prev_end_index) {
                    errprintf (&err, "key watched with WATCH_APPEND truncated");
                    errno = EINVAL;
                    goto error_respond;
                }
                else
                    goto out;
            }
            else {
                w->index_valid = true;
                w->prev_start_index = 0;
                w->prev_end_index = treeobj_get_count (val) - 1;
            }

            if (load_range (h,
                            w,
                            w->prev_start_index,
                            w->prev_end_index,
                            val) < 0) {
                errprintf (&err,
                           "error sending request for content blobs [%d:%d]",
                           w->prev_start_index,
                           w->prev_end_index);
                goto error_respond;
            }
        }
        else {
            if (w->flags & FLUX_KVS_WATCH_APPEND)
                errprintf (&err,
                           "%s cannot be watched with WATCH_APPEND",
                           treeobj_type_name (val));
            else
                errprintf (&err,
                           "%s cannot be streamed",
                           treeobj_type_name (val));
            if (treeobj_is_dir (val)
                || treeobj_is_dirref (val))
                errno = EISDIR;
            else
                errno = EINVAL;
            goto error_respond;
        }
    }
    else {
        if (treeobj_is_valref (val)) {
            int new_end_index;
            if (!w->index_valid) {
                errno = EPROTO;
                goto error_respond;
            }
            if (w->flags & FLUX_KVS_STREAM)
                goto out;
            new_end_index = treeobj_get_count (val) - 1;
            if (new_end_index > w->prev_end_index) {
                w->prev_start_index = w->prev_end_index + 1;
                w->prev_end_index = new_end_index;
            }
            else if (new_end_index < w->prev_end_index) {
                errprintf (&err, "key watched with WATCH_APPEND shortened");
                errno = EINVAL;
                goto error_respond;
            }
            else
                goto out;

            if (load_range (h,
                            w,
                            w->prev_start_index,
                            w->prev_end_index,
                            val) < 0) {
                errprintf (&err, "error loading reference");
                goto error_respond;
            }
        }
        else {
            /* If we're streaming, we don't care that the treeobject
             * was overwritten */
            if (w->flags & FLUX_KVS_STREAM)
                goto out;
            errprintf (&err,
                       "value of key watched with WATCH_APPEND overwritten");
            errno = EINVAL;
            goto error_respond;
        }
    }

out:
    return 0;

error_respond:
    if (!w->mute) {
        if (flux_respond_error (h, w->request, errno, err.text) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
error_out:
    return -1;
}

static int handle_normal_response (flux_t *h,
                                   struct watcher *w,
                                   json_t *val)
{
    if (flux_respond_pack (h, w->request, "{ s:O }", "val", val) < 0) {
        flux_log_error (h,
                        "%s: failed to respond to kvs-watch.lookup",
                        __FUNCTION__);
        return -1;
    }

    w->responded = true;
    return 0;
}

/* New value of key is available in future 'f' container.
 * Send response to watcher using raw payload from lookup response.
 * Return 0 on success, -1 on error (caller should destroy watcher).
 *
 * Special handling done for FLUX_KVS_WATCH_FULL/UNIQ/APPEND, must do
 * some comparisons before returning.
 */
static void handle_lookup_response (flux_future_t *f,
                                    struct watcher *w)
{
    flux_t *h = flux_future_get_flux (f);
    int errnum;
    const char *root_ref;
    int root_seq;
    json_t *val;

    if (flux_future_aux_get (f, "initial")) {

        w->initial_rpc_received = true;

        /* First check for ENOENT */
        if (!flux_rpc_get_unpack (f,
                                  "{ s:i s:i }",
                                  "errno", &errnum,
                                  "rootseq", &root_seq)) {
            assert (errnum == ENOENT);
            if ((w->flags & FLUX_KVS_WAITCREATE)
                && w->responded == false) {
                w->initial_rootseq = root_seq;
                return;
            }
            errno = errnum;
            goto error;
        }

        if (flux_rpc_get_unpack (f,
                                 "{ s:o s:s s:i }",
                                 "val", &val,
                                 "rootref", &root_ref,
                                 "rootseq", &root_seq) < 0) {
            /* It is worth mentioning ENOTSUP error conditions here.
             *
             * Recall that in namespace_monitor(), an initial getroot
             * call is done.  If an ENOTSUP occurs on that getroot
             * call, in watcher_respond(), WAITCREATE will be handled.
             *
             * We cannot reach this function / point in the code if
             * the namespace has not been created.  So an ENOTSUP here
             * must mean that the namespace has been removed, but we
             * did not yet receive the kvs.namespace-<NS>-removed event.
             * We can safely return ENOTSUP to the user.
             *
             * Note that kvs-watch does not handle monitoring of
             * namespaces being removed and re-created.  On a
             * kvs.namespace-<NS>removed event, monitoring in a namespace
             * is torn down.  See fatal_errnum var.
             */
            goto error;
        }

        if (handle_initial_response (h,
                                     w,
                                     val,
                                     root_ref,
                                     root_seq,
                                     w->nsm->ns_name) < 0)
            goto finished;
    }
    else {
        /* First check for ENOENT */
        if (!flux_rpc_get_unpack (f, "{ s:i }", "errno", &errnum)) {
            assert (errnum == ENOENT);
            errno = errnum;
            goto error;
        }

        if (flux_rpc_get_unpack (f,
                                 "{ s:o s:s s:i }",
                                 "val", &val,
                                 "rootref", &root_ref,
                                 "rootseq", &root_seq) < 0)
            goto error;

        /* if we got some setroots before the initial rpc returned,
         * toss them */
        if (root_seq <= w->initial_rootseq)
            return;

        if (!w->mute) {
            if ((w->flags & FLUX_KVS_WATCH_FULL)
                || (w->flags & FLUX_KVS_WATCH_UNIQ)) {
                if (handle_compare_response (h, w, val) < 0)
                    goto finished;
            }
            else if ((w->flags & FLUX_KVS_WATCH_APPEND)
                     || (w->flags & FLUX_KVS_STREAM)) {
                if (handle_append_response (h,
                                            w,
                                            val,
                                            root_ref,
                                            root_seq,
                                            w->nsm->ns_name) < 0)
                    goto finished;
            }
            else {
                if (handle_normal_response (h, w, val) < 0)
                    goto finished;
            }
        }
    }

    if ((w->flags & FLUX_KVS_STREAM)
        && w->responded
        && w->index_valid
        && (w->loaded_blob_count
            == (w->prev_end_index - w->prev_start_index + 1))) {
        errno = ENODATA;
        goto error;
    }

    return;
error:
    if (!w->mute) {
        if (flux_respond_error (h, w->request, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
finished:
    w->finished = true;
}

/* One lookup has completed.
 * Pop ready futures off w->lookups and send responses, until
 * the list is empty, or a non-ready future is encountered.
 */
static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct watcher *w = arg;
    struct ns_monitor *nsm = w->nsm;

    while ((f = zlist_first (w->lookups)) && flux_future_is_ready (f)) {
        f = zlist_pop (w->lookups);
        if (!w->finished)
            handle_lookup_response (f, w);
        flux_future_destroy (f);
        /* if WAITCREATE and (!WATCH and !STREAM), then we only care
         * about sending one response and being done.  We can use the
         * responded flag to indicate that condition.
         */
        if (w->responded
            && (w->flags & FLUX_KVS_WAITCREATE)
            && (!(w->flags & FLUX_KVS_WATCH)
                && !(w->flags & FLUX_KVS_STREAM)))
            w->finished = true;
    }
    if (w->finished)
        watcher_cleanup (nsm, w);
}

/* Like flux_kvs_lookupat() except:
 * - targets kvs.lookup-plus, so root_ref & root_seq are available in
 *   response
 * - blobref param replaces treeobj
 * - namespace param (ignores namespace associated with flux_t handle)
 * - cred params (see N.B. below)
 * Use flux_rpc_get() not flux_kvs_lookup_get() to access the response.
 */
static flux_future_t *lookupat (flux_t *h,
                                struct watcher *w,
                                const char *blobref,
                                int root_seq,
                                const char *ns)
{
    flux_msg_t *msg;
    json_t *o = NULL;
    flux_future_t *f;
    int saved_errno;
    int flags = w->flags;

    if (!(msg = flux_request_encode ("kvs.lookup-plus", NULL)))
        return NULL;
    if ((flags & FLUX_KVS_WATCH_APPEND)
        || (flags & FLUX_KVS_STREAM))
        flags |= FLUX_KVS_TREEOBJ;
    if (!w->initial_rpc_sent) {
        if (flux_msg_pack (msg,
                           "{s:s s:s s:i}",
                           "key", w->key,
                           "namespace", ns,
                           "flags", flags) < 0)
            goto error;
    }
    else {
        if (!(o = treeobj_create_dirref (blobref)))
            goto error;
        if (flux_msg_pack (msg,
                           "{s:s s:i s:i s:O}",
                           "key", w->key,
                           "flags", flags,
                           "rootseq", root_seq,
                           "rootdir", o) < 0)
            goto error;
    }
    /* N.B. Since this module is authenticated to the shmem:// connector
     * with FLUX_ROLE_OWNER, we are allowed to switch the message credentials
     * in this request message, and not be overridden at the connector,
     * as would be the case if we were not sufficiently privileged.
     */
    if (flux_msg_set_cred (msg, w->cred) < 0)
        goto error;
    if (!(f = flux_rpc_message (h, msg, FLUX_NODEID_ANY, 0)))
        goto error;
    if (!w->initial_rpc_sent) {
        /* just need to set an aux as a flag, pointer to 'f' as aux
         * data is random pointer choice */
        if (flux_future_aux_set (f, "initial", f, NULL) < 0) {
            flux_future_destroy (f);
            goto error;
        }
    }
    w->initial_rpc_sent = true;
    flux_msg_destroy (msg);
    json_decref (o);
    return f;
error:
    saved_errno = errno;
    json_decref (o);
    flux_msg_destroy (msg);
    errno = saved_errno;
    return NULL;
}

static int process_lookup_response (struct ns_monitor *nsm, struct watcher *w)
{
    flux_future_t *f;
    if (!(f = lookupat (nsm->ctx->h,
                        w,
                        nsm->commit->rootref,
                        nsm->commit->rootseq,
                        nsm->ns_name))) {
        flux_log_error (nsm->ctx->h, "%s: lookupat", __FUNCTION__);
        return -1;
    }
    if (zlist_append (w->lookups, f) < 0) {
        flux_future_destroy (f);
        errno = ENOMEM;
        return -1;
    }
    if (flux_future_then (f, -1., lookup_continuation, w) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    w->rootseq = nsm->commit->rootseq;
    return 0;
}

/* Respond to watcher request, if appropriate.
 * De-list and destroy watcher from namespace on error.
 * De-hash and destroy namespace if watchers list becomes empty.
 */
static void watcher_respond (struct ns_monitor *nsm, struct watcher *w)
{
    /* If this watcher is already done, we should ignore namespace
     * remove, setroot, cancel, etc.  that leads us here.  Just goto
     * 'finished'.
     */
    if (w->finished)
        goto finished;
    if (w->canceled) {
        errno = ENODATA;
        goto error_respond;
    }
    if (nsm->fatal_errnum != 0) {
        errno = nsm->fatal_errnum;
        goto error_respond;
    }
    if (nsm->errnum != 0) {
        /* if namespace not yet created, don't return error to user if
         * they want to wait */
        if ((w->flags & FLUX_KVS_WAITCREATE)
            && nsm->errnum == ENOTSUP
            && w->responded == false) {
            nsm->errnum = 0;
            return;
        }
        errno = nsm->errnum;
        goto error_respond;
    }
    /* This assert is safe, only potential case is if namespace
     * removed before initial getroot or a setroot received.  But that
     * case is handled by error handling above.
     */
    assert (nsm->commit != NULL);
    if (nsm->commit->rootseq <= w->rootseq)
        return;
    if (flux_msg_cred_authorize (w->cred, nsm->owner) < 0) {
        flux_log (nsm->ctx->h, LOG_DEBUG, "%s: auth failure", __FUNCTION__);
        goto error_respond;
    }
    /* flux_kvs_lookup (FLUX_KVS_WATCH)
     *
     * Ordering note: KVS lookups can be returned out of order because
     * they are processed asynchronously.  For example, some values
     * may be cached within the KVS while others are not.
     *
     * KVS lookup futures are added to the w->lookups zlist in commit
     * order here, and in lookup_continuation(), fulfilled futures are
     * popped off the head of w->lookups until an unfulfilled future
     * is encountered, so that responses are always returned to the
     * watcher in commit order.
     *
     * Security note: although the requester has already been authenticated
     * to access the namespace by check_authorization() above, we make the
     * kvs.lookupat request with the requester's creds, in case the key lookup
     * traverses to a new namespace.  Leave it up to the KVS module to ensure
     * the requester is permitted to access *that* namespace.
     *
     * Note on FLUX_KVS_WATCH_FULL: A lookup / comparison is done on every
     * change.
     */
    if (w->rootseq == -1
        || (w->flags & FLUX_KVS_WATCH_FULL)
        || key_match (nsm->commit->keys, w->key)) {
        if (process_lookup_response (nsm, w) < 0)
            goto error_respond;
    }
    return;
error_respond:
    if (!w->mute) {
        if (flux_respond_error (nsm->ctx->h, w->request, errno, NULL) < 0)
            flux_log_error (nsm->ctx->h, "%s: flux_respond_error", __FUNCTION__);
    }
    w->finished = true;
finished:
    watcher_cleanup (nsm, w);
}

/* Respond to all ready watchers.
 * N.B. watcher_respond() may call zlistx_delete() on nsm->watchers.
 */
static void watcher_respond_ns (struct ns_monitor *nsm)
{
    struct watcher *w;

    w = zlistx_first (nsm->watchers);
    while (w) {
        /* Note: get next watcher before calling watcher_respond() since
         * `nsm` may be destroyed when next == NULL:
         */
        struct watcher *next = zlistx_next (nsm->watchers);
        watcher_respond (nsm, w);

        /* Note: No use-after-free possible since `nsm` may only be destroyed
         * if nsm->watchers is empty and and thus next == NULL
         */
        w = next;
    }
}

/* Cancel watcher 'w' if it matches:
 * - credentials and matchtag if cancel true
 * - credentials if cancel false
 * Suppress response if cancel is false (disconnect)
 */
static void watcher_cancel (struct ns_monitor *nsm,
                            struct watcher *w,
                            const flux_msg_t *msg,
                            bool cancel)
{
    if (cancel) {
        if (!flux_cancel_match (msg, w->request))
            return;
    }
    else {
        if (!flux_disconnect_match (msg, w->request))
            return;
    }
    w->canceled = true;
    w->mute = !cancel;
    watcher_respond (nsm, w);
}

/* Cancel all namespace watchers that match:
 * - credentials and matchtag if cancel true
 * - credentials if cancel false
 * Suppress response if cancel is false
 */
static void watcher_cancel_ns (struct ns_monitor *nsm,
                               const flux_msg_t *msg,
                               bool cancel)
{
    struct watcher *w;

    w = zlistx_first (nsm->watchers);
    while (w) {
        /* Note: get next watcher before calling watcher_respond() since
         * `nsm` may be destroyed when next == NULL:
         */
        struct watcher *next = zlistx_next (nsm->watchers);
        watcher_cancel (nsm, w, msg, cancel);

        /* Note: No use-after-free possible since `nsm` may only be destroyed
         * if nsm->watchers is empty and and thus next == NULL
         */
        w = next;
    }
}

/* Cancel all watchers that match:
 * - credentials and matchtag if cancel true
 * - credentials if cancel false
 * Suppress response if cancel is false
 */
static void watcher_cancel_all (struct watch_ctx *ctx,
                                const flux_msg_t *msg,
                                bool cancel)
{
    zlistx_t *l;
    char *name;
    struct ns_monitor *nsm;

    if ((l = zhashx_keys (ctx->namespaces))) {
        name = zlistx_first (l);
        while (name) {
            nsm = zhashx_lookup (ctx->namespaces, name);
            watcher_cancel_ns (nsm, msg, cancel);
            name = zlistx_next (l);
        }
        zlistx_destroy (&l);
    }
    else
        flux_log_error (ctx->h, "%s: zhashx_keys", __FUNCTION__);
}

/* kvs.namespace-removed-* event
 * A namespace has been removed.  All watchers should receive ENOTSUP.
 */
static void removed_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *ns;
    struct ns_monitor *nsm;

    if (flux_event_unpack (msg, NULL, "{s:s}", "namespace", &ns) < 0) {
        flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }
    if ((nsm = zhashx_lookup (ctx->namespaces, ns))) {
        nsm->fatal_errnum = ENOTSUP;
        watcher_respond_ns (nsm);
    }
}

/* kvs.namespace-created event
 * Update namespace with new namespace info.
 * N.B. commit->keys is empty in this case, in contrast setroot_cb().
 */
static void namespace_created_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct watch_ctx *ctx = arg;
    struct ns_monitor *nsm;
    const char *ns;
    int rootseq;
    const char *rootref;
    int owner;
    struct commit *commit;

    if (flux_event_unpack (msg,
                           NULL,
                           "{s:s s:i s:s s:i}",
                           "namespace", &ns,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "owner", &owner) < 0) {
        flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }
    if (!(nsm = zhashx_lookup (ctx->namespaces, ns))
        || nsm->commit)
        return;
    if (!(commit = commit_create (rootref, rootseq, NULL))) {
        flux_log_error (h, "%s: error creating commit", __FUNCTION__);
        nsm->errnum = errno;
        goto done;
    }
    nsm->commit = commit;
    if (nsm->owner == FLUX_USERID_UNKNOWN)
        nsm->owner = owner;
done:
    watcher_respond_ns (nsm);
}

/* kvs.setroot event
 * Update namespace with new commit info.
 * Subscribe/unsubscribe is tied to 'struct ns_monitor' create/destroy.
 */
static void setroot_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct watch_ctx *ctx = arg;
    struct ns_monitor *nsm;
    const char *ns;
    int rootseq;
    const char *rootref;
    int owner;
    json_t *keys;
    struct commit *commit;

    if (flux_event_unpack (msg,
                           NULL,
                           "{s:s s:i s:s s:i s:o}",
                           "namespace", &ns,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "owner", &owner,
                           "keys", &keys) < 0) {
        flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }
    if (!(nsm = zhashx_lookup (ctx->namespaces, ns))
            || (nsm->commit && rootseq <= nsm->commit->rootseq))
        return;
    if (!(commit = commit_create (rootref, rootseq, keys))) {
        flux_log_error (h, "%s: error creating commit", __FUNCTION__);
        nsm->errnum = errno;
        goto done;
    }
    commit_destroy (nsm->commit);
    nsm->commit = commit;
    if (nsm->owner == FLUX_USERID_UNKNOWN)
        nsm->owner = owner;
done:
    watcher_respond_ns (nsm);
}

/* kvs.getroot response for initial namespace creation
 * Discard result if namespace has already begun receiving setroot events.
 * N.B. commit->keys is empty in this case, in contrast setroot_cb().
 */
static void namespace_getroot_continuation (flux_future_t *f, void *arg)
{
    struct ns_monitor *nsm = arg;
    const char *rootref;
    int rootseq;
    uint32_t owner;
    struct commit *commit;

    if (nsm->commit) {
        flux_future_destroy (f);
        nsm->getrootf = NULL;
        return;
    }
    if (flux_kvs_getroot_get_sequence (f, &rootseq) < 0
            || flux_kvs_getroot_get_blobref (f, &rootref) < 0
            || flux_kvs_getroot_get_owner (f, &owner) < 0) {
        if (errno != ENOTSUP && errno != EPERM)
            flux_log_error (nsm->ctx->h, "%s: kvs_getroot", __FUNCTION__);
        nsm->errnum = errno;
        goto done;
    }
    if (!(commit = commit_create (rootref, rootseq, NULL))) {
        flux_log_error (nsm->ctx->h, "%s: commit_create", __FUNCTION__);
        nsm->errnum = errno;
        goto done;
    }
    nsm->commit = commit;
    nsm->owner = owner;
done:
    flux_future_destroy (f);
    nsm->getrootf = NULL;
    watcher_respond_ns (nsm);
}

/* event.subscribe response for initial namespace creation */
static void namespace_event_subscribe_continuation (flux_future_t *f, void *arg)
{
    struct ns_monitor *nsm = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        flux_log_error (nsm->ctx->h, "%s: event subscribe", __FUNCTION__);
        nsm->errnum = errno;
        goto cleanup;
    }
    flux_future_destroy (f);
    nsm->eventsubf = NULL;
    nsm->subscribed = true;
    return;

cleanup:
    flux_future_destroy (f);
    nsm->eventsubf = NULL;
    watcher_respond_ns (nsm);
}

/* Create 'nsm' if not already monitoring this namespace, and
 * send a getroot RPC to the kvs so first response need not wait
 * for the next commit to occur in the arbitrarily distant future.
 */
struct ns_monitor *namespace_monitor (struct watch_ctx *ctx,
                                      const char *ns)
{
    struct ns_monitor *nsm;

    if (!(nsm = zhashx_lookup (ctx->namespaces, ns))) {
        if (!(nsm = namespace_create (ctx, ns)))
            return NULL;
        (void)zhashx_insert (ctx->namespaces, ns, nsm);
        /* store futures in namespace, so namespace can be destroyed
         * appropriately to avoid matchtag leak */
        if (!(nsm->getrootf = flux_kvs_getroot (ctx->h, ns, 0)))
            goto cleanup;
        if (flux_future_then (nsm->getrootf,
                              -1.,
                              namespace_getroot_continuation,
                              nsm) < 0)
            goto cleanup;
        /* We are subscribing to the kvs.namespace-<NS> substring.
         *
         * This substring encompasses four events at the moment.
         *
         * kvs.namespace-<NS>-setroot
         * kvs.namespace-<NS>-error
         * kvs.namespace-<NS>-removed
         * kvs.namespace-<NS>-created
         *
         * This module only has callbacks for the "setroot", "removed", and
         * "created" events. "error" events are dropped.
         *
         * While dropped events are "bad" performance wise, "error" events
         * are presumably rare and it is a net win on performance to limit
         * the number of calls to subscribe to events.
         *
         * See issue #2779 for more information.
         */
        if (asprintf (&nsm->topic, "kvs.namespace-%s", ns) < 0)
            goto cleanup;
        if (!(nsm->eventsubf = flux_event_subscribe_ex (ctx->h,
                                                        nsm->topic,
                                                        0)))
            goto cleanup;
        if (flux_future_then (nsm->eventsubf,
                              -1.,
                              namespace_event_subscribe_continuation,
                              nsm) < 0)
            goto cleanup;
    }
    return nsm;
cleanup:
    zhashx_delete (ctx->namespaces, ns);
    return NULL;
}

static void lookup_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *ns;
    const char *key;
    int flags;
    struct ns_monitor *nsm;
    struct watcher *w;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:s s:i}",
                             "namespace", &ns,
                             "key", &key,
                             "flags", &flags) < 0)
        goto error;
    if ((flags & FLUX_KVS_WATCH) && !flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "KVS watch request rejected without streaming RPC flag";
        goto error;
    }
    if ((flags & FLUX_KVS_STREAM) && !flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "KVS stream request rejected without streaming RPC flag";
        goto error;
    }
    if ((flags & FLUX_KVS_WATCH) && (flags & FLUX_KVS_STREAM)) {
        errmsg = "Cannot KVS watch and stream at the same time";
        errno = EINVAL;
        goto error;
    }
    if (!(nsm = namespace_monitor (ctx, ns)))
        goto error;

    /* Thread a new watcher 'w' onto nsm->watchers.
     * If there is already a commit result available, send initial rpc,
     * otherwise initial rpc will be sent upon getroot RPC response
     * or setroot event.
     */
    if (!(w = watcher_create (msg, key, flags)))
        goto error;
    w->nsm = nsm;
    if (!(w->handle = zlistx_add_end (nsm->watchers, w))) {
        watcher_destroy (w);
        errno = ENOMEM;
        goto error;
    }
    if (nsm->commit)
        watcher_respond (nsm, w);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* kvs-watch.cancel request
 * The user called flux_kvs_lookup_cancel() which expects no response.
 * The watcher will receive an ENODATA response message.
 */
static void cancel_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct watch_ctx *ctx = arg;
    watcher_cancel_all (ctx, msg, true);
}

/* kvs-watch.disconnect request
 * This is sent automatically upon local connector disconnect.
 * The disconnect sender is used to find any watchers to be canceled.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct watch_ctx *ctx = arg;
    watcher_cancel_all (ctx, msg, false);
}

/* kvs-watch.stats-get request
 */
static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct watch_ctx *ctx = arg;
    struct ns_monitor *nsm;
    json_t *stats;
    int watchers = 0;

    if (!(stats = json_object()))
        goto nomem;
    nsm = zhashx_first (ctx->namespaces);
    while (nsm) {
        json_t *o = json_pack ("{s:i s:i s:s s:i}",
                               "owner", (int)nsm->owner,
                               "rootseq", nsm->commit ? nsm->commit->rootseq
                                                      : -1,
                               "rootref", nsm->commit ? nsm->commit->rootref
                                                      : "(null)",
                               "watchers", (int)zlistx_size (nsm->watchers));
        if (!o)
            goto nomem;
        if (json_object_set_new (stats, nsm->ns_name, o) < 0) {
            json_decref (o);
            goto nomem;
        }
        watchers += zlistx_size (nsm->watchers);
        nsm = zhashx_next (ctx->namespaces);
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:O}",
                           "watchers", watchers,
                           "namespace-count", (int)zhashx_size (ctx->namespaces),
                           "namespaces", stats) < 0)
        flux_log_error (h,
                        "%s: failed to respond to kvs-watch.stats-get",
                        __FUNCTION__);
    json_decref (stats);
    return;
nomem:
    if (flux_respond_error (h, msg, ENOMEM, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (stats);
}

/* see comments above in namespace_create() regarding event
 * subscriptions to kvs.namespace */
static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "kvs.namespace-*-removed",
      .cb           = removed_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "kvs.namespace-*-created",
      .cb           = namespace_created_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "kvs.namespace-*-setroot",
      .cb           = setroot_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.stats-get",
      .cb           = stats_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.lookup",
      .cb           = lookup_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.cancel",
      .cb           = cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void watch_ctx_destroy (struct watch_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhashx_destroy (&ctx->namespaces);
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
        errno = saved_errno;
    }
}

static struct watch_ctx *watch_ctx_create (flux_t *h)
{
    struct watch_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->namespaces = zhashx_new ()))
        goto error;
    zhashx_set_destructor (ctx->namespaces,
                           (zhashx_destructor_fn *)namespace_destroy);
    return ctx;
error:
    watch_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct watch_ctx *ctx;
    int rc = -1;

    if (!(ctx = watch_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    watch_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
