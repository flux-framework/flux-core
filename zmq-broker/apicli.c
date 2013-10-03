/* apicli.c - implement the public functions in cmb.h */

/* we talk to cmbd via a UNIX domain socket */

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

#include "log.h"
#include "zmsg.h"
#include "cmb.h"
#include "util.h"

struct cmb_struct {
    int fd;
    int rank;
    char rankstr[16];
    int size;
    int flags;
    char *log_facility;
    kvsctx_t kvs_ctx;
};

static int _send_vmessage (cmb_t c, json_object *o, const char *fmt, va_list ap)
{
    zmsg_t *zmsg = NULL;
    char *tag = NULL;
    json_object *empty = NULL;

    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    if (!o)
        o = empty = util_json_object_new_object ();

    zmsg = cmb_msg_encode (tag, o);
    if (c->flags & CMB_FLAGS_TRACE)
        zmsg_dump_compact (zmsg);
    if (zmsg_send_fd (c->fd, &zmsg) < 0) /* destroys msg on succes */
        goto error;
    free (tag);
    return 0;
error:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);
    if (empty)
        json_object_put (empty);
    return -1;
}

static json_object *_recv_vmessage (cmb_t c, const char *fmt, va_list ap)
{
    zmsg_t *zmsg;
    char *tag, *recv_tag;
    json_object *recv_obj;

    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();

    for (;;) {
        zmsg = zmsg_recv_fd (c->fd, false);
        if (!zmsg)
            goto error;
        if (c->flags & CMB_FLAGS_TRACE)
            zmsg_dump_compact (zmsg);
        if (cmb_msg_decode (zmsg, &recv_tag, &recv_obj) < 0)
            goto error;
        if (strcmp (tag, recv_tag) != 0) {
            if (recv_obj)
                json_object_put (recv_obj);
            free (recv_tag);
            zmsg_destroy (&zmsg); /* destroy unexpected response */
            continue;
        }
        if (!recv_obj) {
            errno = EPROTO;
            goto error;
        }
        if (util_json_object_get_int (recv_obj, "errnum", &errno) == 0) {
            free (recv_obj);
            recv_obj = NULL;
        }
        zmsg_destroy (&zmsg);
        free (recv_tag);
        break;
    }
    free (tag);
    return recv_obj;
error:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);
    return NULL;
}

static int _send_message (cmb_t c, json_object *o, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = _send_vmessage (c, o, fmt, ap);
    va_end (ap);

    return rc;
}

int cmb_send_message (cmb_t c, json_object *o, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = _send_vmessage (c, o, fmt, ap);
    va_end (ap);

    return rc;
}

static int _recv_message (cmb_t c, char **tagp, json_object **op, int nonblock)
{
    zmsg_t *zmsg;

    zmsg = zmsg_recv_fd (c->fd, nonblock);
    if (!zmsg)
        goto error;
    if (c->flags & CMB_FLAGS_TRACE)
        zmsg_dump_compact (zmsg);
    if (cmb_msg_decode (zmsg, tagp, op) < 0)
        goto error;
    zmsg_destroy (&zmsg);
    return 0;

error:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return -1;
}

json_object *cmb_request (cmb_t c, json_object *request, const char *fmt, ...)
{
    va_list ap;
    json_object *reply = NULL;
    int rc;

    va_start (ap, fmt);
    rc = _send_vmessage (c, request, fmt, ap);
    va_end (ap);
    if (rc < 0)
        goto done;

    va_start (ap, fmt);
    reply = _recv_vmessage (c, fmt, ap);
    va_end (ap);

done:
    return reply;
}

int cmb_recv_message (cmb_t c, char **tagp, json_object **op, int nb)
{
    return _recv_message (c, tagp, op, nb);
}

zmsg_t *cmb_recv_zmsg (cmb_t c, int nb)
{
    return zmsg_recv_fd (c->fd, nb);
}

int cmb_ping (cmb_t c, char *name, int seq, int padlen, char **tagp, char **routep)
{
    json_object *o = util_json_object_new_object ();
    int rseq;
    char *tag = NULL;
    char *route_cpy = NULL;
    char *pad = NULL;
    const char *route, *rpad;

    /* send request */
    util_json_object_add_int (o, "seq", seq);
    pad = xzmalloc (padlen + 1);
    memset (pad, 'x', padlen);
    util_json_object_add_string (o, "pad", pad);
    if (_send_message (c, o, "%s.ping", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive a copy back */
    if (_recv_message (c, &tag, &o, false) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errno) == 0) /* error rep */
        goto error;
    if (util_json_object_get_int (o, "seq", &rseq) < 0
     || util_json_object_get_string (o, "pad", &rpad) < 0)
        goto eproto;
    if (seq != rseq) {
        msg ("cmb_ping: seq not the one I sent");
        goto eproto;
    }
    if (padlen != strlen (rpad)) {
        msg ("cmb_ping: padd not the size I sent (%d != %d)",
             padlen, (int)strlen (rpad));
        goto eproto;
    }
    if (util_json_object_get_string (o, "route", &route) < 0) {
        msg ("cmb_ping: missing route object");
        goto eproto;
    }
    if (!(route_cpy = strdup (route))) {
        errno = ENOMEM;
        goto error;
    }
        
    if (tagp)
        *tagp = tag;
    else
        free (tag);
    if (routep)
        *routep = route_cpy;
    else
        free (route_cpy);
    json_object_put (o);
    free (pad);
    return 0;
eproto:
    errno = EPROTO;
error:    
    if (o)
        json_object_put (o);
    if (pad)
        free (pad);
    if (tag)
        free (tag);
    if (route_cpy)
        free (route_cpy);
    return -1;
}

char *cmb_stats (cmb_t c, char *name)
{
    json_object *o = util_json_object_new_object ();
    char *cpy = NULL;

    /* send request */
    if (_send_message (c, o, "%s.stats", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (!o)
        goto eproto;
    cpy = strdup (json_object_get_string (o));
    if (!cpy) {
        errno = ENOMEM;
        goto error;
    }
    json_object_put (o);
    return cpy;
eproto:
    errno = EPROTO;
error:    
    if (cpy)
        free (cpy);
    if (o)
        json_object_put (o);
    return NULL;
}

int cmb_snoop (cmb_t c, bool enable)
{
    return _send_message (c, NULL, "api.snoop.%s", enable ? "on" : "off");
}

int cmb_snoop_one (cmb_t c)
{
    zmsg_t *zmsg; 
    int rc = -1;

    zmsg = zmsg_recv_fd (c->fd, false); /* blocking */
    if (zmsg) {
        //zmsg_dump (zmsg);
        zmsg_dump_compact (zmsg);
        zmsg_destroy (&zmsg);
        rc = 0;
    }
    return rc;
}

int cmb_event_subscribe (cmb_t c, char *sub)
{
    return _send_message (c, NULL, "api.event.subscribe.%s", sub ? sub : "");
}

int cmb_event_unsubscribe (cmb_t c, char *sub)
{
    return _send_message (c, NULL, "api.event.unsubscribe.%s", sub ? sub : "");
}

char *cmb_event_recv (cmb_t c)
{
    char *tag = NULL;

    (void)_recv_message (c, &tag, NULL, false);

    return tag;
}

int cmb_event_send (cmb_t c, char *event)
{
    return _send_message (c, NULL, "api.event.send.%s", event);
}

int cmb_barrier (cmb_t c, const char *name, int nprocs)
{
    json_object *o = util_json_object_new_object ();
    int count = 1;
    int errnum = 0;

    /* send request */
    util_json_object_add_int (o, "count", count);
    util_json_object_add_int (o, "nprocs", nprocs);
    if (_send_message (c, o, "barrier.enter.%s", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (util_json_object_get_int (o, "errnum", &errnum) < 0)
        goto error;
    if (errnum != 0) {
        errno = errnum;
        goto error;
    }
    return 0;
error:
    if (o)
        json_object_put (o);
    return -1;
}

void cmb_log_set_facility (cmb_t c, const char *facility)
{
    if (c->log_facility)
        free (c->log_facility);
    c->log_facility = xstrdup (facility);
}

int cmb_vlog (cmb_t c, int lev, const char *fmt, va_list ap)
{
    json_object *o = util_json_vlog (lev,
                             c->log_facility ? c->log_facility : "api-client",
                             c->rankstr, fmt, ap);

    if (!o || _send_message (c, o, "log.msg") < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_log (cmb_t c, int lev, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = cmb_vlog (c, lev, fmt, ap);
    va_end (ap);
    return rc;
}

int cmb_log_subscribe (cmb_t c, int lev, const char *sub)
{
    json_object *o = util_json_object_new_object ();

    if (_send_message (c, o, "log.subscribe.%d.%s", lev, sub) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

int cmb_log_unsubscribe (cmb_t c, const char *sub)
{
    json_object *o = util_json_object_new_object ();

    if (_send_message (c, o, "log.unsubscribe.%s", sub) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

int cmb_log_dump (cmb_t c, int lev, const char *sub)
{
    json_object *o;

    if (!(o = json_object_new_object ()))
        oom ();
    if (_send_message (c, o, "log.dump.%d.%s", lev, sub) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    if (o)
        json_object_put (o);
    return -1;
}

char *cmb_log_recv (cmb_t c, int *lp, char **fp, int *cp,
                    struct timeval *tvp, char **sp)
{
    json_object *o = NULL;
    const char *s, *fac, *src;
    char *msg;
    int lev, count;
    struct timeval tv;

    if (_recv_message (c, NULL, &o, false) < 0 || o == NULL)
        goto error;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (util_json_object_get_string (o, "facility", &fac) < 0
     || util_json_object_get_int (o, "level", &lev) < 0
     || util_json_object_get_string (o, "source", &src) < 0
     || util_json_object_get_timeval (o, "timestamp", &tv) < 0
     || util_json_object_get_string (o, "message", &s) < 0
     || util_json_object_get_int (o, "count", &count) < 0)
        goto eproto;
    if (tvp)
        *tvp = tv;
    if (lp)
        *lp = lev;
    if (fp)
        *fp = xstrdup (fac);
    if (cp)
        *cp = count;
    if (sp)
        *sp = xstrdup (src); 
    msg = xstrdup (s);
    json_object_put (o);
    return msg;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return NULL;
}

int cmb_route_add (cmb_t c, char *dst, char *gw)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "gw", gw);
    if (_send_message (c, o, "cmb.route.add.%s", dst) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

int cmb_route_del (cmb_t c, char *dst, char *gw)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "gw", gw);
    if (_send_message (c, o, "cmb.route.del.%s", dst) < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

/* FIXME: just return JSON string for now */
char *cmb_route_query (cmb_t c)
{
    json_object *o = util_json_object_new_object ();
    char *cpy;

    /* send request */
    if (_send_message (c, o, "cmb.route.query") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    cpy = xstrdup (json_object_get_string (o));
    json_object_put (o);
    return cpy;
error:
    if (o)
        json_object_put (o);
    return NULL;
    
}

int cmb_rank (cmb_t c)
{
    return c->rank;
}

int cmb_size (cmb_t c)
{
    return c->size;
}

static int _session_info_query (cmb_t c)
{
    json_object *o = util_json_object_new_object ();
    int errnum;

    /* send request */
    if (_send_message (c, o, "api.session.info.query") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errnum) == 0) {
        errno = errnum;
        goto error;
    }
    if (util_json_object_get_int (o, "rank", &c->rank) < 0)
        goto error;
    snprintf (c->rankstr, sizeof (c->rankstr), "%d", c->rank);
    if (util_json_object_get_int (o, "size", &c->size) < 0)
        goto eproto;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

static kvsctx_t get_kvs_ctx (void *h)
{
    cmb_t c = h;

    return c->kvs_ctx;
}

cmb_t cmb_init_full (const char *path, int flags)
{
    cmb_t c = NULL;
    struct sockaddr_un addr;

    c = xzmalloc (sizeof (struct cmb_struct));
    c->flags = flags;
    c->fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0)
        goto error;
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (connect (c->fd, (struct sockaddr *)&addr,
                         sizeof (struct sockaddr_un)) < 0)
        goto error;
    if (_session_info_query (c) < 0)
        goto error;

    c->kvs_ctx = kvs_ctx_create (c);
    kvs_reqfun_set ((KVSReqF *)cmb_request);
    kvs_barrierfun_set ((KVSBarrierF *)cmb_barrier);
    kvs_getctxfun_set ((KVSGetCtxF *)get_kvs_ctx);
    return c;
error:
    if (c)
        cmb_fini (c);
    return NULL;
}

cmb_t cmb_init (void)
{
    const char *val;
    char path[PATH_MAX + 1];

    if ((val = getenv ("CMB_API_PATH"))) {
        if (strlen (val) > PATH_MAX) {
            err ("Crazy value for CMB_API_PATH!");
            return (NULL);
        }
        strcpy(path, val);
    }
    else
        snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    return cmb_init_full (path, 0);
}

void cmb_fini (cmb_t c)
{
    if (c->fd >= 0)
        (void)close (c->fd);
    if (c->log_facility)
        free (c->log_facility);
    if (c->kvs_ctx)
        kvs_ctx_destroy (c->kvs_ctx);
    free (c);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
