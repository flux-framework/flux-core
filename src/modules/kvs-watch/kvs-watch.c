/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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

/* kvs-watcher - track KVS changes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/blobref.h"

/* State for one getroot watcher.
 */
struct watcher {
    flux_msg_t *request;        // getroot request message
    int rootseq;                // last root sequence number sent
    bool auth;                  // true if authorized to watch namespace
    bool cancelled;             // true if watcher has been cancelled
    bool mute;                  // true if response should be suppressed
};

/* Current KVS root.
 */
struct commit {
    char *rootref;              // current root blobref
    int rootseq;                // current root sequence number
    json_t *keys;               // keys changed by commit
};                              //  (empty if data originates from getroot RPC)

/* State for monitoring a KVS namespace.
 */
struct namespace {
    char *name;                 // namespace name, hash key for ctx->namespaces
    uint32_t owner;             // namespace owner (userid)
    struct commit *commit;      // current commit data
    int errnum;                 // if non-zero, error pending for all watchers
    struct watch_ctx *ctx;      // back-pointer to watch_ctx
    zlist_t *watchers;          // list of watchers of this namespace
    char *topic;                // topic string for setroot subscription
    bool subscribed;            // setroot (ns->topic) subscription active
};

/* Module state.
 */
struct watch_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zhash_t *namespaces;        // hash of monitored namespaces
    int subscriptions;          // count of setroot subscrpitions
};


static void watcher_destroy (struct watcher *w)
{
    if (w) {
        int saved_errno = errno;
        flux_msg_destroy (w->request);
        free (w);
        errno = saved_errno;
    }
}

static struct watcher *watcher_create (const flux_msg_t *msg)
{
    struct watcher *w;

    if (!(w = calloc (1, sizeof (*w))))
        return NULL;
    if (!(w->request = flux_msg_copy (msg, true))) {
        watcher_destroy (w);
        return NULL;
    }
    w->rootseq = -1;
    return w;
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

static struct commit *commit_create (const char *rootref, int rootseq,
                                     json_t *keys)
{
    struct commit *commit = calloc (1, sizeof (*commit));
    if (!commit)
        return NULL;
    if (!(commit->rootref = strdup (rootref))) {
        commit_destroy (commit);
        return NULL;
    }
    commit->keys = json_incref (keys);
    commit->rootseq = rootseq;
    return commit;
}

static void namespace_destroy (struct namespace *ns)
{
    if (ns) {
        int saved_errno = errno;
        commit_destroy (ns->commit);
        if (ns->watchers) {
            struct watcher *w;
            while ((w = zlist_pop (ns->watchers)))
                watcher_destroy (w);
            zlist_destroy (&ns->watchers);
        }
        if (ns->subscribed) {
            (void)flux_event_unsubscribe (ns->ctx->h, ns->topic);
            if (--(ns->ctx->subscriptions) == 0)
                (void)flux_event_unsubscribe (ns->ctx->h,
                                              "kvs.namespace-remove");
        }
        free (ns->topic);
        free (ns->name);
        free (ns);
        errno = saved_errno;
    }
}

static struct namespace *namespace_create (struct watch_ctx *ctx,
                                           const char *namespace)
{
    struct namespace *ns = calloc (1, sizeof (*ns));
    if (!ns)
        return NULL;
    if (!(ns->watchers = zlist_new ()))
        goto error;
    if (!(ns->name = strdup (namespace)))
        goto error;
    if (asprintf (&ns->topic, "kvs.setroot-%s", namespace) < 0)
        goto error;
    ns->owner = FLUX_USERID_UNKNOWN;
    ns->ctx = ctx;
    if (flux_event_subscribe (ctx->h, ns->topic) < 0)
        goto error;
    ns->subscribed = true;
    if (ctx->subscriptions++ == 0) {
        if (flux_event_subscribe (ctx->h, "kvs.namespace-remove") < 0)
            goto error;
    }
    return ns;
error:
    namespace_destroy (ns);
    return NULL;
}

/* Verify that a getroot request 'msg' is authorized to access 'ns'.
 * The instance owner or namespace owner are permitted (return 0).
 * All others are denied (return -1, errno == EPERM).
 */
static int authenticate (struct namespace *ns, const flux_msg_t *msg)
{
    uint32_t rolemask;
    uint32_t userid;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return -1;
    if ((rolemask & FLUX_ROLE_OWNER))
        return 0;
    if (flux_msg_get_userid (msg, &userid) < 0)
        return -1;
    if (ns->owner != FLUX_USERID_UNKNOWN && userid == ns->owner)
        return 0;
    errno = EPERM;
    return -1;
}

/* Respond to watcher request, if appropriate.
 * De-list and destroy watcher from namespace on error.
 * De-hash and destroy namespace if watchers list becomes empty.
 */
static void watcher_respond (struct namespace *ns, struct watcher *w)
{
    if (w->cancelled) {
        errno = ENODATA;
        goto error_respond;
    }
    if (ns->errnum != 0) {
        errno = ns->errnum;
        goto error_respond;
    }
    if (ns->commit && ns->commit->rootseq > w->rootseq) {
        if (!w->auth && authenticate (ns, w->request) < 0) {
            flux_log (ns->ctx->h, LOG_DEBUG, "%s: auth failure", __FUNCTION__);
            goto error_respond;
        }
        w->auth = true;
        if (!w->mute) {
            if (flux_respond_pack (ns->ctx->h, w->request, "{s:s s:i s:i s:i}",
                                   "rootref", ns->commit->rootref,
                                   "rootseq", ns->commit->rootseq,
                                   "owner", ns->owner,
                                   "flags", 0) < 0) {
                flux_log_error (ns->ctx->h, "%s: flux_respond", __FUNCTION__);
                goto error;
            }
        }
        w->rootseq = ns->commit->rootseq;
    }
    return;
error_respond:
    if (!w->mute) {
        if (flux_respond_error (ns->ctx->h, w->request, errno, NULL) < 0)
            flux_log_error (ns->ctx->h, "%s: flux_respond_error", __FUNCTION__);
    }
error:
    zlist_remove (ns->watchers, w);
    watcher_destroy (w);
    if (zlist_size (ns->watchers) == 0)
        zhash_delete (ns->ctx->namespaces, ns->name);
}

/* Respond to all watchers for a namespace, as appropriate.
 * See watcher_respond().
 */
static void watcher_respond_ns (struct namespace *ns)
{
    zlist_t *l;
    struct watcher *w;

    if ((l = zlist_dup (ns->watchers))) {
        w = zlist_first (l);
        while (w) {
            watcher_respond (ns, w);
            w = zlist_next (l);
        }
        zlist_destroy (&l);
    }
    else
        flux_log_error (ns->ctx->h, "%s: zlist_dup", __FUNCTION__);
}

/* Cancel this watcher 'w' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 * If 'mute' is true, suppress response.
 */
static void watcher_cancel (struct namespace *ns, struct watcher *w,
                            const char *sender, uint32_t matchtag,
                            bool mute)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
            && (flux_msg_get_matchtag (w->request, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (w->request, &s) < 0)
        return;
    if (!strcmp (sender, s)) {
        w->cancelled = true;
        w->mute = mute;
        watcher_respond (ns, w);
    }
    free (s);
}

/* Cancel all namespace watchers that match (sender, matchtag).
 * If 'mute' is true, suppress response.
 */
static void watcher_cancel_ns (struct namespace *ns,
                               const char *sender, uint32_t matchtag,
                               bool mute)
{
    zlist_t *l;
    struct watcher *w;

    if ((l = zlist_dup (ns->watchers))) {
        w = zlist_first (l);
        while (w) {
            watcher_cancel (ns, w, sender, matchtag, mute);
            w = zlist_next (l);
        }
        zlist_destroy (&l);
    }
    else
        flux_log_error (ns->ctx->h, "%s: zlist_dup", __FUNCTION__);
}

/* Cancel all watchers that match (sender, matchtag).
 * If 'mute' is true, suppress response.
 */
static void watcher_cancel_all (struct watch_ctx *ctx,
                                const char *sender, uint32_t matchtag,
                                bool mute)
{
    zlist_t *l;
    char *name;
    struct namespace *ns;

    if ((l = zhash_keys (ctx->namespaces))) {
        name = zlist_first (l);
        while (name) {
            ns = zhash_lookup (ctx->namespaces, name);
            watcher_cancel_ns (ns, sender, matchtag, mute);
            name = zlist_next (l);
        }
        zlist_destroy (&l);
    }
    else
        flux_log_error (ctx->h, "%s: zhash_keys", __FUNCTION__);
}

/* kvs.namespace-remove event
 * A namespace has been removed.  All watchers should receive ENODATA.
 */
static void remove_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *namespace;
    struct namespace *ns;

    if (flux_event_unpack (msg, NULL, "{s:s}", "namespace", &namespace) < 0) {
        flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }
    if ((ns = zhash_lookup (ctx->namespaces, namespace))) {
        ns->errnum = ENODATA;
        watcher_respond_ns (ns);
    }
}

/* kvs.setroot event
 * Update namespace with new commit info.
 * Subscribe/unsubscribe is tied to 'struct namespace' create/destroy.
 */
static void setroot_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    struct namespace *ns;
    const char *namespace;
    int rootseq;
    const char *rootref;
    int owner;
    json_t *keys;
    struct commit *commit;

    if (flux_event_unpack (msg, NULL, "{s:s s:i s:s s:i s:o}",
                           "namespace", &namespace,
                           "rootseq", &rootseq,
                           "rootref", &rootref,
                           "owner", &owner,
                           "keys", &keys) < 0) {
        flux_log_error (h, "%s: flux_event_decode", __FUNCTION__);
        return;
    }
    if (!(ns = zhash_lookup (ctx->namespaces, namespace))
            || (ns->commit && rootseq <= ns->commit->rootseq))
        return;
    if (!(commit = commit_create (rootref, rootseq, keys))) {
        flux_log_error (h, "%s: error creating commit", __FUNCTION__);
        ns->errnum = errno;
        goto done;;
    }
    commit_destroy (ns->commit);
    ns->commit = commit;
    if (ns->owner == FLUX_USERID_UNKNOWN)
        ns->owner = owner;
done:
    watcher_respond_ns (ns);
}

/* kvs.getroot response
 * Discard result if namespace has already begun receiving setroot events.
 * N.B. commit->keys is empty in this case, in contrast setroot_cb().
 */
static void getroot_continuation (flux_future_t *f, void *arg)
{
    struct namespace *ns = arg;
    const char *rootref;
    int rootseq;
    uint32_t owner;
    struct commit *commit;

    if (ns->commit) {
        flux_future_destroy (f);
        return;
    }
    if (flux_kvs_getroot_get_sequence (f, &rootseq) < 0
            || flux_kvs_getroot_get_blobref (f, &rootref) < 0
            || flux_kvs_getroot_get_owner (f, &owner) < 0) {
        flux_log_error (ns->ctx->h, "%s: kvs_getroot", __FUNCTION__);
        ns->errnum = errno;
        goto done;
    }
    if (!(commit = commit_create (rootref, rootseq, NULL))) {
        flux_log_error (ns->ctx->h, "%s: commit_create", __FUNCTION__);
        ns->errnum = errno;
        goto done;
    }
    ns->commit = commit;
    ns->owner = owner;
done:
    watcher_respond_ns (ns);
    flux_future_destroy (f);
}

/* kvs-watch.getroot request
 */
static void getroot_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *namespace;
    struct watcher *w;
    struct namespace *ns;
    flux_future_t *f;

    if (flux_request_unpack (msg, NULL, "{s:s}", "namespace", &namespace) < 0)
        goto error;
    /* Create 'ns' if not already monitoring this namespace, and
     * send a getroot RPC to the kvs so first response need not wait
     * for the next commit to occur in the arbitrarily distant future.
     */
    if (!(ns = zhash_lookup (ctx->namespaces, namespace))) {
        if (!(ns = namespace_create (ctx, namespace)))
            goto error;
        if (zhash_insert (ctx->namespaces, namespace, ns) < 0) {
            namespace_destroy (ns);
            goto error;
        }
        zhash_freefn (ctx->namespaces, namespace,
                      (zhash_free_fn *)namespace_destroy);
        if (!(f = flux_kvs_getroot (ctx->h, namespace, 0))) {
            zhash_delete (ctx->namespaces, namespace);
            goto error;
        }
        if (flux_future_then (f, -1., getroot_continuation, ns) < 0) {
            zhash_delete (ctx->namespaces, namespace);
            goto error;
        }
    }
    /* Thread a new watcher 'w' onto ns->watchers.
     * If there is already a commit result available, send first response now,
     * otherwise response will be sent upon getroot RPC response
     * or setroot event.
     */
    if (!(w = watcher_create (msg)))
        goto error;
    if (zlist_append (ns->watchers, w) < 0) {
        watcher_destroy (w);
        errno = ENOMEM;
        goto error;
    }
    if (ns->commit)
        watcher_respond (ns, w);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* kvs-watch.cancel request
 * The user called flux_kvs_getroot_cancel() which expects no response.
 * The enclosed matchtag and the cancel sender are used to find the
 * watcher that is to be cancelled.  The watcher will receive an ENODATA
 * response message.
 */
static void cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    uint32_t matchtag;
    char *sender;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    watcher_cancel_all (ctx, sender, matchtag, false);
    free (sender);
}

/* kvs-watch.disconnect request
 * This is sent automatically upon local connector disconnect.
 * The disconnect sender is used to find any watchers to be cancelled.
 */
static void disconnect_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    char *sender;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: flux_request_decode", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    watcher_cancel_all (ctx, sender, FLUX_MATCHTAG_NONE, true);
    free (sender);
}

/* kvs-watch.stats.get request
 */
static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    struct namespace *ns;
    json_t *stats;
    int watchers = 0;

    if (!(stats = json_object()))
        goto nomem;
    ns = zhash_first (ctx->namespaces);
    while (ns) {
        json_t *o = json_pack ("{s:i s:i s:s s:i}",
                               "owner", (int)ns->owner,
                               "rootseq", ns->commit ? ns->commit->rootseq
                                                     : -1,
                               "rootref", ns->commit ? ns->commit->rootref
                                                     : "(null)",
                               "watchers", (int)zlist_size (ns->watchers));
        if (!o)
            goto nomem;
        if (json_object_set_new (stats, ns->name, o) < 0) {
            json_decref (o);
            goto nomem;
        }
        watchers += zlist_size (ns->watchers);
        ns = zhash_next (ctx->namespaces);
    }
    if (flux_respond_pack (h, msg, "{s:i s:i s:O}",
                           "watchers", watchers,
                           "namespace-count", (int)zhash_size (ctx->namespaces),
                           "namespaces", stats) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (stats);
    return;
nomem:
    if (flux_respond_error (h, msg, ENOMEM, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (stats);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "kvs.namespace-remove",
      .cb           = remove_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "kvs.setroot-*",
      .cb           = setroot_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "kvs-watch.getroot",
      .cb           = getroot_cb,
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
        zhash_destroy (&ctx->namespaces);
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
    if (!(ctx->namespaces = zhash_new ()))
        goto error;
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

MOD_NAME ("kvs-watch");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
