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
#include <json/json.h>

#include "zmq.h"

static void _oom (void)
{
    fprintf (stderr, "out of memory\n");
    exit (1);
}

static void *_zmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        _oom ();
    memset (new, 0, size);
    return new;
}

void _zmq_close (void *socket)
{
    if (zmq_close (socket) < 0) {
        fprintf (stderr, "zmq_close: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_term (void *ctx)
{
    if (zmq_term (ctx) < 0) {
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

void _zmq_mcast_loop (void *sock, bool enable)
{
    uint64_t val = enable ? 1 : 0;

    if (zmq_setsockopt (sock, ZMQ_MCAST_LOOP, &val, sizeof (val)) < 0) {
        fprintf (stderr, "zmq_setsockopt ZMQ_MCAST_LOOP: %s\n",
                zmq_strerror (errno));
        exit (1);
    }
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

void _zmq_send (void *socket, zmq_msg_t *msg, int flags)
{
    if (zmq_send (socket, msg, flags) < 0) {
        fprintf (stderr, "zmq_send: %s\n", zmq_strerror (errno));
        exit (1);
    }
}

void _zmq_recv (void *socket, zmq_msg_t *msg, int flags)
{
    if (zmq_recv (socket, msg, flags) < 0) {
        fprintf (stderr, "zmq_recv: %s\n", zmq_strerror (errno));
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

void _zmq_2part_init_buf (zmq_2part_t *msg, char *buf, int len,
                          const char *fmt, ...)
{
    va_list ap;
    int n;
    char *tag;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0) {
        fprintf (stderr, "vasprintf: %s\n", strerror (errno));
        exit (1);
    }
    _zmq_msg_init_size (&msg->tag, strlen (tag));
    memcpy (zmq_msg_data (&msg->tag), tag, strlen (tag));
    _zmq_msg_init_size (&msg->body, len);
    memcpy (zmq_msg_data (&msg->body), buf, len);;
    free (tag);
}


void _zmq_2part_init_empty (zmq_2part_t *msg, const char *fmt, ...)
{
    va_list ap;
    int n;
    char *tag;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0) {
        fprintf (stderr, "vasprintf: %s\n", strerror (errno));
        exit (1);
    }
    _zmq_msg_init_size (&msg->tag, strlen (tag));
    memcpy (zmq_msg_data (&msg->tag), tag, strlen (tag));
    _zmq_msg_init_size (&msg->body, 0);
    free (tag);
}

void _zmq_2part_init_json (zmq_2part_t *msg, json_object *o,
                           const char *fmt, ...)
{
    va_list ap;
    int n;
    char *tag;
    const char *body;
    int taglen, bodylen;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0) {
        fprintf (stderr, "vasprintf: %s\n", strerror (errno));
        exit (1);
    }
    taglen = strlen (tag);
    _zmq_msg_init_size (&msg->tag, taglen);
    memcpy (zmq_msg_data (&msg->tag), tag, taglen);

    body = json_object_to_json_string (o);
    bodylen = strlen (body);
    _zmq_msg_init_size (&msg->body, bodylen);
    memcpy (zmq_msg_data (&msg->body), body, bodylen);

    free (tag);    
    json_object_put (o);
}

void _zmq_2part_init (zmq_2part_t *msg)
{
    _zmq_msg_init (&msg->tag);
    _zmq_msg_init (&msg->body);
}

void _zmq_2part_close (zmq_2part_t *msg)
{
    _zmq_msg_close (&msg->tag);
    _zmq_msg_close (&msg->body);
}

bool _zmq_rcvmore (void *socket)
{
    int64_t more;
    size_t more_size = sizeof (more);

    _zmq_getsockopt (socket, ZMQ_RCVMORE, &more, &more_size);

    return (bool)more;
}

void _zmq_2part_recv (void *socket, zmq_2part_t *msg, int flags)
{
    _zmq_recv (socket, &msg->tag, flags);
    if (!_zmq_rcvmore (socket)) {
        fprintf (stderr, "_zmq_2part_recv: only one part recieved\n");
        exit (1);
    }
    _zmq_recv (socket, &msg->body, flags);
    if (_zmq_rcvmore (socket)) {
        fprintf (stderr, "_zmq_2part_recv: more than two parts received\n");
        exit (1);
    }
}

static char *_msg2str (zmq_msg_t *msg)
{
    int len = zmq_msg_size (msg);
    char *s = _zmalloc (len + 1);

    memcpy (s, zmq_msg_data (msg), len);
    s[len] = '\0';

    return s;
}

int _zmq_2part_recv_json (void *socket, char **tagp, json_object **op)
{
    zmq_2part_t msg;
    json_object *o = NULL;
    char *tag = NULL;
    char *body = NULL;

    _zmq_2part_init (&msg);
    _zmq_2part_recv (socket, &msg, 0);
    tag = _msg2str (&msg.tag);
    if (zmq_msg_size (&msg.body) > 0) {
        body = _msg2str (&msg.body);
        o = json_tokener_parse (body);
        if (!o)
            goto error;
    }
    *tagp = tag;
    *op = o;
    if (body)
        free (body);
    _zmq_2part_close (&msg);
    return 0;
error:
    _zmq_2part_close (&msg);
    if (o)
        json_object_put (o);
    if (body)
        free (body);
    if (tag)
        free (tag);
    return -1;
}

void _zmq_2part_send (void *socket, zmq_2part_t *msg, int flags)
{
    _zmq_send (socket, &msg->tag, flags | ZMQ_SNDMORE);
    _zmq_send (socket, &msg->body, flags);
}

void _zmq_msg_dup (zmq_msg_t *dest, zmq_msg_t *src)
{
    _zmq_msg_init_size (dest, zmq_msg_size (src));
    memcpy (zmq_msg_data (dest), zmq_msg_data (src), zmq_msg_size (dest));
}

void _zmq_2part_dup (zmq_2part_t *dest, zmq_2part_t *src)
{
    _zmq_msg_dup (&dest->tag, &src->tag);
    _zmq_msg_dup (&dest->body, &src->body);
}

bool _zmq_2part_match (zmq_2part_t *msg, char *tag)
{
    int n = zmq_msg_size (&msg->tag);
    int t = strlen (tag);

    return (t <= n && !memcmp (zmq_msg_data (&msg->tag), tag, t));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

