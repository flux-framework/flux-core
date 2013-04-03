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

static void
_uuid_generate_str (cmb_t c)
{
    char s[sizeof (uuid_t) * 2 + 1];
    uuid_t uuid;
    int i;

    uuid_generate (uuid);
    for (i = 0; i < sizeof (uuid_t); i++)
        snprintf (s + i*2, sizeof (s) - i*2, "%-.2x", uuid[i]);
    snprintf (c->uuid, sizeof (c->uuid), "api.%s", s);
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
    if (cmb_sendf (c, "setuuid %s", c->uuid) < 0)
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

int cmb_send (cmb_t c, char *buf, int len)
{
    if (len > CMB_API_BUFSIZE) {
        errno = EINVAL;
        goto error;
    }
    if (write (c->fd, buf, len) < len)
        goto error;
    return 0;
error:
    return -1;
}

int cmb_sendf (cmb_t c, const char *fmt, ...)
{
    va_list ap;
    char *s = NULL, *p;
    int len, n;

    va_start (ap, fmt);
    n = vasprintf(&s, fmt, ap);
    va_end (ap);
    if (n < 0)
        goto error;
    len = strlen (s) + 1;
    if ((p = strchr (s, ' '))) {
        *p = '\0';
        len--; /* don't null terminate body, if there is one */
    }
    if (cmb_send (c, s, len) < 0)
        goto error;
    free (s);
    return 0;
error:
    if (s)
        free (s);
    return -1;
}

int cmb_recv (cmb_t c, char *buf, int len, int *lenp)
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

int cmb_recvs (cmb_t c, char **tagp, char **bodyp)
{
    char *bodycpy = NULL;
    char *tagcpy = NULL;
    char *body;
    int n, i, bodylen;

    if (cmb_recv (c, c->buf, sizeof (c->buf), &n) < 0)
        goto error;
    for (i = 0; i < n ; i++)
        if (c->buf[i] == '\0')
            break;
    bodylen = n - i - 1;
    if (bodylen < 0) {
        errno = EPROTO;
        goto error;
    }
    body = &c->buf[i + 1];

    tagcpy = strdup (c->buf);
    bodycpy = malloc (bodylen + 1);
    if (!tagcpy || !bodycpy) {
        errno = ENOMEM;
        goto error;
    }
    memcpy (bodycpy, body, bodylen);
    bodycpy[bodylen] = '\0';

    *tagp = tagcpy;
    *bodyp = bodycpy;
    return 0;
error:
    if (tagcpy)
        free (tagcpy);
    if (bodycpy)
        free (bodycpy);
    return -1;
}

int cmb_ping (cmb_t c, int seq)
{
    char *tag = NULL;
    char *body = NULL;
    json_object *no, *o = NULL;

    if (cmb_sendf (c, "subscribe ping") < 0)
        goto error;
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_int (seq)))
        goto nomem;
    json_object_object_add (o, "seq", no);

    if (cmb_sendf (c, "ping %s", json_object_to_json_string (o)) < 0)
        goto error;
    if (cmb_recvs (c, &tag, &body))
        goto error;
    if (cmb_sendf (c, "unsubscribe") < 0)
        goto error;
    json_object_put (o);
    if (!(o = json_tokener_parse (body)))
        goto error;
    if (!(no = json_object_object_get (o, "seq")))
        goto error;
    printf ("ping: %s: %d\n", tag, json_object_get_int (no));
    free (tag);
    free (body);
    json_object_put (o);
    return 0;
nomem:
    errno = ENOMEM;
error:    
    if (tag)
        free (tag);
    if (body)
        free (body);
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_snoop (cmb_t c, char *sub)
{
    char *tag, *body;

    if (cmb_sendf (c, "subscribe %s", sub) < 0)
        goto error;
    while (cmb_recvs (c, &tag, &body) == 0) {
        fprintf (stderr, "snoop: %s %s\n", tag, body);
        free (tag);
        free (body);
    }
    if (cmb_sendf (c, "unsubscribe") < 0)
        goto error;
    return 0;
error:
    return -1;
}

int cmb_barrier (cmb_t c, char *name, int count, int nprocs, int tasks_per_node)
{
    json_object *no, *o = NULL;
    char *tag = NULL, *body = NULL;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_int (count)))
        goto nomem;
    json_object_object_add (o, "count", no);
    if (!(no = json_object_new_int (nprocs)))
        goto nomem;
    json_object_object_add (o, "nprocs", no);
    if (!(no = json_object_new_int (tasks_per_node)))
        goto nomem;
    json_object_object_add (o, "tasks_per_node", no);
    if (cmb_sendf (c, "subscribe event.barrier.exit.%s", name) < 0)
        goto error;
    if (cmb_sendf (c, "barrier.enter.%s %s", name,
                   json_object_to_json_string (o)) < 0)
        goto error;
    if (cmb_recvs (c, &tag, &body) < 0)
        goto error;
    if (cmb_sendf (c, "unsubscribe") < 0)
        goto error;
    json_object_put (o);
    free (tag);
    free (body);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    if (tag)
        free (tag);
    if (body)
        free (body);
    return -1;
}

/* FIXME: add timeout */
int cmb_sync (cmb_t c)
{
    char *tag, *body;

    if (cmb_sendf (c, "subscribe event.sched.trigger") < 0)
        goto error;
    if (cmb_recvs (c, &tag, &body) < 0)
        goto error;
    free (tag);
    free (body);
    return 0;
error:
    if (tag)
        free (tag);
    if (body)
        free (body);
    return -1;
}

int cmb_kvs_put (cmb_t c, char *key, char *val)
{
    json_object *no, *o;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_string (key)))
        goto nomem;
    json_object_object_add (o, "key", no);
    if (!(no = json_object_new_string (val)))
        goto nomem;
    json_object_object_add (o, "val", no);
    if (!(no = json_object_new_string (c->uuid)))
        goto nomem;
    json_object_object_add (o, "sender", no);
    if (cmb_sendf (c, "kvs.put %s", json_object_to_json_string (o)) < 0)
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

char *cmb_kvs_get (cmb_t c, char *key)
{
    json_object *no, *o = NULL;
    char *tag = NULL, *body = NULL;
    const char *val;
    char *valcpy;

    if (cmb_sendf (c, "subscribe %s", c->uuid) < 0)
        goto error;
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_string (key)))
        goto nomem;
    json_object_object_add (o, "key", no);
    if (!(no = json_object_new_string (c->uuid)))
        goto nomem;
    json_object_object_add (o, "sender", no);
    if (cmb_sendf (c, "kvs.get %s", json_object_to_json_string (o)) < 0)
        goto error;
    json_object_put (o);
    o = NULL;
    if (cmb_recvs (c, &tag, &body) < 0)
        goto error;
    o = json_tokener_parse (body);
    no = json_object_object_get (o, "val");
    if (!no) {
        errno = EPROTO;
        goto error;
    }
    val = json_object_get_string (no);
    valcpy = strdup (val);
    if (!valcpy)
        goto nomem;
    json_object_put (o);
    free (tag);
    free (body);
    return valcpy;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    if (tag)
        free (tag);
    if (body)
        free (body);
    return NULL;
}

int cmb_kvs_commit (cmb_t c)
{
    json_object *no, *o = NULL;
    char *tag = NULL, *body = NULL;

    if (cmb_sendf (c, "subscribe %s", c->uuid) < 0)
        goto error;
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_string (c->uuid)))
        goto nomem;
    json_object_object_add (o, "sender", no);
    if (cmb_sendf (c, "kvs.commit %s", json_object_to_json_string (o)) < 0)
        goto error;
    json_object_put (o);
    o = NULL;
    if (cmb_recvs (c, &tag, &body) < 0)
        goto error;
    free (tag);
    free (body);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    if (tag)
        free (tag);
    if (body)
        free (body);
    return -1;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
