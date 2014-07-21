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

#if ZMQ_VERSION_MAJOR < 3
#error requires zeromq version 3 or greater
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

zmsg_t *zmsg_recv_fd_typemask (int fd, int *typemask, bool nonblock)
{
    uint8_t *buf = NULL;
    uint32_t len, mask;
    int n;
    zmsg_t *msg;

    if (typemask) {
        n = _read_all (fd, (uint8_t *)&mask, sizeof (mask), nonblock);
        if (n < 0)
            goto error;
        if (n == 0)
            goto eproto;
        mask = ntohl (mask);
    }

    n = _read_all (fd, (uint8_t *)&len, sizeof (len), 0);
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
    if (typemask)
        *typemask = mask;
    return msg;
eproto:
    errno = EPROTO;
error:
    if (buf)
        free (buf);
    return NULL;
}

zmsg_t *zmsg_recv_fd (int fd, bool nonblock)
{
    return zmsg_recv_fd_typemask (fd, NULL, nonblock);
}

int zmsg_send_fd_typemask (int fd, int typemask, zmsg_t **msg)
{
    uint8_t *buf = NULL;
    int n, len;
    uint32_t nlen, mask;

    len = zmsg_encode (*msg, &buf);
    if (len < 0) {
        errno = EPROTO;
        goto error;
    }

    if (typemask != -1) {
        mask = htonl ((uint32_t)typemask);
        n = _write_all (fd, (uint8_t *)&mask, sizeof (mask));
        if (n < 0)
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

int zmsg_send_fd (int fd, zmsg_t **msg)
{
    return zmsg_send_fd_typemask (fd, -1, msg);
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
    zframe_t *zf, *prev = NULL;

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
        if (json)
            util_json_decode (op, (char *)zframe_data (json),
                                          zframe_size (json));
        else
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
    unsigned int zlen;
    char *zbuf;
    zframe_t *zf;

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (zmsg_addmem (zmsg, tag, strlen (tag)) < 0)
        err_exit ("zmsg_addmem");
    if (o) {
        util_json_encode (o, &zbuf, &zlen);
        if (!(zf = zframe_new (zbuf, zlen)))
            oom ();
        free (zbuf);
        if (zmsg_add (zmsg, zf) < 0)
            oom ();
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
    const char *tag_noaddr;
    bool match;

    if ((tag_noaddr = strchr (tag, '!')))
        tag_noaddr++;
    else
        tag_noaddr = tag;
    match = !strcmp (ztag, tag_noaddr);
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
    zframe_t *zf = _json_frame (zmsg);
    char *zbuf;
    unsigned int zlen;

    if (!zf) {
        msg ("%s: no JSON frame", __FUNCTION__);
        errno = EPROTO;
        return -1;
    }
    zmsg_remove (zmsg, zf);
    zframe_destroy (&zf);
    util_json_encode (o, &zbuf, &zlen);
    if (!(zf = zframe_new (zbuf, zlen)))
        oom ();
    free (zbuf);
    if (zmsg_add (zmsg, zf) < 0)
        oom ();
    return 0;
}

int cmb_msg_replace_json_errnum (zmsg_t *zmsg, int errnum)
{
    json_object *no, *o = NULL;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_int (errnum)))
        goto nomem;
    json_object_object_add (o, "errnum", no);
    if (cmb_msg_replace_json (zmsg, o) < 0)
        goto error;
    json_object_put (o);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

void zmsg_dump_compact (zmsg_t *self, const char *prefix)
{
    int hops;
    zframe_t *zf;

    fprintf (stderr, "--------------------------------------\n");
    if (!self) {
        fprintf (stderr, "NULL");
        return;
    }
    hops = zmsg_hopcount (self);
    if (hops > 0) {
        char *rte = zmsg_route_str (self, 0);
        fprintf (stderr, "%s[%3.3d] |%s|\n", prefix ? prefix : "", hops, rte);

        zf = zmsg_first (self);
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (self);
        if (zf)
            zf = zmsg_next (self); // skip empty delimiter frame
        free (rte);
    } else {
        zf = zmsg_first (self);
    }
    while (zf) {
        zframe_print (zf, prefix ? prefix : "");
        zf = zmsg_next (self);
    }
}

char *zmsg_route_str (zmsg_t *self, int skiphops)
{
    int len = 1, hops = zmsg_hopcount (self) - skiphops;
    zframe_t *zf = zmsg_first (self);
    char *buf;
    zlist_t *ids;
    char *s;

    if (!(ids = zlist_new ()))
        oom ();
    while (hops-- > 0) {
        if (!(s = zframe_strdup (zf)))
            oom ();
        if (strlen (s) == 32) /* abbreviate long uuids */
            s[5] = '\0';
        if (zlist_push (ids, s) < 0)
            oom ();
        len += strlen (s) + 1;
        zf = zmsg_next (self);
    }
    buf = xzmalloc (len);
    while ((s = zlist_pop (ids))) {
        int l = strlen (buf);
        snprintf (buf + l, len - l, "%s%s", l > 0 ? "!" : "", s);
        free (s);
    }
    zlist_destroy (&ids);
    return buf;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

