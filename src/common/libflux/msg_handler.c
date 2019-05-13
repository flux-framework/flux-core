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
#include <czmq.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "message.h"
#include "reactor.h"
#include "msg_handler.h"
#include "response.h"
#include "flog.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"

struct dispatch {
    flux_t *h;
    zlist_t *handlers;
    zlist_t *handlers_new;
    zhashx_t *handlers_rpc; // hashed by matchtag
    flux_watcher_t *w;
    int running_count;
    int usecount;
    zlist_t *unmatched;
#if HAVE_CALIPER
    cali_id_t prof_msg_type;
    cali_id_t prof_msg_topic;
    cali_id_t prof_msg_dispatch;
#endif
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

static void handle_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg);
static void free_msg_handler (flux_msg_handler_t *mh);

static size_t matchtag_hasher (const void *key);
static int matchtag_cmp (const void *key1, const void *key2);

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
#if HAVE_CALIPER
        d->prof_msg_type = cali_create_attribute ("flux.message.type",
                                                  CALI_TYPE_STRING,
                                                  CALI_ATTR_SKIP_EVENTS);
        d->prof_msg_topic = cali_create_attribute ("flux.message.topic",
                                                   CALI_TYPE_STRING,
                                                   CALI_ATTR_SKIP_EVENTS);
        d->prof_msg_dispatch = cali_create_attribute ("flux.message.dispatch",
                                                      CALI_TYPE_BOOL,
                                                      CALI_ATTR_DEFAULT);
#endif
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

    if ((m1 & FLUX_MATCHTAG_GROUP_MASK))
        m1 &= FLUX_MATCHTAG_GROUP_MASK;
    if ((m2 & FLUX_MATCHTAG_GROUP_MASK))
        m2 &= FLUX_MATCHTAG_GROUP_MASK;
    if (m1 < m2)
        return -1;
    if (m1 > m2)
        return 1;
    return 0;
}

/* zhashx_hash_fn to map response matchtags to handlers
 * 12 high order bits are for group matchtags (mrpc).
 * If group bits are set, we must ignore the low order bits when
 * mapping to the handler.
 */
static size_t matchtag_hasher (const void *key)
{
    uint32_t matchtag = *(uint32_t *)key;

    if ((matchtag & FLUX_MATCHTAG_GROUP_MASK))
        matchtag &= FLUX_MATCHTAG_GROUP_MASK;

    return matchtag;
}

static int copy_match (struct flux_match *dst,
                       const struct flux_match src)
{
    if (dst->topic_glob)
        free (dst->topic_glob);
    *dst = src;
    if (src.topic_glob) {
        if (!(dst->topic_glob = strdup (src.topic_glob)))
            return -1;
    }
    return 0;
}

static void call_handler (flux_msg_handler_t *mh, const flux_msg_t *msg)
{
    uint32_t rolemask, matchtag;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return;
    if (!(rolemask & mh->rolemask)) {
        if (flux_msg_cmp (msg, FLUX_MATCH_REQUEST)
                        && flux_msg_get_matchtag (msg, &matchtag) == 0
                        && matchtag != FLUX_MATCHTAG_NONE) {
            (void)flux_respond_error (mh->d->h, msg, EPERM, NULL);
        }
        return;
    }
    mh->fn (mh->d->h, mh, msg, mh->arg);
}

static bool dispatch_message (struct dispatch *d,
                              const flux_msg_t *msg, int type)
{
    flux_msg_handler_t *mh;
    bool match = false;

    /* rpc w/matchtag */
    if (type == FLUX_MSGTYPE_RESPONSE) {
        uint32_t matchtag;
        if (flux_msg_get_matchtag (msg, &matchtag) == 0
                && matchtag != FLUX_MATCHTAG_NONE
                && (mh = zhashx_lookup (d->handlers_rpc, &matchtag))
                && mh->running
                && flux_msg_cmp (msg, mh->match)) {
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
    if (matchtag == FLUX_MATCHTAG_NONE)
        return; // no matchtag was allocated
    if (flux_matchtag_group (matchtag))
        return; // no way to tell here if an mrpc is complete
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

    const char *topic;
    flux_msg_get_topic (msg, &topic);
    /* Add any new handlers here, making handler creation
     * safe to call during handlers list traversal below.
     */
    if (transfer_items_zlist (d->handlers_new, d->handlers) < 0)
        goto done;

#if defined(HAVE_CALIPER)
    cali_begin_string (d->prof_msg_type, flux_msg_typestr (type));
    cali_begin_string (d->prof_msg_topic, topic);
    cali_begin (d->prof_msg_dispatch);
    cali_end (d->prof_msg_topic);
    cali_end (d->prof_msg_type);
#endif

    match = dispatch_message (d, msg, type);

#if defined(HAVE_CALIPER)
    cali_begin_string (d->prof_msg_type, flux_msg_typestr (type));
    cali_begin_string (d->prof_msg_topic, topic);
    cali_end (d->prof_msg_dispatch);
    cali_end (d->prof_msg_topic);
    cali_end (d->prof_msg_type);
#endif

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
                case FLUX_MSGTYPE_REQUEST:
                    if (flux_respond_error (d->h, msg, ENOSYS, NULL))
                        goto done;
                    break;
                case FLUX_MSGTYPE_EVENT:
                    break;
                case FLUX_MSGTYPE_RESPONSE:
                    handle_late_response (d, msg);
                    break;
                default:
                    if (flux_flags_get (d->h) & FLUX_O_TRACE) {
                        const char *topic = NULL;
                        (void)flux_msg_get_topic (msg, &topic);
                        fprintf (stderr,
                                 "nomatch: %s '%s'\n",
                                 flux_msg_typestr (type),
                                 topic ? topic : "");
                    }
                    break;
            }
        }
    }
    rc = 0;
done:
    if (rc < 0) {
        flux_reactor_stop_error (r);
        FLUX_FATAL (d->h);
    }
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
        if (mh->match.topic_glob)
            free (mh->match.topic_glob);
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
        } else {
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
                                             flux_msg_handler_f cb, void *arg)
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
    if (mh->match.typemask == FLUX_MSGTYPE_RESPONSE
                            && mh->match.matchtag != FLUX_MATCHTAG_NONE) {
        if (zhashx_insert (d->handlers_rpc, &mh->match.matchtag, mh) < 0) {
            errno = EEXIST;
            goto error;
        }
    } else {
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

int flux_msg_handler_addvec (flux_t *h,
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
        match.typemask = tab[i].typemask;
        /* flux_msg_handler_create() will make a copy of the topic_glob
         * so it is safe to temporarily remove "const" from
         * tab[i].topic_glob with a cast. */
        match.topic_glob = (char *)tab[i].topic_glob;
        if (!(handlers[i] = flux_msg_handler_create (h, match, tab[i].cb, arg)))
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
