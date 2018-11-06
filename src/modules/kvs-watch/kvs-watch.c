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

#include "src/common/libkvs/treeobj.h"
#include "src/common/libutil/blobref.h"

/* State for one watcher.
 * If w->key is NULL, watcher tracks root updates for flux_kvs_getroot().
 * If w->key is non-NULL, watcher tracks key key updates for flux_kvs_lookup().
 */
struct watcher {
    flux_msg_t *request;        // getroot request message
    uint32_t rolemask;          // request cred
    uint32_t userid;            // request cred
    int rootseq;                // last root sequence number sent
    bool cancelled;             // true if watcher has been cancelled
    bool mute;                  // true if response should be suppressed

    char *key;                  // non-NULL if watching a key with lookup
    int flags;                  // kvs_lookup flags
    zlist_t *lookups;           // list of futures, in commit order

    struct namespace *ns;       // back pointer for removal
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
    int subscriptions;          // count of kvs.setroot-<name> subscriptions
};


static void watcher_destroy (struct watcher *w)
{
    if (w) {
        int saved_errno = errno;
        flux_msg_destroy (w->request);
        free (w->key);
        if (w->lookups) {
            flux_future_t *f;
            while ((f = zlist_pop (w->lookups)))
                flux_future_destroy (f);
            zlist_destroy (&w->lookups);
        }
        free (w);
        errno = saved_errno;
    }
}

static struct watcher *watcher_create (const flux_msg_t *msg, const char *key,
                                       int flags)
{
    struct watcher *w;

    if (!(w = calloc (1, sizeof (*w))))
        return NULL;
    if (!(w->request = flux_msg_copy (msg, true)))
        goto error;
    if (flux_msg_get_rolemask (msg, &w->rolemask) < 0
            || flux_msg_get_userid (msg, &w->userid) < 0)
        goto error;
    if (key && !(w->key = strdup (key)))
        goto error;
    if (!(w->lookups = zlist_new ()))
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

/* Verify that (userid, rolemask) credential is authorized to access 'ns'.
 * The instance owner or namespace owner are permitted (return 0).
 * All others are denied (return -1, errno == EPERM).
 */
static int check_authorization (struct namespace *ns,
                                uint32_t rolemask, uint32_t userid)
{
    if ((rolemask & FLUX_ROLE_OWNER))
        return 0;
    if (ns->owner != FLUX_USERID_UNKNOWN && userid == ns->owner)
        return 0;
    errno = EPERM;
    return -1;
}

/* Helper for watcher_respond - is key a member of array?
 */
static bool array_match (json_t *a, const char *key)
{
    size_t index;
    json_t *value;

    json_array_foreach (a, index, value) {
        const char *s = json_string_value (value);
        if (s && !strcmp (s, key))
            return true;
    }
    return false;
}

/* New value of key is available in future 'f' container.
 * Send response to watcher using raw payload from lookup response.
 * Return 0 on success, -1 on error (caller should destroy watcher).
 */
static int handle_lookup_response (flux_future_t *f, struct watcher *w)
{
    flux_t *h = flux_future_get_flux (f);
    const void *data;
    int len;

    if (flux_rpc_get_raw (f, &data, &len) < 0)
        goto error;
    if (!w->mute) {
        if (flux_respond_raw (h, w->request, data, len) < 0)
            flux_log_error (h, "%s: flux_respond_raw", __FUNCTION__);
    }
    return 0;
error:
    if (!w->mute) {
        if (flux_respond_error (h, w->request, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
    return -1;
}

/* One lookup has completed.
 * Pop ready futures off w->lookups and send responses, until
 * the list is empty, or a non-ready future is encountered.
 */
static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct watcher *w = arg;
    struct namespace *ns = w->ns;

    while ((f = zlist_first (w->lookups)) && flux_future_is_ready (f)) {
        f = zlist_pop (w->lookups);
        int rc = handle_lookup_response (f, w);
        flux_future_destroy (f);
        if (rc < 0)
            goto destroy_watcher;
    }
    return;
destroy_watcher:
    zlist_remove (ns->watchers, w);
    watcher_destroy (w);
    if (zlist_size (ns->watchers) == 0)
        zhash_delete (ns->ctx->namespaces, ns->name);
}

/* Like flux_kvs_lookupat() except:
 * - blobref param replaces treeobj
 * - namespace param (ignores namespace associated with flux_t handle)
 * - userid, rolemask params (see N.B. below)
 * Use flux_rpc_get() not flux_kvs_lookup_get() to access the response.
 */
static flux_future_t *lookupat (flux_t *h,
                                int flags,
                                const char *key,
                                const char *blobref,
                                const char *namespace,
                                uint32_t userid,
                                uint32_t rolemask)
{
    flux_msg_t *msg;
    json_t *o = NULL;
    flux_future_t *f;
    int saved_errno;

    if (!(msg = flux_request_encode ("kvs.lookup", NULL)))
        return NULL;
    if (!(o = treeobj_create_dirref (blobref)))
        goto error;
    if (flux_msg_pack (msg, "{s:s s:s s:i s:O}",
                       "key", key,
                       "namespace", namespace,
                       "flags", flags,
                       "rootdir", o) < 0)
        goto error;
    /* N.B. Since this module is authenticated to the shmem:// connector
     * with FLUX_ROLE_OWNER, we are allowed to switch the message credentials
     * in this request message, and not be overridden at the connector,
     * as would be the case if we were not sufficiently privileged.
     */
    if (flux_msg_set_userid (msg, userid) < 0)
        goto error;
    if (flux_msg_set_rolemask (msg, rolemask) < 0)
        goto error;
    if (!(f = flux_rpc_message (h, msg, FLUX_NODEID_ANY, 0)))
        goto error;
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
    assert (ns->commit != NULL);
    if (ns->commit->rootseq <= w->rootseq)
        return;
    if (check_authorization (ns, w->rolemask, w->userid) < 0) {
        flux_log (ns->ctx->h, LOG_DEBUG, "%s: auth failure", __FUNCTION__);
        goto error_respond;
    }
    /* flux_kvs_getroot (FLUX_KVS_WATCH)
     */
    if (w->key == NULL) {
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
    /* flux_kvs_lookup (FLUX_KVS_WATCH)
     *
     * Ordering note: KVS lookups can be returned out of order.  KVS lookup
     * futures are added to the w->lookups zlist in commit order here, and
     * in lookup_continuation(), fulfilled futures are popped off the head
     * of w->lookups until an unfulfilled future is encountered, so that
     * responses are always returned to the watcher in commit order.
     *
     * Security note: although the requestor has already been authenticated
     * to access the namespace by check_authorization() above, we make the
     * kvs.lookupat request with the requestor's creds, in case the key lookup
     * traverses to a new namespace.  Leave it up to the KVS module to ensure
     * the requestor is permitted to access *that* namespace.
     */
    else if (w->rootseq == -1 || array_match (ns->commit->keys, w->key)) {
        flux_future_t *f;
        if (!(f = lookupat (ns->ctx->h,
                            w->flags,
                            w->key,
                            ns->commit->rootref,
                            ns->name,
                            w->userid,
                            w->rolemask))) {
            flux_log_error (ns->ctx->h, "%s: lookupat", __FUNCTION__);
            goto error_respond;
        }
        if (zlist_append (w->lookups, f) < 0) {
            flux_future_destroy (f);
            errno = ENOMEM;
            goto error_respond;
        }
        if (flux_future_then (f, -1., lookup_continuation, w) < 0) {
            flux_future_destroy (f);
            goto error_respond;
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

/* Respond to all ready watchers.
 * N.B. watcher_respond() may call zlist_remove() on ns->watchers.
 * Since zlist_t is not deletion-safe for traversal, a temporary duplicate
 * must be created here.
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

/* Cancel watcher 'w' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 * If 'mute' is true, suppress response (e.g. for disconnect handling).
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
 * A namespace has been removed.  All watchers should receive ENOTSUP.
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
        ns->errnum = ENOTSUP;
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
        if (errno != ENOTSUP && errno != EPERM)
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

/* Create 'ns' if not already monitoring this namespace, and
 * send a getroot RPC to the kvs so first response need not wait
 * for the next commit to occur in the arbitrarily distant future.
 */
struct namespace *namespace_monitor (struct watch_ctx *ctx,
                                     const char *namespace)
{
    struct namespace *ns;
    flux_future_t *f;

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
            flux_future_destroy (f);
            goto error;
        }
    }
    return ns;
error:
    return NULL;
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

    if (flux_request_unpack (msg, NULL, "{s:s}", "namespace", &namespace) < 0)
        goto error;
    if (!(ns = namespace_monitor (ctx, namespace)))
        goto error;
    /* Thread a new watcher 'w' onto ns->watchers.
     * If there is already a commit result available, send first response now,
     * otherwise response will be sent upon getroot RPC response
     * or setroot event.
     */
    if (!(w = watcher_create (msg, NULL, 0)))
        goto error;
    w->ns = ns;
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

static void lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct watch_ctx *ctx = arg;
    const char *namespace;
    const char *key;
    int flags;
    struct namespace *ns;
    struct watcher *w;

    if (flux_request_unpack (msg, NULL, "{s:s s:s s:i}",
                             "namespace", &namespace,
                             "key", &key,
                             "flags", &flags) < 0)
        goto error;
    if (!(ns = namespace_monitor (ctx, namespace)))
        goto error;
    /* Thread a new watcher 'w' onto ns->watchers.
     * If there is already a commit result available, send first response now,
     * otherwise response will be sent upon getroot RPC response
     * or setroot event.
     */
    if (!(w = watcher_create (msg, key, flags)))
        goto error;
    w->ns = ns;
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
