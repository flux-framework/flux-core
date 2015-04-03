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
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <json.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/zconnect.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libutil/ev_zlist.h"

#include "module.h"

/* While transitioning to argc, argv - style args per RFC 5,
 * we have our own mod_main prototype.
 */
typedef int (mod_main_comms_f)(flux_t h, zhash_t *args);


typedef struct {
    int request_tx;
    int request_rx;
    int svc_tx;
    int svc_rx;
    int event_tx;
    int event_rx;
} mod_stats_t;

#define PLUGIN_MAGIC    0xfeefbe01
struct mod_ctx_struct {
    int magic;

    /* inproc:// sockets to broker
     * Created/destroyed in broker, used in module.
     */
    void *zs_request;       /* DEALER for making requests */
    void *zs_svc[2];        /* PAIR for handling requests 0=module, 1=broker */
    void *zs_evin;          /* SUB for handling subscribed-to events */

    /* putmsg queue - where we put messages that arrive before the one
     * we really want.  Afterwards, this queue must be processed until
     * empty, before handling any new messages.
     */
    zlist_t *putmsg;        /* queue of (zmsg_t *) */

    /* Event loop machinery.
     * main/putmsg pollers are turned on and off to give putmsg priority.
     */
    struct ev_loop *loop;
    ev_zlist putmsg_w;
    ev_zmq request_w;
    ev_zmq svc_w;
    ev_zmq evin_w;
    int loop_rc;
    int timer_seq;
    zhash_t *watchers;
    flux_msg_f msg_cb;
    void *msg_cb_arg;

    /* misc comms module state
     */
    zuuid_t *uuid;          /* uuid for unique request sender identity */
    pthread_t t;            /* module thread */
    mod_main_comms_f *main; /* dlopened mod_main() */
    mod_stats_t stats;   /* module message statistics */
    void *zctx;             /* broker zctx */
    flux_t h;               /* a module has a flux handle implicitly created */
    const char *name;       /* MOD_NAME */
    void *dso;              /* reference on dlopened module */
    int size;               /* size of .so file for lsmod */
    char *digest;           /* digest of .so file for lsmod */
    zhash_t *args;          /* hash of module arguments (FIXME RFC 5) */
    int rank;               /* broker rank */
};

#define HASHKEY_LEN 80

typedef struct {
    ev_timer w;
    FluxTmoutHandler cb;
    void *arg;
    int id;
} ptimer_t;

typedef struct {
    ev_zmq w;
    FluxZsHandler cb;
    void *arg;
} pzs_t;

typedef struct {
    ev_io w;
    FluxFdHandler cb;
    void *arg;
} pfd_t;


static void sync_msg_watchers (mod_ctx_t p);
static void mod_reactor_stop (void *impl, int rc);

static const struct flux_handle_ops mod_handle_ops;


/**
 ** flux_t implementation
 **/

static int mod_sendmsg (void *impl, zmsg_t **zmsg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    int type;
    int rc = -1;

    if (flux_msg_get_type (*zmsg, &type) < 0)
        goto done;
    if (type == FLUX_MSGTYPE_REQUEST) {
        rc = zmsg_send (zmsg, p->zs_request);
        p->stats.request_tx++;
    } else if (type == FLUX_MSGTYPE_RESPONSE) {
        rc = zmsg_send (zmsg, p->zs_svc[0]);
        p->stats.svc_tx++;
    } else
        errno = EINVAL;
done:
    return rc;
}

static zmsg_t *mod_recvmsg_main (mod_ctx_t p, bool nonblock)
{
    zmq_pollitem_t items[] = {
        {  .events = ZMQ_POLLIN, .socket = p->zs_evin },
        {  .events = ZMQ_POLLIN, .socket = p->zs_request },
        {  .events = ZMQ_POLLIN, .socket = p->zs_svc[0] },
    };
    int nitems = sizeof (items) / sizeof (items[0]);
    int rc, i;
    zmsg_t *zmsg = NULL;

    do {
        rc = zmq_poll (items, nitems, nonblock ? 0 : -1);
        if (rc < 0)
            goto done;
        if (rc > 0) {
            for (i = 0; i < nitems; i++) {
                if (items[i].revents & ZMQ_POLLIN) {
                    zmsg = zmsg_recv (items[i].socket);
                    goto done;
                }
            }
        }
    } while (!nonblock);
done:
    return zmsg;
}

static zmsg_t *mod_recvmsg_putmsg (mod_ctx_t p)
{
    zmsg_t *zmsg = zlist_pop (p->putmsg);
    if (zmsg) {
        if (zlist_size (p->putmsg) == 0)
            sync_msg_watchers (p);
    }
    return zmsg;
}

static zmsg_t *mod_recvmsg (void *impl, bool nonblock)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zmsg_t *zmsg = NULL;
    int type;

    if (!(zmsg = mod_recvmsg_putmsg (p)))
        zmsg = mod_recvmsg_main (p, nonblock);
    if (!zmsg)
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0) {
        zmsg_destroy (&zmsg);
        goto done;
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            p->stats.svc_rx++;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            p->stats.request_rx++;
            break;
        case FLUX_MSGTYPE_EVENT:
            p->stats.event_rx++;
            break;
    }
done:
    return zmsg;
}

static int mod_putmsg (void *impl, zmsg_t **zmsg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    int oldcount = zlist_size (p->putmsg);

    if (zlist_append (p->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (p);
    return 0;
}

static int mod_pushmsg (void *impl, zmsg_t **zmsg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    int oldcount = zlist_size (p->putmsg);

    if (zlist_push (p->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (p);
    return 0;
}

static void mod_purge (void *impl, flux_match_t match)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zmsg_t *zmsg = zlist_first (p->putmsg);

    while (zmsg) {
        if (flux_msg_cmp (zmsg, match)) {
            zlist_remove (p->putmsg, zmsg);
            zmsg_destroy (&zmsg);
        }
        zmsg = zlist_next (p->putmsg);
    }
}

static int mod_event_subscribe (void *impl, const char *topic)
{
    mod_ctx_t p = impl;
    char *s = topic ? (char *)topic : "";
    assert (p->magic == PLUGIN_MAGIC);
    return zmq_setsockopt (p->zs_evin, ZMQ_SUBSCRIBE, s, strlen (s));
}

static int mod_event_unsubscribe (void *impl, const char *topic)
{
    mod_ctx_t p = impl;
    char *s = topic ? (char *)topic : "";
    assert (p->magic == PLUGIN_MAGIC);
    return zmq_setsockopt (p->zs_evin, ZMQ_UNSUBSCRIBE, s, strlen (s));
}

static int mod_rank (void *impl)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return p->rank;
}

static zctx_t *mod_get_zctx (void *impl)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return p->zctx;
}

static int mod_reactor_start (void *impl)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);

    p->loop_rc = 0;
    ev_run (p->loop, 0);
    return p->loop_rc;
};

static void mod_reactor_stop (void *impl, int rc)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);

    p->loop_rc = rc;
    ev_break (p->loop, EVBREAK_ALL);
}

static int mod_reactor_msg_add (void *impl, flux_msg_f cb, void *arg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    int rc = -1;

    if (p->msg_cb != NULL) {
        errno = EBUSY;
        goto done;
    }
    p->msg_cb = cb;
    p->msg_cb_arg = arg;
    sync_msg_watchers (p);
    rc = 0;
done:
    return rc;
}

static void mod_reactor_msg_remove (void *impl)
{
    mod_ctx_t p = impl;
    p->msg_cb = NULL;
    p->msg_cb_arg = NULL;
    sync_msg_watchers (p);
}

static void fd_cb (struct ev_loop *looop, ev_io *w, int revents)
{
    pfd_t *f = (pfd_t *)((char *)w - offsetof (pfd_t, w));
    mod_ctx_t p = w->data;
    assert (p->magic == PLUGIN_MAGIC);

    if (f->cb (p->h, w->fd, etoz (revents), f->arg) < 0)
        mod_reactor_stop (p, -1);
}

static int mod_reactor_fd_add (void *impl, int fd, int events,
                                  FluxFdHandler cb, void *arg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    char hashkey[HASHKEY_LEN];

    pfd_t *f = xzmalloc (sizeof (*f));
    ev_io_init (&f->w, fd_cb, fd, ztoe (events));
    f->w.data = p;
    f->cb = cb;
    f->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    zhash_update (p->watchers, hashkey, f);
    zhash_freefn (p->watchers, hashkey, free);

    ev_io_start (p->loop, &f->w);

    return 0;
}

static void mod_reactor_fd_remove (void *impl, int fd, int events)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    pfd_t *f = zhash_lookup (p->watchers, hashkey);
    if (f) {
        ev_io_stop (p->loop, &f->w);
        zhash_delete (p->watchers, hashkey);
    }
}

static void zs_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    pzs_t *z = (pzs_t *)((char *)w - offsetof (pzs_t, w));
    mod_ctx_t p = z->w.data;
    assert (p->magic == PLUGIN_MAGIC);

    if (z->cb (p->h, w->zsock, etoz (revents), z->arg) < 0)
        mod_reactor_stop (p, -1);
}

static int mod_reactor_zs_add (void *impl, void *zs, int events,
                                  FluxZsHandler cb, void *arg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    char hashkey[HASHKEY_LEN];

    pzs_t *z = xzmalloc (sizeof (*z));
    ev_zmq_init (&z->w, zs_cb, zs, ztoe (events));
    z->w.data = p;
    z->cb = cb;
    z->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    zhash_update (p->watchers, hashkey, z);
    zhash_freefn (p->watchers, hashkey, free);

    ev_zmq_start (p->loop, &z->w);

    return 0;
}

static void mod_reactor_zs_remove (void *impl, void *zs, int events)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    pzs_t *z = zhash_lookup (p->watchers, hashkey);
    if (z) {
        ev_zmq_stop (p->loop, &z->w);
        zhash_delete (p->watchers, hashkey);
    }
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    ptimer_t *t = (ptimer_t *)((char *)w - offsetof (ptimer_t, w));
    mod_ctx_t p = w->data;
    assert (p->magic == PLUGIN_MAGIC);

    if (t->cb (p->h, t->arg) < 0)
        mod_reactor_stop (p, -1);
}

static int mod_reactor_tmout_add (void *impl, unsigned long msec,
                                     bool oneshot,
                                     FluxTmoutHandler cb, void *arg)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    double after = (double)msec / 1000.0;
    double repeat = oneshot ? 0 : after;
    char hashkey[HASHKEY_LEN];

    ptimer_t *t = xzmalloc (sizeof (*t));
    t->id = p->timer_seq++;
    t->w.data = p;
    ev_timer_init (&t->w, timer_cb, after, repeat);
    t->cb = cb;
    t->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "timer:%d", t->id);
    zhash_update (p->watchers, hashkey, t);
    zhash_freefn (p->watchers, hashkey, free);

    ev_timer_start (p->loop, &t->w);

    return t->id;
}

static void mod_reactor_tmout_remove (void *impl, int timer_id)
{
    mod_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "timer:%d", timer_id);
    ptimer_t *t = zhash_lookup (p->watchers, hashkey);
    if (t) {
        ev_timer_stop (p->loop, &t->w);
        zhash_delete (p->watchers, hashkey);
    }
}

/**
 ** end of handle implementation
 **/

static int ping_req_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    json_object *o = NULL;
    char *s = NULL;
    int rc = 0;

    if (flux_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL ||
        !json_object_is_type (o, json_type_object)) {
        flux_err_respond (h, EPROTO, zmsg);
        goto done; /* reactor continues */
    }

    /* Route string will not include the endpoints.
     * On arrival here, uuid of dst module has been stripped.
     * The '1' arg to zdump_routestr strips the uuid of the sender.
     */
    s = zdump_routestr (*zmsg, 1);
    util_json_object_add_string (o, "route", s);
    if (flux_respond (h, zmsg, o) < 0) {
        err ("%s: flux_respond", __FUNCTION__);
        rc = -1; /* reactor terminates */
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (s)
        free (s);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static int stats_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    mod_ctx_t p = arg;
    json_object *o = NULL;
    int rc = 0;
    char *tag = NULL;

    if (flux_msg_decode (*zmsg, &tag, NULL) < 0) {
        flux_log (p->h, LOG_ERR, "%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (fnmatch ("*.get", tag, 0) == 0) {
        o = util_json_object_new_object ();
        util_json_object_add_int (o, "#request (tx)", p->stats.request_tx);
        util_json_object_add_int (o, "#request (rx)", p->stats.request_rx);
        util_json_object_add_int (o, "#svc (tx)",     p->stats.svc_tx);
        util_json_object_add_int (o, "#svc (rx)",     p->stats.svc_rx);
        util_json_object_add_int (o, "#event (tx)",   p->stats.event_tx);
        util_json_object_add_int (o, "#event (rx)",   p->stats.event_rx);
        if (flux_respond (h, zmsg, o) < 0) {
            err ("%s: flux_respond", __FUNCTION__);
            rc = -1;
            goto done;
        }
    } else if (fnmatch ("*.clear", tag, 0) == 0) {
        memset (&p->stats, 0, sizeof (p->stats));
        if (typemask & FLUX_MSGTYPE_REQUEST) {
            if (flux_respond_errnum (h, zmsg, 0) < 0) {
                err ("%s: flux_respond_errnum", __FUNCTION__);
                rc = -1;
                goto done;
            }
        }
    } else {
        flux_log (p->h, LOG_ERR, "%s: %s: unknown tag", __FUNCTION__, tag);
    }
done:
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static int rusage_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    mod_ctx_t p = arg;
    json_object *response = NULL;
    int rc = 0;
    struct rusage usage;

    if (flux_msg_decode (*zmsg, NULL, NULL) < 0) {
        flux_log (p->h, LOG_ERR, "%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (getrusage (RUSAGE_THREAD, &usage) < 0) {
        if (flux_respond_errnum (h, zmsg, errno) < 0) {
            err ("%s: flux_respond_errnum", __FUNCTION__);
            rc = -1;
            goto done;
        }
        goto done;
    }
    response = rusage_to_json (&usage);
    if (flux_respond (h, zmsg, response) < 0) {
        err ("%s: flux_respond", __FUNCTION__);
        rc = -1;
        goto done;
    }
done:
    if (response)
        json_object_put (response);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static int shutdown_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    mod_ctx_t p = arg;
    mod_reactor_stop (p, 0);
    zmsg_destroy (zmsg);
    return 0;
}

static void main_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    mod_ctx_t p = w->data;
    assert (p->magic == PLUGIN_MAGIC);

    assert (zlist_size (p->putmsg) == 0);

    if ((revents & EV_ERROR)) {
        msg ("%s: error polling inproc socket", p->name);
        mod_reactor_stop (p, -1);
    } else if ((revents & EV_READ)) {
        if (p->msg_cb) {
            if (p->msg_cb (p->h, p->msg_cb_arg) < 0)
                mod_reactor_stop (p, -1);
        }
    }
}

static void putmsg_cb (struct ev_loop *loop, ev_zlist *w, int revents)
{
    mod_ctx_t p = w->data;
    assert (p->magic == PLUGIN_MAGIC);

    assert (zlist_size (p->putmsg) > 0);
    if (p->msg_cb) {
        if (p->msg_cb (p->h, p->msg_cb_arg) < 0)
            mod_reactor_stop (p, -1);
    }
}

/* Enable/disable the appropriate watchers to give putmsg priority
 * over the main zsockets.
 */
static void sync_msg_watchers (mod_ctx_t p)
{
    if (p->msg_cb == NULL) {
        ev_zmq_stop (p->loop, &p->request_w);
        ev_zmq_stop (p->loop, &p->svc_w);
        ev_zmq_stop (p->loop, &p->evin_w);
        ev_zlist_stop (p->loop, &p->putmsg_w);
    } else if (zlist_size (p->putmsg) == 0) {
        ev_zmq_start (p->loop, &p->request_w);
        ev_zmq_start (p->loop, &p->svc_w);
        ev_zmq_start (p->loop, &p->evin_w);
        ev_zlist_stop (p->loop, &p->putmsg_w);
    } else {
        ev_zmq_stop (p->loop, &p->request_w);
        ev_zmq_stop (p->loop, &p->svc_w);
        ev_zmq_stop (p->loop, &p->evin_w);
        ev_zlist_start (p->loop, &p->putmsg_w);
    }
}

static void register_event (mod_ctx_t p, char *name, FluxMsgHandler cb)
{
    char *s;

    if (asprintf (&s, "%s.%s", p->name, name) < 0)
        oom ();
    if (flux_msghandler_add (p->h, FLUX_MSGTYPE_EVENT, s, cb, p) < 0)
        err_exit ("%s: flux_msghandler_add", p->name);
    /* Trim a glob wildcard off the end to form a subscription string.
     * 0MQ subscriptions are a rooted substring match.
     * Globs in other places are fatal.
     */
    if (s[strlen (s) - 1] == '*')
        s[strlen (s) - 1] = '\0';
    if (strchr (s, '*'))
        err_exit ("%s: cant deal with '%s' subscription", __FUNCTION__, name);
    if (flux_event_subscribe (p->h, s) < 0)
        err_exit ("%s: flux_event_subscribe %s", p->name, s);
    free (s);
}

static void register_request (mod_ctx_t p, char *name, FluxMsgHandler cb)
{
    char *s;

    if (asprintf (&s, "%s.%s", p->name, name) < 0)
        oom ();
    if (flux_msghandler_add (p->h, FLUX_MSGTYPE_REQUEST, s, cb, p) < 0)
        err_exit ("%s: flux_msghandler_add %s", p->name, s);
    free (s);
}

static void *mod_thread (void *arg)
{
    mod_ctx_t p = arg;
    sigset_t signal_set;
    int errnum;

    /* block all signals */
    if (sigfillset (&signal_set) < 0)
        err_exit ("%s: sigfillset", p->name);
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0)
        errn_exit (errnum, "pthread_sigmask");

    /* prep the event loop
     */
    if (!(p->loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("%s: ev_loop_new", p->name);

    /* Register callbacks for "internal" methods.
     * These can be overridden in p->ops->main() if desired.
     */
    register_request (p, "shutdown",shutdown_cb);
    register_request (p, "ping",    ping_req_cb);
    register_request (p, "stats.*", stats_cb);
    register_request (p, "rusage", rusage_cb);
    register_event   (p, "stats.*", stats_cb);

    if (p->main(p->h, p->args) < 0) {
        err ("%s: mod_main returned error", p->name);
        goto done;
    }
done:
    ev_loop_destroy (p->loop);
    zstr_send (p->zs_svc[0], ""); /* EOF */

    return NULL;
}

const char *mod_name (mod_ctx_t p)
{
    return p->name;
}

const char *mod_uuid (mod_ctx_t p)
{
    return zuuid_str (p->uuid);
}

void *mod_sock (mod_ctx_t p)
{
    return p->zs_svc[1];
}

const char *mod_digest (mod_ctx_t p)
{
    return p->digest;
}

int mod_size (mod_ctx_t p)
{
    return p->size;
}

void mod_destroy (mod_ctx_t p)
{
    int errnum;

    if (p->t) {
        errnum = pthread_join (p->t, NULL);
        if (errnum)
            errn_exit (errnum, "pthread_join");
    }

    flux_handle_destroy (&p->h);

    zsocket_destroy (p->zctx, p->zs_evin);
    zsocket_destroy (p->zctx, p->zs_svc[0]);
    zsocket_destroy (p->zctx, p->zs_svc[1]);
    zsocket_destroy (p->zctx, p->zs_request);

    zmsg_t *zmsg;
    while ((zmsg = zlist_pop (p->putmsg)))
        zmsg_destroy (&zmsg);
    zlist_destroy (&p->putmsg);
    zhash_destroy (&p->watchers);

    dlclose (p->dso);
    zuuid_destroy (&p->uuid);
    free (p->digest);

    free (p);
}

/* Send shutdown request, broker to module.
 */
void mod_stop (mod_ctx_t p)
{
    char *topic = xasprintf ("%s.shutdown", p->name);
    zmsg_t *zmsg;
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (zmsg_send (&zmsg, p->zs_svc[1]) < 0)
        goto done;
done:
    free (topic);
    zmsg_destroy (&zmsg);
}

void mod_start (mod_ctx_t p)
{
    int errnum;
    errnum = pthread_create (&p->t, NULL, mod_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create");
}

mod_ctx_t mod_create (flux_t h, const char *path, zhash_t *args)
{
    mod_ctx_t p;
    void *dso;
    const char **mod_namep;
    mod_main_comms_f *mod_main;
    zfile_t *zf;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_LOCAL))) {
        msg ("%s", dlerror ());
        errno = ENOENT;
        return NULL;
    }
    mod_main = dlsym (dso, "mod_main");
    mod_namep = dlsym (dso, "mod_name");
    if (!mod_main || !mod_namep || !*mod_namep) {
        err ("%s: mod_main or mod_name undefined", path);
        dlclose (dso);
        errno = ENOENT;
        return NULL;
    }

    p = xzmalloc (sizeof (*p));
    p->magic = PLUGIN_MAGIC;
    p->zctx = flux_get_zctx (h);
    p->args = args;
    p->main = mod_main;
    p->dso = dso;
    zf = zfile_new (NULL, path);
    p->digest = xstrdup (zfile_digest (zf));
    p->size = (int)zfile_cursize (zf);
    zfile_destroy (&zf);
    p->name = *mod_namep;
    if (!(p->uuid = zuuid_new ()))
        oom ();
    p->rank = flux_rank (h);

    p->h = flux_handle_create (p, &mod_handle_ops, 0);
    flux_log_set_facility (p->h, p->name);

    /* connect sockets in the parent, then use them in the thread */
    zconnect (p->zctx, &p->zs_request, ZMQ_DEALER, REQUEST_URI, -1,
              zuuid_str (p->uuid));
    zconnect (p->zctx, &p->zs_evin,  ZMQ_SUB, EVENT_URI, 0, NULL);

    char *svc_uri = xasprintf ("inproc://svc-%s", p->name);
    zbind (p->zctx, &p->zs_svc[1], ZMQ_PAIR, svc_uri, -1);
    zconnect (p->zctx, &p->zs_svc[0], ZMQ_PAIR, svc_uri, -1, NULL);
    free (svc_uri);

    if (!(p->putmsg = zlist_new ()))
        oom ();
    if (!(p->watchers = zhash_new ()))
        oom ();
    ev_zmq_init (&p->request_w, main_cb, p->zs_request, EV_READ);
    ev_zmq_init (&p->svc_w, main_cb, p->zs_svc[0], EV_READ);
    ev_zmq_init (&p->evin_w, main_cb, p->zs_evin, EV_READ);
    ev_zlist_init (&p->putmsg_w, putmsg_cb, p->putmsg, EV_READ);
    p->request_w.data = p->svc_w.data = p->evin_w.data = p->putmsg_w.data = p;
    return p;
}


static const struct flux_handle_ops mod_handle_ops = {
    .sendmsg = mod_sendmsg,
    .recvmsg = mod_recvmsg,
    .putmsg = mod_putmsg,
    .pushmsg = mod_pushmsg,
    .purge = mod_purge,
    .event_subscribe = mod_event_subscribe,
    .event_unsubscribe = mod_event_unsubscribe,
    .rank = mod_rank,
    .get_zctx = mod_get_zctx,
    .reactor_stop = mod_reactor_stop,
    .reactor_start = mod_reactor_start,
    .reactor_fd_add = mod_reactor_fd_add,
    .reactor_fd_remove = mod_reactor_fd_remove,
    .reactor_zs_add = mod_reactor_zs_add,
    .reactor_zs_remove = mod_reactor_zs_remove,
    .reactor_tmout_add = mod_reactor_tmout_add,
    .reactor_tmout_remove = mod_reactor_tmout_remove,
    .reactor_msg_add = mod_reactor_msg_add,
    .reactor_msg_remove = mod_reactor_msg_remove,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
