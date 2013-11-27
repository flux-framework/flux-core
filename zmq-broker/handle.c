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

#include "hostlist.h"
#include "log.h"
#include "zmsg.h"
#include "util.h"

#include "flux.h"
#include "handle.h"

struct flux_handle_struct {
    const struct flux_handle_ops    *ops;
    int                             flags;
    void                            *impl;
    zhash_t                         *aux;
};

flux_t flux_handle_create (void *impl, const struct flux_handle_ops *ops,
                           int flags)
{
    flux_t h = xzmalloc (sizeof (*h));

    h->flags = flags;
    if (!(h->aux = zhash_new()))
        oom ();
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
        zmsg_dump_compact (*zmsg);

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
        zmsg_dump_compact (zmsg);

    return zmsg;
}

int flux_response_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->response_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

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
        zmsg_dump_compact (zmsg);

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

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->ops->event_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->ops->event_sendmsg (h->impl, zmsg);
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
        zmsg_dump_compact (zmsg);

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

zmsg_t *flux_snoop_recvmsg (flux_t h, bool nonblock)
{
    if (!h->ops->snoop_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->snoop_recvmsg (h->impl, nonblock);
}

int flux_snoop_subscribe (flux_t h, const char *topic)
{
    if (!h->ops->snoop_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->ops->snoop_subscribe (h->impl, topic);
}

int flux_snoop_unsubscribe (flux_t h, const char *topic)
{
    if (!h->ops->snoop_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->ops->snoop_unsubscribe (h->impl, topic);
}

int flux_timeout_set (flux_t h, unsigned long msec)
{
    if (!h->ops->timeout_set) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->timeout_set (h->impl, msec);
}

int flux_timeout_clear (flux_t h)
{
    if (!h->ops->timeout_clear) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->timeout_clear (h->impl);
}

bool flux_timeout_isset (flux_t h)
{
    if (!h->ops->timeout_isset)
        return false;
    return h->ops->timeout_isset (h->impl);
}

int flux_rank (flux_t h)
{
    if (!h->ops->rank) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->rank (h->impl);
}

zloop_t *flux_get_zloop (flux_t h)
{
    if (!h->ops->get_zloop) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_zloop (h->impl);
}

zctx_t *flux_get_zctx (flux_t h)
{
    if (!h->ops->get_zctx) {
        errno = ENOSYS;
        return NULL;
    }
    return h->ops->get_zctx (h->impl);
}

/**
 ** Utility
 **/

struct map_struct {
    const char *name;
    int typemask;
};

static struct map_struct msgtype_map[] = {
    { "request", FLUX_MSGTYPE_REQUEST },
    { "response", FLUX_MSGTYPE_RESPONSE},
    { "event", FLUX_MSGTYPE_EVENT},
    { "snoop", FLUX_MSGTYPE_SNOOP},
};

const char *flux_msgtype_string (int typemask)
{
    const int len = sizeof (msgtype_map) / sizeof (msgtype_map[0]);
    int i;

    for (i = 0; i < len; i++)
        if ((typemask & msgtype_map[i].typemask))
            return msgtype_map[i].name;
    return "unknown";
}

static zframe_t *tag_frame (zmsg_t *zmsg)
{
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty */
    if (!zf)
        zf = zmsg_first (zmsg); /* rewind - there was no envelope */
    return zf;
}

static zframe_t *json_frame (zmsg_t *zmsg)
{
    zframe_t *zf = tag_frame (zmsg);

    return (zf ? zmsg_next (zmsg) : NULL);
}

char *flux_zmsg_tag (zmsg_t *zmsg)
{
    zframe_t *zf = tag_frame (zmsg);
    char *tag;

    if (!zf)
        return NULL;
    if (!(tag = zframe_strdup (zf)))
        oom ();
    return tag;
}

json_object *flux_zmsg_json (zmsg_t *zmsg)
{
    zframe_t *zf = json_frame (zmsg);
    json_object *o;

    if (!zf)
        return NULL;
    util_json_decode (&o, (char *)zframe_data (zf), zframe_size (zf));
    return o;
}

/**
 ** Reactor
 **/

static void reactor_recvmsg (int typemask, zmsg_t **zmsg, void *arg)
{
    //flux_t h = arg;
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    return 0;
}

int flux_msghandler_append (flux_t h, int typemask, const char *pattern,
                            FluxMsgHandler cb, void *arg)
{
    return 0;
}

void flux_msghandler_remove (flux_t h, int typemask, const char *pattern)
{
}

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    if (!h->ops->reactor_fdhandler_add) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->reactor_fdhandler_add (h->impl, fd, events, cb, arg);
}

void flux_fdhandler_remove (flux_t h, int fd, short events)
{
    if (h->ops->reactor_fdhandler_remove)
        h->ops->reactor_fdhandler_remove (h->impl, fd, events);
}

int flux_reactor_start (flux_t h)
{
    if (!h->ops->reactor_start) {
        errno = ENOSYS;
        return -1;
    }
    return h->ops->reactor_start (h->impl, reactor_recvmsg, h);
}

void flux_reactor_stop (flux_t h)
{
    if (h->ops->reactor_stop)
        h->ops->reactor_stop (h->impl);
}

/**
 ** Higher level functions built on those above
 **/

int flux_event_send (flux_t h, json_object *request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    zmsg = cmb_msg_encode (tag, request);
    free (tag);
    if ((rc = flux_event_sendmsg (h, &zmsg)) < 0)
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

json_object *flux_rpc (flux_t h, json_object *request, const char *fmt, ...)
{
    char *tag = NULL;
    json_object *response = NULL;
    zmsg_t *zmsg = NULL;
    zlist_t *nomatch = NULL;
    va_list ap;
    json_object *empty = NULL;

    if (!(nomatch = zlist_new ()))
        oom ();

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
    do {
        if (!(zmsg = flux_response_recvmsg (h, false)))
            goto done;
        if (!cmb_msg_match (zmsg, tag)) {
            if (zlist_append (nomatch, zmsg) < 0)
                oom ();
            zmsg = NULL;
        }
    } while (zmsg == NULL);
    if (cmb_msg_decode (zmsg, NULL, &response) < 0)
        goto done;
    if (!response) {
        json_object_put (response);
        response = NULL;
        goto done;
    }
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
    if (nomatch) {
        while ((zmsg = zlist_pop (nomatch))) {
            if (flux_response_putmsg (h, &zmsg) < 0)
                zmsg_destroy (&zmsg);
        }
        zlist_destroy (&nomatch);
    }
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

char *flux_ping (flux_t h, const char *name, const char *pad, int seq)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rseq;
    const char *route, *rpad;
    char *ret = NULL;

    if (pad)
        util_json_object_add_string (request, "pad", pad);
    util_json_object_add_int (request, "seq", seq);
    if (!(response = flux_rpc (h, request, "%s.ping", name)))
        goto done;

    if (util_json_object_get_int (response, "seq", &rseq) < 0
            || util_json_object_get_string (response, "route", &route) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (seq != rseq) {
        msg ("%s: seq not echoed back", __FUNCTION__);
        errno = EPROTO;
        goto done;
    }
    if (pad) {
        if (util_json_object_get_string (response, "pad", &rpad) < 0
                                || !rpad || strlen (pad) != strlen (rpad)) {
            msg ("%s: pad not echoed back", __FUNCTION__);
            errno = EPROTO;
            goto done;
        }
    }
    ret = strdup (route);
done:
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
