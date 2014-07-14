/* handle.c - core flux_t handle operations */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>
#include <fnmatch.h>

#include "hostlist.h"
#include "log.h"
#include "zmsg.h"
#include "util.h"

#include "flux.h"
#include "handle.h"

typedef struct reactor_struct *reactor_t;

static void reactor_destroy (reactor_t r);
static reactor_t reactor_create (void);

struct flux_handle_struct {
    const struct flux_handle_ops *ops;
    int             flags;
    void            *impl;
    reactor_t       reactor;
    zhash_t         *aux;
};

flux_t handle_create (void *impl, const struct flux_handle_ops *ops, int flags)
{
    flux_t h = xzmalloc (sizeof (*h));

    h->flags = flags;
    if (!(h->aux = zhash_new()))
        oom ();
    h->reactor = reactor_create ();
    h->ops = ops;
    h->impl = impl;

    return h;
}

void flux_handle_destroy (flux_t *hp)
{
    flux_t h;

    if (hp && (h = *hp)) {
        if (h->ops->impl_destroy)
            h->ops->impl_destroy (h->impl);
        zhash_destroy (&h->aux);
        reactor_destroy (h->reactor);
        free (h);
        *hp = NULL;
    }
}

void flux_flags_set (flux_t h, int flags)
{
    h->flags |= flags;
}

void flux_flags_unset (flux_t h, int flags)
{
    h->flags &= ~flags;
}

void *flux_aux_get (flux_t h, const char *name)
{
    return zhash_lookup (h->aux, name);
}

void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy)
{
    zhash_update (h->aux, name, aux);
    zhash_freefn (h->aux, name, destroy);
}

int flux_request_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->request_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg, flux_msgtype_shortstr (FLUX_MSGTYPE_REQUEST));

    return h->ops->request_sendmsg (h->impl, zmsg);
}

zmsg_t *flux_request_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->ops->request_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->ops->request_recvmsg (h->impl, nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg, flux_msgtype_shortstr (FLUX_MSGTYPE_REQUEST));

    return zmsg;
}

int flux_response_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->response_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg, flux_msgtype_shortstr(FLUX_MSGTYPE_RESPONSE));

    return h->ops->response_sendmsg (h->impl, zmsg);
}

zmsg_t *flux_response_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->ops->response_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->ops->response_recvmsg (h->impl, nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg, flux_msgtype_shortstr (FLUX_MSGTYPE_RESPONSE));

    return zmsg;
}

int flux_response_putmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->response_putmsg) {
        errno = ENOSYS;
        return -1;
    }

    return h->ops->response_putmsg (h->impl, zmsg);
}

zmsg_t *flux_event_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->ops->event_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->ops->event_recvmsg (h->impl, nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg, flux_msgtype_shortstr (FLUX_MSGTYPE_EVENT));

    return zmsg;
}

int flux_event_subscribe (flux_t h, const char *topic)
{
    if (!h->ops->event_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->ops->event_subscribe (h->impl, topic);
}

int flux_event_unsubscribe (flux_t h, const char *topic)
{
    if (!h->ops->event_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->ops->event_unsubscribe (h->impl, topic);
}

int flux_rank (flux_t h)
{
    if (!h->ops->rank) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->rank (h->impl);
}

zctx_t *flux_get_zctx (flux_t h)
{
    if (!h->ops->get_zctx) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_zctx (h->impl);
}

flux_sec_t flux_get_sec (flux_t h)
{
    if (!h->ops->get_sec) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_sec (h->impl);
}

/**
 ** Utility
 **/

struct map_struct {
    const char *name;
    const char *sname;
    int typemask;
};

static struct map_struct msgtype_map[] = {
    { "request", ">", FLUX_MSGTYPE_REQUEST },
    { "response", "<", FLUX_MSGTYPE_RESPONSE},
    { "event", "e", FLUX_MSGTYPE_EVENT},
};
static const int msgtype_map_len = 
                            sizeof (msgtype_map) / sizeof (msgtype_map[0]);

const char *flux_msgtype_string (int typemask)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((typemask & msgtype_map[i].typemask))
            return msgtype_map[i].name;
    return "unknown";
}

const char *flux_msgtype_shortstr (int typemask)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((typemask & msgtype_map[i].typemask))
            return msgtype_map[i].sname;
    return "?";
}

static zframe_t *unwrap_zmsg (zmsg_t *zmsg, int frameno)
{
    zframe_t *zf = zmsg_first (zmsg);

    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty routing envelope frames */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty routing envelope delimiter */
    if (!zf)
        zf = zmsg_first (zmsg); /* rewind - there was no routing envelope */
    while (zf && frameno-- > 0) /* frame 0=tag, 1=json */
        zf = zmsg_next (zmsg);
    return zf;
}

char *flux_zmsg_tag (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 0);
    char *tag = NULL;

    if (zf && !(tag = zframe_strdup (zf)))
        oom ();
    return tag;
}

json_object *flux_zmsg_json (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 1);
    json_object *o = NULL;

    if (zf) {
        json_tokener *tok;
        if (!(tok = json_tokener_new ()))
            oom ();
        o = json_tokener_parse_ex (tok,  (const char *)zframe_data (zf),
                                                       zframe_size (zf));
        json_tokener_free (tok);
    } else {
        if (!(o = json_object_new_object ()))
            oom ();
    }
    return o;
}

/**
 ** Reactor
 **/

typedef enum {
    DSP_TYPE_MSG, DSP_TYPE_FD, DSP_TYPE_ZS, DSP_TYPE_TMOUT
} dispatch_type_t;

typedef struct {
    dispatch_type_t type;
    union {
        struct {
            int typemask;
            char *pattern;
            FluxMsgHandler fn;
            void *arg;
        } msg;
        struct {
            int fd;
            short events;
            FluxFdHandler fn;
            void *arg;
        } fd;
        struct {
            void *zs;
            short events;
            FluxZsHandler fn;
            void *arg;
        } zs;
        struct {
            int timer_id;
            FluxTmoutHandler fn;
            void *arg;
        } tmout;
    };
} dispatch_t;

struct reactor_struct {
    zlist_t *dsp;
    bool timeout_set;
};

static bool reactor_empty (reactor_t r)
{
    return ((zlist_size (r->dsp) == 0) && !r->timeout_set);
}

static dispatch_t *dispatch_create (dispatch_type_t type)
{
    dispatch_t *d = xzmalloc (sizeof (*d));
    d->type = type;
    return d;
}

static void dispatch_destroy (dispatch_t *d)
{
    if (d->type == DSP_TYPE_MSG && d->msg.pattern)
        free (d->msg.pattern);
    free (d);
}

static reactor_t reactor_create (void)
{
    reactor_t r = xzmalloc (sizeof (*r));
    if (!(r->dsp = zlist_new ()))
        oom ();
    return r;
}

static void reactor_destroy (reactor_t r)
{
    dispatch_t *d;

    while ((d = zlist_pop (r->dsp)))
        dispatch_destroy (d);
    zlist_destroy (&r->dsp);
    free (r);
}

static bool dispatch_msg_match (dispatch_t *d, const char *tag, int typemask)
{
    if (d->type == DSP_TYPE_MSG) {
        if (!(d->msg.typemask & (typemask & FLUX_MSGTYPE_MASK)))
            return false;
        if (!d->msg.pattern)
            return true;
        if (fnmatch (d->msg.pattern, tag, 0) == 0)
            return true;
    }
    return false;
}

int handle_event_msg (flux_t h, int typemask, zmsg_t **zmsg)
{
    dispatch_t *d;
    char *tag;
    int rc = 0;

    assert (*zmsg != NULL);
    if (!(tag = flux_zmsg_tag (*zmsg))) {
        rc = -1;
        errno = EPROTO;
        goto done;
    }
    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (dispatch_msg_match (d, tag, typemask) && d->msg.fn != NULL) {
            rc = d->msg.fn (h, typemask, zmsg, d->msg.arg);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
done:
    if (tag)
        free (tag);
    /* If we return with zmsg unconsumed, the impl's reactor will
     * dispose of it.
     */
    return rc;
}

int handle_event_fd (flux_t h, int fd, short events)
{
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_FD && d->fd.fd == fd
                            && (d->fd.events & events) && d->fd.fn != NULL) {
            rc = d->fd.fn (h, fd, events, d->fd.arg);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    return rc;
}

int handle_event_zs (flux_t h, void *zs, short events)
{
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_ZS && d->zs.zs == zs
                                   && (d->zs.events & events)) {
            rc = d->zs.fn (h, zs, events, d->zs.arg);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    return rc;
}

int handle_event_tmout (flux_t h, int timer_id)
{
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_TMOUT && d->tmout.fn != NULL
                                      && d->tmout.timer_id == timer_id) {
            rc = d->tmout.fn (h, d->tmout.arg);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    return rc;
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    dispatch_t *d;
    int rc = -1;

    if (typemask == 0 || !pattern || !cb) {
        errno = EINVAL;
        goto done;
    }
    d = dispatch_create (DSP_TYPE_MSG);
    d->msg.typemask = typemask;
    d->msg.pattern = xstrdup (pattern);
    d->msg.fn = cb;
    d->msg.arg = arg;
    if (zlist_push (h->reactor->dsp, d) < 0)
        oom ();
    rc = 0;
done:
    return rc;
}

int flux_msghandler_addvec (flux_t h, msghandler_t *handlers, int len,
                            void *arg)
{
    int i;

    for (i = 0; i < len; i++)
        if (flux_msghandler_add (h, handlers[i].typemask, handlers[i].pattern,
                                    handlers[i].cb, arg) < 0)
            return -1;
    return 0;
}

void flux_msghandler_remove (flux_t h, int typemask, const char *pattern)
{
    dispatch_t *d;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_MSG && d->msg.typemask == typemask
                                    && !strcmp (d->msg.pattern, pattern)) {
            zlist_remove (h->reactor->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (h->reactor))
                flux_reactor_stop (h);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
}

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    dispatch_t *d;
    int rc = -1;

    if (fd < 0 || events == 0 || !cb) {
        errno = EINVAL;
        goto done;
    }
    if (!h->ops->reactor_fd_add) {
        errno = ENOSYS;
        goto done;
    }
    if (h->ops->reactor_fd_add (h->impl, fd, events) < 0)
        goto done;

    d = dispatch_create (DSP_TYPE_FD);
    d->fd.fd = fd;
    d->fd.events = events;
    d->fd.fn = cb;
    d->fd.arg = arg;
    if (zlist_append (h->reactor->dsp, d) < 0)
        oom ();
    rc = 0;
done:
    return rc;
}

void flux_fdhandler_remove (flux_t h, int fd, short events)
{
    dispatch_t *d;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_FD && d->fd.fd == fd
                                   && d->fd.events == events) {
            zlist_remove (h->reactor->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (h->reactor))
                flux_reactor_stop (h);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    if (h->ops->reactor_fd_remove)
        h->ops->reactor_fd_remove (h->impl, fd, events);
}

int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg)
{
    dispatch_t *d;

    if (!h->ops->reactor_zs_add) {
        errno = ENOSYS;
        return -1;
    }
    if (h->ops->reactor_zs_add (h->impl, zs, events) < 0)
        return -1;
    d = dispatch_create (DSP_TYPE_ZS);
    d->zs.zs = zs;
    d->zs.events = events;
    d->zs.fn = cb;
    d->zs.arg = arg;
    if (zlist_append (h->reactor->dsp, d) < 0)
        oom ();
    return 0;
}

void flux_zshandler_remove (flux_t h, void *zs, short events)
{
    dispatch_t *d;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_ZS && d->zs.zs == zs
                                   && d->zs.events == events) {
            zlist_remove (h->reactor->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (h->reactor))
                flux_reactor_stop (h);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    if (h->ops->reactor_zs_remove)
        h->ops->reactor_zs_remove (h->impl, zs, events);
}

int flux_tmouthandler_add (flux_t h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
{
    dispatch_t *d;
    int id;

    if (!h->ops->reactor_tmout_add) {
        errno = ENOSYS;
        return -1;
    }
    if ((id = h->ops->reactor_tmout_add (h->impl, msec, oneshot)) < 0)
        return -1;
    d = dispatch_create (DSP_TYPE_TMOUT);
    d->tmout.fn = cb;
    d->tmout.arg = arg;
    d->tmout.timer_id = id;
    if (zlist_append (h->reactor->dsp, d) < 0)
        oom ();
    return id;
}

void flux_tmouthandler_remove (flux_t h, int timer_id)
{
    dispatch_t *d;

    d = zlist_first (h->reactor->dsp);
    while (d) {
        if (d->type == DSP_TYPE_TMOUT && d->tmout.timer_id == timer_id) {
            zlist_remove (h->reactor->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (h->reactor))
                flux_reactor_stop (h);
            break;
        }
        d = zlist_next (h->reactor->dsp);
    }
    if (h->ops->reactor_tmout_remove)
        h->ops->reactor_tmout_remove (h->impl, timer_id);
}

int flux_reactor_start (flux_t h)
{
    if (!h->ops->reactor_start) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->reactor_start (h->impl);
}

void flux_reactor_stop (flux_t h)
{
    if (h->ops->reactor_stop)
        h->ops->reactor_stop (h->impl, 0);
}

/**
 ** Higher level functions built on those above
 **/

int flux_event_recv (flux_t h, json_object **respp, char **tagp, bool nb)
{
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_event_recvmsg (h, nb)))
        goto done;
    if (cmb_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

int flux_request_send (flux_t h, json_object *request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;
    json_object *empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = cmb_msg_encode (tag, request);
    free (tag);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if ((rc = flux_request_sendmsg (h, &zmsg)) < 0)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    return rc;
}

int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb)
{
    zmsg_t *zmsg;
    int rc = -1;

    if (!(zmsg = flux_response_recvmsg (h, nb)))
        goto done;
    if (cmb_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

zmsg_t *flux_response_matched_recvmsg (flux_t h, const char *match, bool nb)
{
    zmsg_t *zmsg, *response = NULL;
    zlist_t *nomatch;

    if (!(nomatch = zlist_new ()))
        oom ();
    do {
        if (!(response = flux_response_recvmsg (h, nb)))
            goto done;
        if (!cmb_msg_match (response, match)) {
            if (zlist_append (nomatch, response) < 0)
                oom ();
            response = NULL;
        }
    } while (!response);
done:
    if (nomatch) {
        while ((zmsg = zlist_pop (nomatch))) {
            if (flux_response_putmsg (h, &zmsg) < 0)
                zmsg_destroy (&zmsg);
        }
        zlist_destroy (&nomatch);
    }
    return response;
}

json_object *flux_rpc (flux_t h, json_object *request, const char *fmt, ...)
{
    char *tag = NULL;
    json_object *response = NULL;
    zmsg_t *zmsg = NULL;
    va_list ap;
    json_object *empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = cmb_msg_encode (tag, request);

    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if (flux_request_sendmsg (h, &zmsg) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, tag, false)))
        goto done;
    if (cmb_msg_decode (zmsg, NULL, &response) < 0 || !response)
        goto done;
    if (util_json_object_get_int (response, "errnum", &errno) == 0) {
        json_object_put (response);
        response = NULL;
        goto done;
    }
done:
    if (tag)
        free (tag);
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    return response;
}

int flux_respond (flux_t h, zmsg_t **reqmsg, json_object *response)
{
    if (cmb_msg_replace_json (*reqmsg, response) < 0)
        return -1;
    return flux_response_sendmsg (h, reqmsg);
}

int flux_respond_errnum (flux_t h, zmsg_t **reqmsg, int errnum)
{
    if (cmb_msg_replace_json_errnum (*reqmsg, errnum) < 0)
        return -1;
    return flux_response_sendmsg (h, reqmsg);
}

void flux_assfail (flux_t h, char *ass, char *file, int line)
{
    flux_log (h, LOG_CRIT, "assertion failure: %s:%d: %s", file, line, ass);
    sleep (5);
    if (raise (SIGABRT) < 0)
        exit (1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
