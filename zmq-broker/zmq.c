/* zmq.c - wrapper functions for zmq prototyping */

#define _GNU_SOURCE
#include <stdio.h>
#include <zmq.h>
#include <czmq.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <assert.h>

#include "zmq.h"
#include "util.h"
#include "log.h"
#include "cmb.h"

#ifndef ZMQ_DONTWAIT
#   define ZMQ_DONTWAIT   ZMQ_NOBLOCK
#endif
#ifndef ZMQ_RCVHWM
#   define ZMQ_RCVHWM     ZMQ_HWM
#endif
#ifndef ZMQ_SNDHWM
#   define ZMQ_SNDHWM     ZMQ_HWM
#endif
#if ZMQ_VERSION_MAJOR == 2
#   define more_t int64_t
#   define zmq_ctx_destroy(context) zmq_term(context)
#   define zmq_msg_send(msg,sock,opt) zmq_send (sock, msg, opt)
#   define zmq_msg_recv(msg,sock,opt) zmq_recv (sock, msg, opt)
#   define ZMQ_POLL_MSEC    1000        //  zmq_poll is usec
#elif ZMQ_VERSION_MAJOR == 3
#   define more_t int
#   define ZMQ_POLL_MSEC    1           //  zmq_poll is msec
#endif

/**
 ** zmq wrappers
 **/

int zpoll (zmq_pollitem_t *items, int nitems, long timeout)
{
    int rc;

    if ((rc = zmq_poll (items, nitems, timeout * ZMQ_POLL_MSEC)) < 0)
        err_exit ("zmq_poll");
    return rc;
}

void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm)
{
    *sp = zsocket_new (zctx, type);
    zsocket_set_hwm (*sp, hwm);
    if (zsocket_connect (*sp, "%s", uri) < 0)
        err_exit ("zsocket_connect: %s", uri);
}

void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm)
{
    *sp = zsocket_new (zctx, type);
    zsocket_set_hwm (*sp, hwm);
    if (zsocket_bind (*sp, "%s", uri) < 0)
        err_exit ("zsocket_bind: %s", uri);
}

zmsg_t *zmsg_recv_fd (int fd, int flags)
{
    char *buf;
    int n;
    zmsg_t *msg;

    buf = xzmalloc (CMB_API_BUFSIZE);
    n = recv (fd, buf, CMB_API_BUFSIZE, flags);
    if (n < 0)
        goto error;
    if (n == 0) {
        errno = EPROTO;
        goto error;
    }
    msg = zmsg_decode ((byte *)buf, n);
    free (buf);
    return msg;
error:
    free (buf);
    return NULL;
}

int zmsg_send_fd (int fd, zmsg_t **msg)
{
    char *buf = NULL;
    int len;

    len = zmsg_encode (*msg, (byte **)&buf);
    if (len < 0) {
        errno = EPROTO;
        goto error;
    }
    if (send (fd, buf, len, 0) < len)
        goto error;
    free (buf);
    zmsg_destroy (msg);
    return 0;
error:
    if (buf)
        free (buf);
    return -1;
}


/**
 ** cmb messages
 **/


int cmb_msg_decode (zmsg_t *msg, char **tagp, json_object **op,
                    void **datap, int *lenp)
{
    zframe_t *tag = zmsg_first (msg);
    zframe_t *json = zmsg_next (msg);
    zframe_t *data = zmsg_next (msg);

    if (!tag)
        goto eproto;
    if (tagp)
        *tagp = zframe_strdup (tag);
    if (op) {
        char *tmp = json ? zframe_strdup (json) : NULL;

        if (tmp) {
            *op = json_tokener_parse (tmp);
            free (tmp);
        } else
            *op = NULL;
    }
    if (datap && lenp) {
        if (data) {
            *lenp = zframe_size (data);
            *datap = xzmalloc (zframe_size (data));
            memcpy (*datap, zframe_data (data), zframe_size (data));
        } else {
            *lenp = 0;
            *datap = NULL;
        }
    }
    return 0;
eproto:
    errno = EPROTO;
    return -1;
}

int cmb_msg_recv (void *socket, char **tagp, json_object **op,
                    void **datap, int *lenp, int flags)
{
    zmsg_t *msg = NULL;

    if ((flags & ZMQ_DONTWAIT) && !zsocket_poll (socket, 0)) {
        errno = EAGAIN; 
        return -1;
    }
    if (!(msg = zmsg_recv (socket)))
        goto error;
    if (cmb_msg_decode (msg, tagp, op, datap, lenp) < 0)
        goto error;
    zmsg_destroy (&msg);
    return 0;
error:
    if (msg)
        zmsg_destroy (&msg);
    return -1;
}

int cmb_msg_recv_fd (int fd, char **tagp, json_object **op,
                     void **datap, int *lenp, int flags)
{
    zmsg_t *msg;

    msg = zmsg_recv_fd (fd, flags);
    if (!msg)
        goto error;
    if (cmb_msg_decode (msg, tagp, op, datap, lenp) < 0)
        goto error;
    zmsg_destroy (&msg);
    return 0;

error:
    if (msg)
        zmsg_destroy (&msg);
    return -1;
}

zmsg_t *cmb_msg_encode (char *tag, json_object *o, void *data, int len)
{
    zmsg_t *msg = NULL;

    if (!(msg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (zmsg_addstr (msg, "%s", tag) < 0)
        err_exit ("zmsg_addstr");
    if (o) {
        if (zmsg_addstr (msg, "%s", json_object_to_json_string (o)) < 0)
            err_exit ("zmsg_addstr");
    }
    if (len > 0 && data != NULL) {
        assert (o != NULL);
        if (zmsg_addmem (msg, data, len) < 0)
            err_exit ("zmsg_addmem");
    }
    return msg;
}

void cmb_msg_send_long (void *sock, json_object *o, void *data, int len,
                        const char *fmt, ...)
{
    va_list ap;
    zmsg_t *msg;
    char *tag;
    int n;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");
   
    msg = cmb_msg_encode (tag, o, data, len);
    free (tag);
    if (zmsg_send (&msg, sock) < 0)
        err_exit ("zmsg_send");
}

void cmb_msg_send (void *sock, const char *fmt, ...)
{
    va_list ap;
    zmsg_t *msg;
    char *tag;
    int n;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");
   
    msg = cmb_msg_encode (tag, NULL, NULL, 0);
    free (tag);
    if (zmsg_send (&msg, sock) < 0)
        err_exit ("zmsg_send");
}

int cmb_msg_send_long_fd (int fd, json_object *o, void *data, int len,
                          const char *fmt, ...)
{
    va_list ap;
    zmsg_t *msg = NULL;
    char *tag = NULL;
    int n;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");

    msg = cmb_msg_encode (tag, o, data, len);
    if (zmsg_send_fd (fd, &msg) < 0) /* destroys msg on succes */
        goto error;
    free (tag);
    return 0;
error:
    if (msg)
        zmsg_destroy (&msg);
    if (tag)
        free (tag);
    return -1; 
}

int cmb_msg_send_fd (int fd, const char *fmt, ...)
{
    va_list ap;
    zmsg_t *msg = NULL;
    char *tag = NULL;
    int n;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");
   
    msg = cmb_msg_encode (tag, NULL, NULL, 0);
    if (zmsg_send_fd (fd, &msg) < 0) /* destroys msg on succes */
        goto error;
    free (tag);
    return 0;
error:
    if (msg)
        zmsg_destroy (&msg);
    if (tag)
        free (tag);
    return -1;
}


bool cmb_msg_match (zmsg_t *msg, const char *tag, bool exact)
{
    bool match;
    zframe_t *frame = zmsg_first (msg);
    char *s;

    if (!frame)
        msg_exit ("cmb_msg_match: nonexistent message part");
    if (!(s = zframe_strdup (frame)))
        oom ();
    if (exact)
        match = (strcmp (tag, s) == 0);
    else
        match = (strncmp (tag, s, strlen (tag)) == 0);
    free (s);
    return match;
}

int cmb_msg_datacpy (zmsg_t *msg, char *buf, int len)
{
    zframe_t *data;

    if (zmsg_size (msg) != 3)
        return -1;
    data = zmsg_last (msg);
    if (!data)
        return -1;
    if (zframe_size (data) > len) {
        fprintf (stderr, "cmb_msg_getdata: received message is too big\n");
        return -1;
    }
    memcpy (buf, zframe_data (data), zframe_size (data));
    return zframe_size (data);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

