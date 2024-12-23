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
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/errno_safe.h"

struct handler_stack {
    flux_msg_handler_t *mh;  // current message handler in stack
    zlistx_t *stack;         // stack of message handlers if >1
};

struct dispatch {
    flux_t *h;
    zlist_t *handlers;
    zlist_t *handlers_new;
    zhashx_t *handlers_rpc; // matchtag => response handler
    zhashx_t *handlers_method; // topic => request handler (non-glob only)
    flux_watcher_t *w;
    int running_count;
    int usecount;
    zlist_t *unmatched;
};

#define HANDLER_MAGIC 0x44433322
struct flux_msg_handler {
    int magic;
    struct dispatch *d;
    struct flux_match match;
    uint32_t rolemask;
    flux_msg_handler_f fn;
    void *arg;
    uint8_t running:1;
};

static void handle_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg);
static void free_msg_handler (flux_msg_handler_t *mh);

static size_t matchtag_hasher (const void *key);
static int matchtag_cmp (const void *key1, const void *key2);


static void handler_stack_destroy (struct handler_stack *hs)
{
    if (hs->stack)
        zlistx_destroy (&hs->stack);
    free (hs);
}

static void handler_stack_destructor (void **item)
{
    if (item && *item) {
        handler_stack_destroy (*item);
        *item = NULL;
    }
}

static struct handler_stack *handler_stack_create (void)
{
    struct handler_stack *hs = calloc (1, sizeof (*hs));
    return hs;
}

static int handler_stack_push (struct handler_stack *hs,
                               flux_msg_handler_t *mh)
{
    if (!hs->mh) {
        hs->mh = mh;
        return 0;
    }
    if (!hs->stack) {
        /*  Create stack, push current entry */
        if (!(hs->stack = zlistx_new ())
            || !zlistx_add_start (hs->stack, hs->mh))
            return -1;
    }
    if (!zlistx_add_start (hs->stack, mh))
        return -1;
    hs->mh = mh;
    return 0;
}

static int handler_stack_remove (struct handler_stack *hs,
                                 flux_msg_handler_t *mh)
{
    void *handle;
    if (!hs->stack) {
        hs->mh = NULL;
        return 0;
    }
    if (!(handle = zlistx_find (hs->stack, mh))
        || !zlistx_detach (hs->stack, handle)) {
        errno = ENOENT;
        return -1;
    }
    if (hs->mh == mh)
        hs->mh = zlistx_first (hs->stack);
    return 0;
}

static int handler_stack_empty (struct handler_stack *hs)
{
    return (hs->mh == NULL);
}

static zhashx_t *method_hash_create (void)
{
    zhashx_t *hash = zhashx_new ();
    if (!hash) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_destructor (hash, handler_stack_destructor);
    return hash;
}


static flux_msg_handler_t *method_hash_lookup (zhashx_t *hash,
                                               const char *topic)
{
    struct handler_stack *hs = zhashx_lookup (hash, topic);
    return hs ? hs->mh : NULL;
}

static int method_hash_add (zhashx_t *hash, flux_msg_handler_t *mh)
{
    struct handler_stack *hs = zhashx_lookup (hash, mh->match.topic_glob);
    if (!hs) {
        if (!(hs = handler_stack_create ()))
            return -1;
        if (zhashx_insert (hash, mh->match.topic_glob, hs) < 0) {
            errno = EEXIST;
            return -1;
        }
    }
    return handler_stack_push (hs, mh);
}

static void method_hash_remove (zhashx_t *hash, flux_msg_handler_t *mh)
{
    struct handler_stack *hs = zhashx_lookup (hash, mh->match.topic_glob);
    /*
     *  Remove requested handler from this stack. If stack is empty
     *   after remove, remove the hash entry entirely.
     */
    if (hs
        && handler_stack_remove (hs, mh) == 0
        && handler_stack_empty (hs)) {
        zhashx_delete (hash, mh->match.topic_glob);
    }
}

/* Return true if topic string 's' could match multiple request topics,
 * e.g. contains a glob character, or is NULL or "" which match anything.
 */
static bool isa_multmatch (const char *s)
{
    if (!s || strlen (s) == 0)
        return true;
    if (strchr (s, '*') || strchr (s, '?') || strchr (s, '['))
        return true;
    return false;
}

static void dispatch_requeue (struct dispatch *d)
{
    if (d->unmatched) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (d->unmatched))) {
            if (flux_requeue (d->h, msg, FLUX_RQ_HEAD) < 0)
                flux_log_error (d->h, "%s: flux_requeue", __FUNCTION__);
            flux_msg_destroy (msg);
        }
    }
}

static void dispatch_usecount_decr (struct dispatch *d)
{
    if (d && --d->usecount == 0) {
        int saved_errno = errno;
        if (flux_flags_get (d->h) & FLUX_O_CLONE) {
            dispatch_requeue (d);
            zlist_destroy (&d->unmatched);
        }
        if (d->handlers) {
            assert (zlist_size (d->handlers) == 0);
            zlist_destroy (&d->handlers);
        }
        if (d->handlers_new) {
            assert (zlist_size (d->handlers_new) == 0);
            zlist_destroy (&d->handlers_new);
        }
        flux_watcher_destroy (d->w);
        zhashx_destroy (&d->handlers_rpc);
        zhashx_destroy (&d->handlers_method);
        free (d);
        errno = saved_errno;
    }
}

static void dispatch_usecount_incr (struct dispatch *d)
{
    d->usecount++;
}


static void dispatch_destroy (void *arg)
{
    struct dispatch *d = arg;
    dispatch_usecount_decr (d);
}

static struct dispatch *dispatch_get (flux_t *h)
{
    struct dispatch *d = flux_aux_get (h, "flux::dispatch");
    if (!d) {
        flux_reactor_t *r = flux_get_reactor (h);
        if (!(d = malloc (sizeof (*d))))
            return NULL;
        memset (d, 0, sizeof (*d));
        d->usecount = 1;
        if (!(d->handlers = zlist_new ()))
            goto nomem;
        if (!(d->handlers_new = zlist_new ()))
            goto nomem;
        d->h = h;
        d->w = flux_handle_watcher_create (r, h, FLUX_POLLIN, handle_cb, d);
        if (!d->w)
            goto error;
        if (!(d->handlers_rpc = zhashx_new ()))
            goto nomem;
        zhashx_set_key_hasher (d->handlers_rpc, matchtag_hasher);
        zhashx_set_key_comparator (d->handlers_rpc, matchtag_cmp);
        zhashx_set_key_destructor (d->handlers_rpc, NULL);
        zhashx_set_key_duplicator (d->handlers_rpc, NULL);

        if (!(d->handlers_method = method_hash_create ()))
            goto nomem;
        if (flux_aux_set (h, "flux::dispatch", d, dispatch_destroy) < 0)
            goto error;
    }
    return d;
nomem:
    errno = ENOMEM;
error:
    dispatch_destroy (d);
    return NULL;
}

/* zhahsx_comparator_fn to compare matchtags
 */
static int matchtag_cmp (const void *key1, const void *key2)
{
    uint32_t m1 = *(uint32_t *)key1;
    uint32_t m2 = *(uint32_t *)key2;

    if (m1 < m2)
        return -1;
    if (m1 > m2)
        return 1;
    return 0;
}

/* zhashx_hash_fn to map response matchtags to handlers
 */
static size_t matchtag_hasher (const void *key)
{
    uint32_t matchtag = *(uint32_t *)key;

    return matchtag;
}

static int copy_match (struct flux_match *dst,
                       const struct flux_match src)
{

    flux_match_free (*dst);
    *dst = src;
    if (src.topic_glob) {
        if (!(dst->topic_glob = strdup (src.topic_glob)))
            return -1;
    }
    return 0;
}

static void call_handler (flux_msg_handler_t *mh, const flux_msg_t *msg)
{
    uint32_t rolemask;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return;
    if (!(rolemask & mh->rolemask)) {
        if (flux_msg_cmp (msg, FLUX_MATCH_REQUEST)
            && !flux_msg_has_flag (msg, FLUX_MSGFLAG_NORESPONSE)) {
            const char *errmsg;
            if (mh->rolemask == 0 || mh->rolemask == FLUX_ROLE_OWNER)
                errmsg = "Request requires owner credentials";
            else
                errmsg = "Request rejected due to insufficient privilege";
            (void)flux_respond_error (mh->d->h, msg, EPERM, errmsg);
        }
        return;
    }
    mh->fn (mh->d->h, mh, msg, mh->arg);
}

/* Messages are matched in the following order:
 * 1) RPC responses - lookup in handlers_rpc hash by matchtag.
 * 2) RPC requests - lookup in handlers_method hash by topic string
 * 3) Requests and responses not matched above - sent to first match in
 *    list of handlers, where most recently registered handlers match first.
 * 4) Events - sent to all matches in list of handlers
 */
static bool dispatch_message (struct dispatch *d,
                              const flux_msg_t *msg,
                              int type)
{
    flux_msg_handler_t *mh;
    bool match = false;

    /* rpc response w/matchtag */
    if (type == FLUX_MSGTYPE_RESPONSE) {
        uint32_t matchtag;
        if (flux_msg_route_count (msg) == 0
            && flux_msg_get_matchtag (msg, &matchtag) == 0
            && matchtag != FLUX_MATCHTAG_NONE
            && (mh = zhashx_lookup (d->handlers_rpc, &matchtag))
            && mh->running
            && flux_msg_cmp (msg, mh->match)) {
            call_handler (mh, msg);
            match = true;
        }
    }
    /* rpc request */
    else if (type == FLUX_MSGTYPE_REQUEST) {
        const char *topic;
        if (flux_msg_get_topic (msg, &topic) == 0
            && (mh = method_hash_lookup (d->handlers_method, topic))
            && mh->running) {
            call_handler (mh, msg);
            match = true;
        }
    }
    /* other */
    if (!match) {
        FOREACH_ZLIST (d->handlers, mh) {
            if (!mh->running)
                continue;
            if (flux_msg_cmp (msg, mh->match)) {
                call_handler (mh, msg);
                if (type != FLUX_MSGTYPE_EVENT) {
                    match = true;
                    break;
                }
            }
        }
    }
    return match;
}

/* A matchtag may have been leaked if an RPC future is destroyed with
 * responses outstanding.  If the last response is finally received,
 * return it to the pool.
 */
static void handle_late_response (struct dispatch *d, const flux_msg_t *msg)
{
    uint32_t matchtag;
    int errnum;

    if (flux_msg_get_matchtag (msg, &matchtag) < 0)
        return;
    if (flux_msg_route_count (msg) != 0)
        return; // foreign matchtag domain (or error getting count)
    if (matchtag == FLUX_MATCHTAG_NONE)
        return; // no matchtag was allocated
    if (flux_msg_is_streaming (msg)) {
        if (flux_msg_get_errnum (msg, &errnum) < 0 || errnum == 0)
            return; // streaming RPC is only terminated with an error response
    }
    flux_matchtag_free (d->h, matchtag);
    if (flux_flags_get (d->h) & FLUX_O_MATCHDEBUG)
        fprintf (stderr, "MATCHDEBUG: reclaimed matchtag=%d\n", matchtag);
}

static int transfer_items_zlist (zlist_t *from, zlist_t *to)
{
    void *item;
    int rc = -1;

    while ((item = zlist_pop (from))) {
        if (zlist_push (to, item) < 0) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    return rc;
}

static void handle_cb (flux_reactor_t *r,
                       flux_watcher_t *hw,
                       int revents,
                       void *arg)
{
    struct dispatch *d = arg;
    flux_msg_t *msg = NULL;
    int rc = -1;
    int type;
    bool match;
    const char *topic;

    if (revents & FLUX_POLLERR)
        goto done;
    if (!(msg = flux_recv (d->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN && errno == EWOULDBLOCK)
            rc = 0; /* ignore spurious wakeup */
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        rc = 0; /* ignore mangled message */
        goto done;
    }
    if (flux_msg_get_topic (msg, &topic) < 0)
        topic = "unknown"; /* used for logging */

    /* Add any new handlers here, making handler creation
     * safe to call during handlers list traversal below.
     */
    if (transfer_items_zlist (d->handlers_new, d->handlers) < 0)
        goto done;

    match = dispatch_message (d, msg, type);

    /* Message was not "consumed".
     * If in a cloned handle, queue message for later.
     * Otherwise, respond with ENOSYS if it was a request,
     * or log it if FLUX_O_TRACE.
     */
    if (!match) {
        if ((flux_flags_get (d->h) & FLUX_O_CLONE)) {
            if (!d->unmatched && !(d->unmatched = zlist_new ())) {
                errno = ENOMEM;
                goto done;
            }
            if (zlist_push (d->unmatched, msg) < 0) {
                errno = ENOMEM;
                goto done;
            }
            msg = NULL; // prevent destruction below
        }
        else {
            switch (type) {
                case FLUX_MSGTYPE_REQUEST: {
                    char errmsg[256];
                    (void)snprintf (errmsg,
                                    sizeof (errmsg),
                                    "Unknown service method '%s'",
                                    topic);
                    if (flux_respond_error (d->h, msg, ENOSYS, errmsg))
                        goto done;
                    break;
                }
                case FLUX_MSGTYPE_EVENT:
                    break;
                case FLUX_MSGTYPE_RESPONSE:
                    handle_late_response (d, msg);
                    break;
                default:
                    if (flux_flags_get (d->h) & FLUX_O_TRACE) {
                        fprintf (stderr,
                                 "nomatch: %s '%s'\n",
                                 flux_msg_typestr (type),
                                 topic);
                    }
                    break;
            }
        }
    }
    rc = 0;
done:
    if (rc < 0)
        flux_reactor_stop_error (r);
    flux_msg_destroy (msg);
}

void flux_msg_handler_start (flux_msg_handler_t *mh)
{
    struct dispatch *d = mh->d;

    assert (mh->magic == HANDLER_MAGIC);
    if (mh->running == 0) {
        mh->running = 1;
        d->running_count++;
        flux_watcher_start (d->w);
    }
}

void flux_msg_handler_stop (flux_msg_handler_t *mh)
{
    if (!mh)
        return;
    assert (mh->magic == HANDLER_MAGIC);
    if (mh->running == 1) {
        struct dispatch *d = mh->d;
        mh->running = 0;
        d->running_count--;
        if (d->running_count == 0)
            flux_watcher_stop (d->w);
    }
}

void flux_msg_handler_allow_rolemask (flux_msg_handler_t *mh, uint32_t rolemask)
{
    if (mh) {
        mh->rolemask |= rolemask;
    }
}

void flux_msg_handler_deny_rolemask (flux_msg_handler_t *mh, uint32_t rolemask)
{
    if (mh) {
        mh->rolemask &= ~rolemask;
        mh->rolemask |= FLUX_ROLE_OWNER;
    }
}

static void free_msg_handler (flux_msg_handler_t *mh)
{
    if (mh) {
        int saved_errno = errno;
        assert (mh->magic == HANDLER_MAGIC);
        flux_match_free (mh->match);
        mh->magic = ~HANDLER_MAGIC;
        free (mh);
        errno = saved_errno;
    }
}

void flux_msg_handler_destroy (flux_msg_handler_t *mh)
{
    if (mh) {
        int saved_errno = errno;
        assert (mh->magic == HANDLER_MAGIC);
        if (mh->match.typemask == FLUX_MSGTYPE_RESPONSE
            && mh->match.matchtag != FLUX_MATCHTAG_NONE) {
            zhashx_delete (mh->d->handlers_rpc, &mh->match.matchtag);
        }
        else if (mh->match.typemask == FLUX_MSGTYPE_REQUEST
                 && !isa_multmatch (mh->match.topic_glob)) {
            method_hash_remove (mh->d->handlers_method, mh);
        }
        else {
            zlist_remove (mh->d->handlers_new, mh);
            zlist_remove (mh->d->handlers, mh);
        }
        flux_msg_handler_stop (mh);
        dispatch_usecount_decr (mh->d);
        free_msg_handler (mh);
        errno = saved_errno;
    }
}

flux_msg_handler_t *flux_msg_handler_create (flux_t *h,
                                             const struct flux_match match,
                                             flux_msg_handler_f cb,
                                             void *arg)
{
    struct dispatch *d;
    flux_msg_handler_t *mh;

    if (!h || !cb) {
        errno = EINVAL;
        return NULL;
    }
    if (!(d = dispatch_get (h)))
        return NULL;
    if (!(mh = calloc (1, sizeof (*mh))))
        return NULL;
    mh->magic = HANDLER_MAGIC;
    if (copy_match (&mh->match, match) < 0)
        goto error;
    mh->rolemask = FLUX_ROLE_OWNER;
    mh->fn = cb;
    mh->arg = arg;
    mh->d = d;
    /* Response (valid matchtag):
     * Fail if entry in the handlers_rpc hash exists, since that probably
     * indicates a matchtag reuse problem!
     */
    if (mh->match.typemask == FLUX_MSGTYPE_RESPONSE
        && mh->match.matchtag != FLUX_MATCHTAG_NONE) {
        if (zhashx_insert (d->handlers_rpc, &mh->match.matchtag, mh) < 0) {
            errno = EEXIST;
            goto error;
        }
    }
    /* Request (non-glob):
     * Push entry onto top of the handlers_method stack, if any.
     * This allows builtin module methods to be overridden.
     */
    else if (mh->match.typemask == FLUX_MSGTYPE_REQUEST
             && !isa_multmatch (mh->match.topic_glob)) {
        if (method_hash_add (d->handlers_method, mh) < 0)
            goto error;
    }
    /* Request (glob), response (FLUX_MATCHTAG_NONE), events:
     * Message handler is pushed to the front of the handlers list,
     * and matches before older ones for requests and responses.
     * (Requests and responses in hashes above match first though).
     * Event messages are broadcast to all matching handlers.
     */
    else {
        /* N.B. append(handlers_new); later, pop(handlers_new), push(handlers).
         * Net effect: push(handlers).
         */
        if (zlist_append (d->handlers_new, mh) < 0) {
            errno = ENOMEM;
            goto error;
        }
    }
    dispatch_usecount_incr (d);
    return mh;
error:
    free_msg_handler (mh);
    return NULL;
}

static bool at_end (struct flux_msg_handler_spec spec)
{
    struct flux_msg_handler_spec end = FLUX_MSGHANDLER_TABLE_END;

    return (spec.typemask == end.typemask
            && spec.topic_glob == end.topic_glob
            && spec.cb == end.cb
            && spec.rolemask == end.rolemask);
}

int flux_msg_handler_addvec_ex (flux_t *h,
                                const char *service_name,
                                const struct flux_msg_handler_spec tab[],
                                void *arg,
                                flux_msg_handler_t **hp[])
{
    int i;
    struct flux_match match = FLUX_MATCH_ANY;
    flux_msg_handler_t **handlers = NULL;
    int count = 0;

    if (!h || !tab || !hp) {
        errno = EINVAL;
        return -1;
    }
    while (!at_end (tab[count]))
        count++;
    if (!(handlers = calloc (count + 1, sizeof (flux_msg_handler_t *))))
        return -1;
    for (i = 0; i < count; i++) {
        char *topic = NULL;
        match.typemask = tab[i].typemask;
        if (service_name) {
            if (asprintf (&topic, "%s.%s", service_name, tab[i].topic_glob) < 0)
                goto error;
        }
        match.topic_glob = topic ? topic : (char *)tab[i].topic_glob;
        handlers[i] = flux_msg_handler_create (h, match, tab[i].cb, arg);
        ERRNO_SAFE_WRAP (free, topic);
        if (!handlers[i])
            goto error;
        flux_msg_handler_allow_rolemask (handlers[i], tab[i].rolemask);
        flux_msg_handler_start (handlers[i]);
    }
    *hp = handlers;
    return 0;
error:
    flux_msg_handler_delvec (handlers);
    return -1;
}

int flux_msg_handler_addvec (flux_t *h,
                             const struct flux_msg_handler_spec tab[],
                             void *arg,
                             flux_msg_handler_t **hp[])
{
    return flux_msg_handler_addvec_ex (h, NULL, tab, arg, hp);
}

void flux_msg_handler_delvec (flux_msg_handler_t *handlers[])
{
    if (handlers) {
        int saved_errno = errno;
        for (int i = 0; handlers[i] != NULL; i++) {
            flux_msg_handler_destroy (handlers[i]);
            handlers[i] = NULL;
        }
        free (handlers);
        errno = saved_errno;
    }
}

int flux_dispatch_requeue (flux_t *h)
{
    struct dispatch *d;

    if (!h || !(flux_flags_get (h) & FLUX_O_CLONE)) {
        errno = EINVAL;
        return -1;
    }
    if (!(d = dispatch_get (h)))
        return -1;
    dispatch_requeue (d);
    return 0;
}

flux_watcher_t *flux_get_handle_watcher (flux_t *h)
{
    struct dispatch *d = dispatch_get (h);
    return d ? d->w : NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
