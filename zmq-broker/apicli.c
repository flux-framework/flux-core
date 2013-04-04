/* apicli.c - implement the public functions in cmb.h */

/* we talk to cmbd via a UNIX domain socket */

/* FIXME: wire protocol used on socket (tag\0json) is lame */

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
#include <uuid/uuid.h>
#include <json/json.h>

#include "cmb.h"
#include "cmbd.h"

struct cmb_struct {
    int fd;
    char uuid[64];
    char buf[CMB_API_BUFSIZE];
};

static void _uuid_generate_str (cmb_t c)
{
    char s[sizeof (uuid_t) * 2 + 1];
    uuid_t uuid;
    int i;

    uuid_generate (uuid);
    for (i = 0; i < sizeof (uuid_t); i++)
        snprintf (s + i*2, sizeof (s) - i*2, "%-.2x", uuid[i]);
    snprintf (c->uuid, sizeof (c->uuid), "api.%s", s);
}

static int _json_object_add_int (json_object *o, char *name, int i)
{
    json_object *no;

    if (!(no = json_object_new_int (i))) {
        errno = ENOMEM;
        return -1;
    }
    json_object_object_add (o, name, no);
    return 0;
}

static int _json_object_add_string (json_object *o, char *name, char *s)
{
    json_object *no;

    if (!(no = json_object_new_string (s))) {
        errno = ENOMEM;
        return -1;
    }
    json_object_object_add (o, name, no);
    return 0;
}

static int _json_object_get_int (json_object *o, char *name, int *ip)
{
    json_object *no = json_object_object_get (o, name);
    if (!no) {
        errno = EPROTO;
        return -1;
    } 
    *ip = json_object_get_int (no);
    return 0;
}

static int _json_object_get_string (json_object *o, char *name, const char **sp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no) {
        errno = EPROTO;
        return -1;
    } 
    *sp = json_object_get_string (no);
    return 0;
}


static int cmb_send (cmb_t c, char *buf, int len)
{
    if (len > CMB_API_BUFSIZE) {
        errno = EINVAL;
        goto error;
    }
    if (send (c->fd, buf, len, 0) < len)
        goto error;
    return 0;
error:
    return -1;
}

static int cmb_recv (cmb_t c, char *buf, int len, int *lenp)
{
    int n;

    n = read (c->fd, buf, len);
    if (n < 0)
        goto error;
    if (n == 0) {
        errno = EPROTO;
        goto error;
    }
    *lenp = n;
    return 0;
error:
    return -1;
}

static int cmb_send_json (cmb_t c, json_object *o, const char *fmt, ...)
{
    va_list ap;
    char *tag = NULL;
    const char *body;
    int n, taglen, bodylen, totlen;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        goto error;
    taglen = strlen (tag);
    body = o ? json_object_to_json_string (o) : NULL;
    bodylen = o ? strlen (body) : 0;
    totlen = taglen + bodylen + 1;
    if (taglen + bodylen + 1 > sizeof (c->buf)) {
        errno = EINVAL; 
        goto error;
    }
    memcpy (c->buf, tag, taglen + 1);
    memcpy (c->buf + taglen + 1, body, bodylen);
    if (cmb_send (c, c->buf, totlen) < 0)
        goto error;

    json_object_put (o); 
    if (tag)
        free (tag);
    return 0;
error:
    if (tag)
        free (tag);
    return -1;
}

static int cmb_recv_json (cmb_t c, char **tagp, json_object **op)
{
    char *tag = NULL;
    char *body = NULL;
    int taglen, bodylen, totlen;
    json_object *o = NULL;

    if (cmb_recv (c, c->buf, sizeof (c->buf), &totlen) < 0)
        goto error;
    for (taglen = 0; taglen < totlen; taglen++) {
        if (c->buf[taglen] == '\0') {
            tag = strdup (c->buf);
            if (!tag)
                goto nomem;
            break;
        }
    }
    bodylen = totlen - taglen - 1;
    if (bodylen > 0) {
        body = malloc (bodylen + 1);
        if (!body)
            goto nomem;
        memcpy (body, &c->buf[taglen + 1], bodylen);
        body[bodylen] = '\0';
        o = json_tokener_parse (body);
        if (!o)
            goto error;
        free (body);
    }
    if (tagp)
        *tagp = tag;
    else
        free (tag);
    if (op)
        *op = o;
    else if (!op)
        json_object_put (o);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (body)
        free (body);
    if (tag)
        free (tag);
    return -1;
}

int cmb_ping (cmb_t c, int seq, int padding)
{
    json_object *o = NULL;
    int rseq;

    if (cmb_send_json (c, NULL, "api.subscribe.ping.%s", c->uuid) < 0)
        goto error;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_int (o, "seq", seq) < 0)
        goto error;
    if (padding > 0) {
        char *pad = malloc (padding + 1);
        if (!pad) {
            fprintf (stderr, "out of memory\n");
            exit (1);
        }
        memset (pad, 'z', padding);
        pad[padding] = '\0';
        if (_json_object_add_string (o, "padding", pad) < 0)
            goto error;
        free (pad);
    }
    if (cmb_send_json (c, o, "ping.%s", c->uuid) < 0)
        goto error;
    o = NULL;

    /* receive a copy back */
    if (cmb_recv_json (c, NULL, &o) < 0)
        goto error;
    if (_json_object_get_int (o, "seq", &rseq) < 0)
        goto error;
    json_object_put (o);
    o = NULL;
    if (seq != rseq) {
        fprintf (stderr, "cmb_ping: seq not the one I sent\n");
        goto error;
    }

    if (cmb_send_json (c, NULL, "api.unsubscribe") < 0)
        goto error;
    return 0;
nomem:
    errno = ENOMEM;
error:    
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_snoop (cmb_t c, char *sub)
{
    char *tag;
    json_object *o;

    if (cmb_send_json (c, NULL, "api.subscribe.%s", sub) < 0)
        goto error;

    while (cmb_recv_json (c, &tag, &o) == 0) {
        fprintf (stderr, "snoop: %s %s\n", tag,
                 o ? json_object_to_json_string (o) : "");
        free (tag);
        if (o)
            json_object_put (o);
    }
    if (cmb_send_json (c, NULL, "api.unsubscribe") < 0)
        goto error;
    return 0;
error:
    return -1;
}

int cmb_barrier (cmb_t c, char *name, int count, int nprocs, int tasks_per_node)
{
    json_object *o = NULL;

    if (cmb_send_json (c, NULL, "api.subscribe.event.barrier.exit.%s",name) < 0)
        goto error;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_int (o, "count", count) < 0)
        goto error;
    if (_json_object_add_int (o, "nprocs", nprocs) < 0)
        goto error;
    if (_json_object_add_int (o, "tasks_per_node", tasks_per_node) < 0)
        goto error;
    if (cmb_send_json (c, o, "barrier.enter.%s", name) < 0)
        goto error;

    /* receive response */
    if (cmb_recv_json (c, NULL, NULL) < 0)
        goto error;

    if (cmb_send_json (c, NULL, "api.unsubscribe") < 0)
        goto error;

    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

/* FIXME: add timeout */
int cmb_sync (cmb_t c)
{
    if (cmb_send_json (c, NULL, "api.subscribe.event.sched.trigger") < 0)
        return -1;
    if (cmb_recv_json (c, NULL, NULL) < 0)
        return -1;
    return 0;
}

int cmb_kvs_put (cmb_t c, char *key, char *val)
{
    json_object *o;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_string (o, "key", key) < 0)
        goto error;
    if (_json_object_add_string (o, "val", val) < 0)
        goto error;
    if (_json_object_add_string (o, "sender", c->uuid) < 0)
        goto error;
    if (cmb_send_json (c, o, "kvs.put") < 0)
        goto error;
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

char *cmb_kvs_get (cmb_t c, char *key)
{
    json_object *o = NULL;
    const char *val;
    char *valcpy;

    if (cmb_send_json (c, NULL, "api.subscribe.%s", c->uuid) < 0)
        goto error;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_string (o, "key", key) < 0)
        goto error;
    if (_json_object_add_string (o, "sender", c->uuid) < 0)
        goto error;
    if (cmb_send_json (c, o, "kvs.get") < 0)
        goto error;
    o = NULL;

    /* receive response */
    if (cmb_recv_json (c, NULL, &o) < 0)
        goto error;
    if (_json_object_get_string (o, "val", &val) < 0)
        goto error;
    valcpy = strdup (val);
    if (!valcpy)
        goto nomem;
    json_object_put (o);
    return valcpy;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return NULL;
}

int cmb_kvs_commit (cmb_t c)
{
    json_object *o = NULL;

    if (cmb_send_json (c, NULL, "api.subscribe.%s", c->uuid) < 0)
        goto error;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_string (o, "sender", c->uuid) < 0)
        goto error;
    if (cmb_send_json (c, o, "kvs.commit") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (cmb_recv_json (c, NULL, NULL) < 0)
        goto error;
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

cmb_t cmb_init (void)
{
    cmb_t c = NULL;
    struct sockaddr_un addr;

    c = malloc (sizeof (struct cmb_struct));
    if (c == NULL) {
        errno = ENOMEM;
        goto error;
    }
    c->fd = socket (AF_UNIX, SOCK_SEQPACKET, 0);
    if (c->fd < 0)
        goto error;
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, CMB_API_PATH, sizeof (addr.sun_path) - 1);

    if (connect (c->fd, (struct sockaddr *)&addr,
                         sizeof (struct sockaddr_un)) < 0)
        goto error;
    _uuid_generate_str (c);
    if (cmb_send_json (c, NULL, "api.setuuid.%s", c->uuid) < 0)
        goto error;
    return c;
error:
    if (c)
        cmb_fini (c);
    return NULL;
}

void cmb_fini (cmb_t c)
{
    if (c->fd >= 0)
        (void)close (c->fd);
    free (c);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
