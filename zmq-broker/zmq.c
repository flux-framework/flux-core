/* zmq.c - wrapper functions for zmq prototyping */

#define _GNU_SOURCE
#include <stdio.h>
#include <zmq.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <assert.h>

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

#include "zmq.h"
#include "util.h"

/**
 ** zmq wrappers
 **/

void _zmq_close (void *socket)
{
    if (zmq_close (socket) < 0) {
        fprintf (stderr, "zmq_close: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_ctx_destroy (void *ctx)
{
    if (zmq_ctx_destroy (ctx) < 0) {
        fprintf (stderr, "zmq_term: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void *_zmq_init (int nthreads)
{
    void *ctx;

    if (!(ctx = zmq_init (nthreads))) {
        fprintf (stderr, "zmq_init: %s\n", zmq_strerror (errno));
        exit (1);
    }
    return ctx;
}

void *_zmq_socket (void *ctx, int type)
{
    void *sock;

    if (!(sock = zmq_socket (ctx, type))) {
        fprintf (stderr, "zmq_socket(%d): %s\n", type, zmq_strerror (errno));
        exit (1);
    }
    return sock;
}

void _zmq_bind (void *sock, const char *endpoint)
{
    if (zmq_bind (sock, endpoint) < 0) {
        fprintf (stderr, "zmq_bind %s: %s\n", endpoint, zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_connect (void *sock, const char *endpoint)
{
    if (zmq_connect (sock, endpoint) < 0) {
        fprintf (stderr, "zmq_connect %s: %s\n", endpoint, zmq_strerror(errno));
        exit (1);
    }
}

void _zmq_subscribe_all (void *sock)
{
    if (zmq_setsockopt (sock, ZMQ_SUBSCRIBE, NULL, 0) < 0) {
        fprintf (stderr, "zmq_setsockopt ZMQ_SUBSCRIBE: %s\n",
                 zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_subscribe (void *sock, char *tag)
{
    if (zmq_setsockopt (sock, ZMQ_SUBSCRIBE, tag, tag ? strlen (tag) : 0) < 0) {
        fprintf (stderr, "zmq_setsockopt ZMQ_SUBSCRIBE: %s\n",
                 zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_unsubscribe (void *sock, char *tag)
{
    if (zmq_setsockopt (sock, ZMQ_UNSUBSCRIBE, tag, strlen (tag)) < 0) {
        fprintf (stderr, "zmq_setsockopt ZMQ_UNSUBSCRIBE: %s\n",
                 zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_mcast_loop (void *sock, bool enable)
{
#ifdef ZMQ_MCAST_LOOP
    uint64_t val = enable ? 1 : 0;

    if (zmq_setsockopt (sock, ZMQ_MCAST_LOOP, &val, sizeof (val)) < 0) {
        fprintf (stderr, "zmq_setsockopt ZMQ_MCAST_LOOP: %s\n",
                zmq_strerror (errno));
        exit (1);
    }
#else
    fprintf (stderr, "_zmq_mcast_loop: warning: function not implemented\n");
#endif
}

void _zmq_msg_init_size (zmq_msg_t *msg, size_t size)
{
    if (zmq_msg_init_size (msg, size) < 0) {
        fprintf (stderr, "zmq_msg_init_size: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_msg_init (zmq_msg_t *msg)
{
    if (zmq_msg_init (msg) < 0) {
        fprintf (stderr, "zmq_msg_init: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_msg_close (zmq_msg_t *msg)
{
    if (zmq_msg_close (msg) < 0) {
        fprintf (stderr, "zmq_msg_close: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_msg_send (zmq_msg_t *msg, void *socket, int flags)
{
    if (zmq_msg_send (msg, socket, flags) < 0) {
        fprintf (stderr, "zmq_msg_send: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_msg_recv (zmq_msg_t *msg, void *socket, int flags)
{
    if (zmq_msg_recv (msg, socket, flags) < 0) {
        fprintf (stderr, "zmq_msg_recv: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_getsockopt (void *socket, int option_name, void *option_value,
                      size_t *option_len)
{
    if (zmq_getsockopt (socket, option_name, option_value, option_len) < 0) {
        fprintf (stderr, "zmq_getsockopt: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

int _zmq_poll (zmq_pollitem_t *items, int nitems, long timeout)
{
    int rc;

    if ((rc = zmq_poll (items, nitems, timeout * ZMQ_POLL_MSEC)) < 0) {
        fprintf (stderr, "zmq_poll: %s\n", zmq_strerror (errno));
        exit (1);
    }
    return rc;
}

bool _zmq_rcvmore (void *socket)
{
    more_t more;
    size_t more_size = sizeof (more);

    _zmq_getsockopt (socket, ZMQ_RCVMORE, &more, &more_size);

    return (bool)more;
}

void _zmq_msg_dup (zmq_msg_t *dest, zmq_msg_t *src)
{
    _zmq_msg_init_size (dest, zmq_msg_size (src));
    memcpy (zmq_msg_data (dest), zmq_msg_data (src), zmq_msg_size (dest));
}

static char *_msg2str (zmq_msg_t *msg)
{
    int len = zmq_msg_size (msg);
    char *s = xzmalloc (len + 1);

    memcpy (s, zmq_msg_data (msg), len);
    s[len] = '\0';

    return s;
}

/**
 ** mpart messages
 **/

void _zmq_mpart_init (zmq_mpart_t *msg)
{
    int i;

    for (i = 0; i < ZMQ_MPART_MAX; i++)
        _zmq_msg_init (&msg->part[i]);
}

void _zmq_mpart_close (zmq_mpart_t *msg)
{
    int i;
    
    for (i = 0; i < ZMQ_MPART_MAX; i++)
        _zmq_msg_close (&msg->part[i]);
}

/* return 0 on success, -1 on non-fatal error */
int _zmq_mpart_recv (zmq_mpart_t *msg, void *socket, int flags)
{
    int i = 0;

    for (i = 0; i < ZMQ_MPART_MAX; i++) {
        if (i > 0 && !_zmq_rcvmore (socket)) {
            /* N.B. seen on epgm socket when sender restarted */
            fprintf (stderr, "_zmq_mpart_recv: only got %d message parts\n", i);
            goto error;
        }
        _zmq_msg_recv (&msg->part[i], socket, flags);
    }
    if (_zmq_rcvmore (socket)) {
        fprintf (stderr, "_zmq_mpart_recv: too many message parts\n");
        goto error;
    }
    return 0;
error:
    errno = EPROTO;
    return -1;
}

void _zmq_mpart_send (zmq_mpart_t *msg, void *socket, int flags)
{
    int i;

    for (i = 0; i < ZMQ_MPART_MAX; i++)
        if (i < ZMQ_MPART_MAX - 1)
            _zmq_msg_send (&msg->part[i], socket, flags | ZMQ_SNDMORE);
        else
            _zmq_msg_send (&msg->part[i], socket, flags);
}

void _zmq_mpart_dup (zmq_mpart_t *dest, zmq_mpart_t *src)
{
    int i;

    for (i = 0; i < ZMQ_MPART_MAX; i++)
        _zmq_msg_dup (&dest->part[i], &src->part[i]);
}

size_t _zmq_mpart_size (zmq_mpart_t *msg)
{
    int i;
    size_t size = 0;

    for (i = 0; i < ZMQ_MPART_MAX; i++)
        size += zmq_msg_size (&msg->part[i]);
    return size;
}

/**
 ** cmb messages
 **/

int cmb_msg_recv (void *socket, char **tagp, json_object **op,
                    void **datap, int *lenp)
{
    zmq_mpart_t msg;
    char *tag = NULL;
    json_object *o = NULL;
    void *data = NULL;
    int len = 0;

    _zmq_mpart_init (&msg);
    if (_zmq_mpart_recv (&msg, socket, 0) < 0)
        goto eproto;

    /* tag */
    if (tagp) {
        tag = _msg2str (&msg.part[0]);
    } 

    /* json */
    if (op && zmq_msg_size (&msg.part[1]) > 0) {
        char *json = _msg2str (&msg.part[1]);

        o = json_tokener_parse (json);
        free (json);
        if (!o)
            goto eproto;
    }

    /* data */
    if (datap && lenp && zmq_msg_size (&msg.part[2]) > 0) {
        len = zmq_msg_size (&msg.part[2]);
        data = xzmalloc (len);
    }

    _zmq_mpart_close (&msg);

    if (tagp)
        *tagp = tag;
    if (op)
        *op = o;
    if (datap && lenp) {
        *datap = data;
        *lenp = len;
    }
    return 0;
eproto:
    errno = EPROTO;
    _zmq_mpart_close (&msg);
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
    if (data)
        free (data);
    return -1;
}

void cmb_msg_send (void *sock, json_object *o, void *data, int len,
                     const char *fmt, ...)
{
    va_list ap;
    zmq_mpart_t msg;

    _zmq_mpart_init (&msg);

    /* tag */
    if (true) {
        char *tag;
        int n;

        va_start (ap, fmt);
        n = vasprintf (&tag, fmt, ap);
        va_end (ap);
        if (n < 0) {
            fprintf (stderr, "vasprintf: %s\n", strerror (errno));
            exit (1);
        }
        _zmq_msg_init_size (&msg.part[0], strlen (tag));
        memcpy (zmq_msg_data (&msg.part[0]), tag, strlen (tag));
        free (tag);
    }

    /* json */
    if (o) {
        const char *json = json_object_to_json_string (o);
        int jlen = strlen (json);

        _zmq_msg_init_size (&msg.part[1], jlen);
        memcpy (zmq_msg_data (&msg.part[1]), json, jlen);
    }

    /* data */
    if (data && len > 0) {
        _zmq_msg_init_size (&msg.part[2], len);
        memcpy (zmq_msg_data (&msg.part[2]), data, len);
    }

    _zmq_mpart_send (&msg, sock, 0);
}

void cmb_msg_dump (char *s, zmq_mpart_t *msg)
{
    if (zmq_msg_size (&msg->part[0]) > 0)
        fprintf (stderr, "%s: %.*s\n", s, (int)zmq_msg_size (&msg->part[0]),
                                       (char *)zmq_msg_data (&msg->part[0]));
    if (zmq_msg_size (&msg->part[1]) > 0)
        fprintf (stderr, "    %.*s\n",    (int)zmq_msg_size (&msg->part[1]),
                                       (char *)zmq_msg_data (&msg->part[1]));
    if (zmq_msg_size (&msg->part[2]) > 0)
        fprintf (stderr, "    data[%d]\n",(int)zmq_msg_size (&msg->part[2]));
}

bool cmb_msg_match (zmq_mpart_t *msg, char *tag)
{
    bool match = false;

    if (zmq_msg_size (&msg->part[0]) > 0) {
        int n = zmq_msg_size (&msg->part[0]);
        int t = strlen (tag);

        match = (t <= n && !memcmp (zmq_msg_data (&msg->part[0]), tag, t));
    }
    return match;
}

/* convert to tag\0json\0data format for socket xfer */
int cmb_msg_tobuf (zmq_mpart_t *msg, char *buf, int len)
{
    char *p = &buf[0];

    if (_zmq_mpart_size (msg) + 2 > len) {
        errno = EPROTO;
        return -1;
    }

    /* tag */
    memcpy (p, zmq_msg_data (&msg->part[0]), zmq_msg_size (&msg->part[0]));
    p += zmq_msg_size (&msg->part[0]);
    *p++ = '\0';

    /* json */
    memcpy (p, zmq_msg_data (&msg->part[1]), zmq_msg_size (&msg->part[1]));
    p += zmq_msg_size (&msg->part[1]);
    *p++ = '\0';

    /* data */
    memcpy (p, zmq_msg_data (&msg->part[2]), zmq_msg_size (&msg->part[2]));
    p += zmq_msg_size (&msg->part[2]);

    return p - buf;
}

/* convert to tag\0json\0data format for socket xfer */
void cmb_msg_frombuf (zmq_mpart_t *msg, char *buf, int len)
{
    char *p, *q;

    /* tag */
    for (p = q = buf; q - buf < len; q++)
        if (*q == '\0')
            break;
    assert (p <= q);
    _zmq_msg_init_size (&msg->part[0], q - p);
    memcpy (zmq_msg_data (&msg->part[0]), p, q - p);

    /* json */
    for (p = q = q + 1; q - buf < len; q++)
        if (*q == '\0')
            break;
    assert (p <= q);
    _zmq_msg_init_size (&msg->part[1], q - p);
    memcpy (zmq_msg_data (&msg->part[1]), p, q - p);

    /* data */
    p = q + 1;
    q = buf + len;
    if (p < q) {
        _zmq_msg_init_size (&msg->part[2], q - p);
        memcpy (zmq_msg_data (&msg->part[2]), p, q - p);
    }
}

int cmb_msg_datacpy (zmq_mpart_t *msg, char *buf, int len)
{
    int n = zmq_msg_size (&msg->part[2]);

    if (n > len) {
        fprintf (stderr, "cmb_msg_getdata: received message is too big\n");
        return -1;
    }
    memcpy (buf, zmq_msg_data (&msg->part[2]), n);
    return n;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

