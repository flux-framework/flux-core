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

static void *getimpl (flux_t h)
{
    return flux_aux_get (h, "handle_impl");
}

flux_t flux_handle_create (void *impl, FluxFreeFn *destroy, int flags)
{
    flux_t h = xzmalloc (sizeof (*h));

    h->flags = flags;
    if (!(h->aux = zhash_new()))
        oom ();
    flux_aux_set (h, "handle_impl", impl, destroy);

    return h;
}

void flux_handle_destroy (flux_t *hp)
{
    flux_t h;

    if (hp && (h = *hp)) {
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

void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn *destroy)
{
    zhash_update (h->aux, name, aux);
    zhash_freefn (h->aux, name, destroy);
}

int flux_request_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->request_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->request_sendmsg (getimpl (h), zmsg);
}

zmsg_t *flux_request_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->request_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->request_recvmsg (getimpl (h), nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg);

    return zmsg;
}

int flux_response_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->response_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->response_sendmsg (getimpl (h), zmsg);
}

zmsg_t *flux_response_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->response_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->response_recvmsg (getimpl (h), nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg);

    return zmsg;
}

int flux_response_putmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->response_putmsg) {
        errno = ENOSYS;
        return -1;
    }

    return h->response_putmsg (getimpl (h), zmsg);
}

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->event_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->event_sendmsg (getimpl (h), zmsg);
}

zmsg_t *flux_event_recvmsg (flux_t h, bool nonblock)
{
    zmsg_t *zmsg;

    if (!h->event_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    zmsg = h->event_recvmsg (getimpl (h), nonblock);
    if (zmsg && h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (zmsg);

    return zmsg;
}

int flux_event_subscribe (flux_t h, const char *topic)
{
    if (!h->event_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->event_subscribe (getimpl (h), topic);
}

int flux_event_unsubscribe (flux_t h, const char *topic)
{
    if (!h->event_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->event_unsubscribe (getimpl (h), topic);
}

zmsg_t *flux_snoop_recvmsg (flux_t h, bool nonblock)
{
    if (!h->snoop_recvmsg) {
        errno = ENOSYS;
        return NULL;
    }
    return h->snoop_recvmsg (getimpl (h), nonblock);
}

int flux_snoop_subscribe (flux_t h, const char *topic)
{
    if (!h->snoop_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->snoop_subscribe (getimpl (h), topic);
}

int flux_snoop_unsubscribe (flux_t h, const char *topic)
{
    if (!h->snoop_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->snoop_unsubscribe (getimpl (h), topic);
}

int flux_rank (flux_t h)
{
    if (!h->rank) {
        errno = ENOSYS;
        return -1;
    }

    return h->rank (getimpl (h));
}

int flux_size (flux_t h)
{
    if (!h->size) {
        errno = ENOSYS;
        return -1;
    }

    return h->size (getimpl (h));
}

bool flux_treeroot (flux_t h)
{
    if (!h->treeroot)
        return false;
    return h->treeroot (getimpl (h));
}

int flux_timeout_set (flux_t h, unsigned long msec)
{
    if (!h->timeout_set) {
        errno = ENOSYS;
        return -1;
    }
    return h->timeout_set (getimpl (h), msec);
}

int flux_timeout_clear (flux_t h)
{
    if (!h->timeout_clear) {
        errno = ENOSYS;
        return -1;
    }
    return h->timeout_clear (getimpl (h));
}

bool flux_timeout_isset (flux_t h)
{
    if (!h->timeout_isset)
        return false;
    return h->timeout_isset (getimpl (h));
}

zloop_t *flux_get_zloop (flux_t h)
{
    if (!h->get_zloop) {
        errno = ENOSYS;
        return NULL;
    }
    return h->get_zloop (getimpl (h));
}

zctx_t *flux_get_zctx (flux_t h)
{
    if (!h->get_zctx) {
        errno = ENOSYS;
        return NULL;
    }
    return h->get_zctx (getimpl (h));
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
