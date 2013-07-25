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

#include "zmsg.h"
#include "util.h"
#include "log.h"

#if ZMQ_VERSION_MAJOR != 3
#error requires zeromq version 3
#endif

/**
 ** zmq wrappers
 **/

void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id)
{
    *sp = zsocket_new (zctx, type);
    if (hwm != -1)
        zsocket_set_hwm (*sp, hwm);
    if (id != NULL)
        zsocket_set_identity (*sp, id);
    if (zsocket_connect (*sp, "%s", uri) < 0)
        err_exit ("zsocket_connect: %s", uri);
}

void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm)
{
    *sp = zsocket_new (zctx, type);
    if (hwm != -1)
        zsocket_set_hwm (*sp, hwm);
    if (zsocket_bind (*sp, "%s", uri) < 0)
        err_exit ("zsocket_bind: %s", uri);
}

void *zmonitor (zctx_t *ctx, void *s, const char *uri, int flags)
{
    void *mon;

    if (zmq_socket_monitor (s, uri, flags) < 0)
        err_exit ("zmq_socket_monitor");
    if (!(mon = zsocket_new (ctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    if (zsocket_connect (mon, "%s", uri) < 0)
        err_exit ("zsocket_connect %s", uri);
    return mon;
}

void zmonitor_recv (void *s, zmq_event_t *event, bool *vp)
{
    zmsg_t *zmsg;
    zframe_t *zf;

    if (!(zmsg = zmsg_recv (s)))
        err_exit ("%s: zmq_recvmsg", __FUNCTION__);

    if (zmsg_size (zmsg) == 1) {  /* broken 3.2.2 - addrs invalid */
        if (!(zf = zmsg_pop (zmsg)))
            msg_exit ("%s: zmsg_pop", __FUNCTION__);
        assert (zframe_size (zf) == sizeof (zmq_event_t));
        memcpy (event, zframe_data (zf), sizeof (zmq_event_t));
        zframe_destroy (&zf);
        *vp = false;
    } else {
        err_exit ("%s: unexpected event message content", __FUNCTION__);
        /* FIXME: later versions (3.3?) fix the invalid addrs but 
         * also change zmq_event_t member names, so this block needs
         * some conditional-compilation-fu.
         */
    }
    zmsg_destroy (&zmsg);
}

void zmonitor_dump (char *name, void *s)
{
    zmq_event_t event;
    bool valid_addr;

    zmonitor_recv (s, &event, &valid_addr);
    switch (event.event) {
        case ZMQ_EVENT_CONNECTED:
            msg ("%s: ZMQ_EVENT_CONNECTED from %s on fd %d", name,
                 valid_addr ? event.data.connected.addr : "<unknown>",
                 event.data.connected.fd);
            break;
        case ZMQ_EVENT_CONNECT_DELAYED:
            msg ("%s: ZMQ_EVENT_CONNECT_DELAYED", name);
            break;
        case ZMQ_EVENT_CONNECT_RETRIED:
            msg ("%s: ZMQ_EVENT_CONNECT_RETRIED", name);
            break;
        case ZMQ_EVENT_LISTENING:
            msg ("%s: ZMQ_EVENT_LISTENING", name);
            break;
        case ZMQ_EVENT_BIND_FAILED:
            msg ("%s: ZMQ_EVENT_BIND_FAILED", name);
            break;
        case ZMQ_EVENT_ACCEPTED:
            msg ("%s: ZMQ_EVENT_ACCEPTED from %s on fd %d", name,
                 valid_addr ? event.data.accepted.addr : "<unknown>",
                 event.data.accepted.fd);
            break;
        case ZMQ_EVENT_ACCEPT_FAILED:
            msg ("%s: ZMQ_EVENT_ACCEPT_FAILED", name);
            break;
        case ZMQ_EVENT_CLOSED:
            msg ("%s: ZMQ_EVENT_CLOSED from %s on fd %d", name,
                 valid_addr ? event.data.closed.addr : "<unknown>",
                 event.data.closed.fd);
            break;
        case ZMQ_EVENT_CLOSE_FAILED:
            msg ("%s: ZMQ_EVENT_CLOSE_FAILED", name);
            break;
        case ZMQ_EVENT_DISCONNECTED:
            msg ("%s: ZMQ_EVENT_DISCONNECTED from %s on fd %d", name,
                 valid_addr ? event.data.disconnected.addr : "<unknown>", 
                 event.data.disconnected.fd);
            break;
        default:
            msg ("%s: unknown event (%d)", name, event.event);
            break;
    }
}

static int _nonblock (int fd, bool nonblock)
{
    int flags;

    if ((flags = fcntl (fd, F_GETFL)) < 0)
        return -1;
    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl (fd, F_SETFL, flags) < 0)
        return -1;
    return 0;
}

static int _read_all (int fd, uint8_t *buf, size_t len, bool nonblock)
{
    int n, count = 0;

    do {
        if (nonblock && _nonblock (fd, true) < 0)
            return -1;
        n = read (fd, buf + count, len - count);
        if (nonblock && _nonblock (fd, false) < 0)
            return -1;
        nonblock = false;
        if (n <= 0)
            return n;
        count += n;
    } while (count < len);

    return count;
}

static int _write_all (int fd, uint8_t *buf, size_t len)
{
    int n, count = 0;

    do {
        n = write (fd, buf + count, len - count);
        if (n < 0)
            return n;
        count += n;
    } while (count < len);

    return count;
}

zmsg_t *zmsg_recv_fd (int fd, bool nonblock)
{
    uint8_t *buf = NULL;
    uint32_t len;
    int n;
    zmsg_t *msg;

    n = _read_all (fd, (uint8_t *)&len, sizeof (len), nonblock);
    if (n < 0)
        goto error;
    if (n == 0)
        goto eproto;
    len = ntohl (len);

    buf = xzmalloc (len);
    n = _read_all (fd, buf, len, 0);
    if (n < 0)
        goto error;
    if (n == 0)
        goto eproto;

    msg = zmsg_decode ((byte *)buf, len);
    free (buf);
    return msg;
eproto:
    errno = EPROTO;
error:
    if (buf)
        free (buf);
    return NULL;
}

int zmsg_send_fd (int fd, zmsg_t **msg)
{
    uint8_t *buf = NULL;
    int n, len;
    uint32_t nlen;

    len = zmsg_encode (*msg, &buf);
    if (len < 0) {
        errno = EPROTO;
        goto error;
    }
    nlen = htonl ((uint32_t)len);

    n = _write_all (fd, (uint8_t *)&nlen, sizeof (nlen));
    if (n < 0)
        goto error;

    n = _write_all (fd, buf, len);
    if (n < 0)
        goto error;

    free (buf);
    zmsg_destroy (msg);
    return 0;
error:
    if (buf)
        free (buf);
    return -1;
}

void zmsg_send_unrouter (zmsg_t **zmsg, void *sock, char *addr, const char *gw)
{
    zframe_t *zf;

    if (!(zf = zframe_new (addr, strlen (addr))))
        oom ();
    if (zmsg_push (*zmsg, zf) < 0) /* push local addr for reply path */
        oom ();
    if (!(zf = zframe_new (gw, strlen (gw))))
        oom ();
    if (zmsg_push (*zmsg, zf) < 0) /* push gw addr for routing socket */
        oom ();
    zmsg_send (zmsg, sock);
}

zmsg_t *zmsg_recv_unrouter (void *sock)
{
    zmsg_t *zmsg;
    zframe_t *zf;

    zmsg = zmsg_recv (sock);
    if (zmsg) {     
        zf = zmsg_pop (zmsg);       /* pop-n-toss */
        if (zf)
            zframe_destroy (&zf);
        zf = zmsg_pop (zmsg);       /* pop-n-toss */
        if (zf)
            zframe_destroy (&zf);
    }
    return zmsg;
}

void zmsg_cc (zmsg_t *zmsg, void *sock)
{
    if (zmsg) {
        zmsg_t *cpy = zmsg_dup (zmsg);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, sock) < 0)
            err_exit ("zmsg_send");
    }
}

int zmsg_hopcount (zmsg_t *zmsg)
{
    int count = 0;
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        count++;
        zf = zmsg_next (zmsg); /* skip non-empty */
    }
    if (!zf)
        count = 0;
    return count;
}

static zframe_t *_tag_frame (zmsg_t *zmsg)
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

static zframe_t *_json_frame (zmsg_t *zmsg)
{
    zframe_t *zf = _tag_frame (zmsg);

    return (zf ? zmsg_next (zmsg) : NULL);
}

static zframe_t *_sender_frame (zmsg_t *zmsg)
{
    zframe_t *zf, *prev;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        prev = zf;
        zf = zmsg_next (zmsg);
    }
    return (zf ? prev : NULL);
}

int cmb_msg_decode (zmsg_t *zmsg, char **tagp, json_object **op)
{
    zframe_t *tag = _tag_frame (zmsg);
    zframe_t *json = zmsg_next (zmsg);

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
    return 0;
eproto:
    errno = EPROTO;
    return -1;
}

zmsg_t *cmb_msg_encode (char *tag, json_object *o)
{
    zmsg_t *zmsg = NULL;

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (zmsg_addmem (zmsg, tag, strlen (tag)) < 0)
        err_exit ("zmsg_addmem");
    if (o) {
        const char *s = json_object_to_json_string (o);
        if (zmsg_addmem (zmsg, s, strlen (s)) < 0)
            err_exit ("zmsg_addmem");
    }
    return zmsg;
}

static char *_ztag_noaddr (zmsg_t *zmsg)
{
    zframe_t *zf = _tag_frame (zmsg);
    char *p, *ztag;

    if (!zf)
        msg_exit ("_ztag_noaddr: no tag in message");
    if (!(ztag = zframe_strdup (zf)))
        oom ();
    if ((p = strchr (ztag, '!')))
        memmove (ztag, p + 1, strlen (ztag) - (p - ztag)); /* N.B. include \0 */
    return ztag;
}

bool cmb_msg_match (zmsg_t *zmsg, const char *tag)
{
    char *ztag = _ztag_noaddr (zmsg);
    bool match = !strcmp (ztag, tag);

    free (ztag);
    return match;
}

bool cmb_msg_match_substr (zmsg_t *zmsg, const char *tag, char **restp)
{
    char *ztag = _ztag_noaddr (zmsg);
    int taglen = strlen (tag);
    int ztaglen = strlen (ztag);

    if (ztaglen >= taglen && strncmp (tag, ztag, taglen) == 0) {
        if (restp) {
            memmove (ztag, ztag + taglen, ztaglen - taglen + 1);
            *restp = ztag;
        } else
            free (ztag);
        return true;
    }
    free (ztag);
    return false;
}

char *cmb_msg_sender (zmsg_t *zmsg)
{
    zframe_t *zf = _sender_frame (zmsg);
    if (!zf) {
        msg ("%s: empty envelope", __FUNCTION__);
        return NULL;
    }
    return zframe_strdup (zf); /* caller must free */
}

char *cmb_msg_nexthop (zmsg_t *zmsg)
{
    zframe_t *zf = zmsg_first (zmsg);
    if (!zf) {
        msg ("%s: empty envelope", __FUNCTION__);
        return NULL;
    }
    return zframe_strdup (zf); /* caller must free */
}

char *cmb_msg_tag (zmsg_t *zmsg, bool shorten)
{
    zframe_t *zf = _tag_frame (zmsg);
    char *tag;
    if (!zf) {
        msg ("%s: no tag frame", __FUNCTION__);
        return NULL;
    }
    tag = zframe_strdup (zf); /* caller must free */
    if (tag && shorten) {
        char *p = strchr (tag, '.');
        if (p)
            *p = '\0';
    }
    return tag;
}

int cmb_msg_replace_json (zmsg_t *zmsg, json_object *o)
{
    const char *json = json_object_to_json_string (o);
    zframe_t *zf = _json_frame (zmsg);

    if (!zf) {
        msg ("%s: no JSON frame", __FUNCTION__);
        errno = EPROTO;
        return -1;
    }
    zframe_reset (zf, json, strlen (json)); /* N.B. unchecked malloc inside */
    return 0;
}

int cmb_msg_replace_json_errnum (zmsg_t *zmsg, int errnum)
{
    json_object *no, *o = NULL;
    zframe_t *zf = _json_frame (zmsg);
    const char *json;

    if (!zf) {
        msg ("%s: no JSON frame", __FUNCTION__);
        errno = EPROTO;
        goto error;
    }
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_int (errnum)))
        goto nomem;
    json_object_object_add (o, "errnum", no);
    json = json_object_to_json_string (o);
    zframe_reset (zf, json, strlen (json)); /* N.B. unchecked malloc inside */
    json_object_put (o);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

/* Customized versions of zframe_print() and zmsg_dump() from czmq source.
 * We don't want to truncate message parts, and want envelop parts represented
 * more compactly (let's try all on one line).
 */
static void _zframe_print_compact (zframe_t *self, const char *prefix)
{
    assert (self);
    if (prefix)
        fprintf (stderr, "%s", prefix);
    byte *data = zframe_data (self);
    size_t size = zframe_size (self);

    int is_bin = 0;
    uint char_nbr;
    for (char_nbr = 0; char_nbr < size; char_nbr++)
        if (data [char_nbr] < 9 || data [char_nbr] > 127)
            is_bin = 1;

    //fprintf (stderr, "[%03d] ", (int) size);
    size_t max_size = is_bin? 35: 70;
    //const char *elipsis = "";
    if (size > max_size) {
        size = max_size;
        //elipsis = "...";
    }
    for (char_nbr = 0; char_nbr < size; char_nbr++) {
        if (is_bin)
            fprintf (stderr, "%02X", (unsigned char) data [char_nbr]);
        else
            fprintf (stderr, "%c", data [char_nbr]);
    }
    //fprintf (stderr, "%s\n", elipsis);
}

void zmsg_dump_compact (zmsg_t *self)
{
    int hops = zmsg_hopcount (self);
    zframe_t *zf = zmsg_first (self);

    fprintf (stderr, "--------------------------------------\n");
    if (!self || !zf) {
        fprintf (stderr, "NULL");
        return;
    }
    if (hops > 0) {
        fprintf (stderr, "[%3.3d] ", hops);
        while (zf && zframe_size (zf) > 0) {
            _zframe_print_compact (zf , "|");
            zf = zmsg_next (self);
        }
        if (zf)
            zf = zmsg_next (self); // skip empty delimiter frame
        fprintf (stderr, "|\n");
    }
    while (zf) {
        zframe_print (zf, "");
        zf = zmsg_next (self);
    }
}

char *zmsg_route_str (zmsg_t *self, int skiphops)
{
    int hops = zmsg_hopcount (self) - skiphops;
    zframe_t *zf = zmsg_first (self);
    const size_t len = 256;
    char *buf = xzmalloc (len);

    while (hops-- > 0) {
        int l = strlen (buf);
        char *s = zframe_strdup (zf);
        if (!s)
            oom ();
        snprintf (buf + l, len - l, "%s%s", l > 0 ? "!" : "", s);
        free (s);
        zf = zmsg_next (self);
    }
    return buf;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

