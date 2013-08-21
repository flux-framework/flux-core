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
};

static int _send_vmessage (cmb_t c, json_object *o, const char *fmt, va_list ap)
{
    zmsg_t *zmsg = NULL;
    char *tag = NULL;
    int n;

    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");

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
    return -1;
}

static int _send_message (cmb_t c, json_object *o, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    return _send_vmessage (c, o, fmt, ap);
}

int cmb_send_message (cmb_t c, json_object *o, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    return _send_vmessage (c, o, fmt, ap);
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

int cmb_barrier (cmb_t c, char *name, int nprocs)
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

int cmb_conf_put (cmb_t c, const char *key, json_object *vo)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    util_json_object_add_string (o, "key", key);
    json_object_get (vo);
    json_object_object_add (o, "val", vo);
    if (_send_message (c, o, "conf.put") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL || util_json_object_get_int (o, "errnum", &errno) < 0)
        goto eproto;
    if (errno != 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_conf_commit (cmb_t c)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    if (_send_message (c, o, "conf.commit") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL || util_json_object_get_int (o, "errnum", &errno) < 0)
        goto eproto;
    if (errno != 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

json_object *cmb_conf_get (cmb_t c, const char *key, bool watch)
{
    json_object *o = util_json_object_new_object ();
    json_object *vo = NULL;

    /* send request */
    util_json_object_add_string (o, "key", key);
    util_json_object_add_boolean (o, "watch", watch);
    if (_send_message (c, o, "conf.get") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    vo = json_object_object_get (o, "val");
    json_object_get (vo);
    json_object_put (o);
    return vo;
error:
    if (o)
        json_object_put (o);
    return NULL;
}

int cmb_conf_list (cmb_t c)
{
    json_object *o = util_json_object_new_object ();

    if (_send_message (c, o, "conf.list") < 0)
        goto error;
    json_object_put (o);
    return 0;
error:
    json_object_put (o);
    return -1;
}

/* EOF = error response with errnum == 0 */
int cmb_conf_next (cmb_t c, char **kp, json_object **vop)
{
    json_object *o = NULL;
    const char *key;
    json_object *vo;

    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (util_json_object_get_string (o, "key", &key) < 0)
        goto eproto;
    vo = json_object_object_get (o, "val");
    if (vop) {
        json_object_get (vo);
        *vop = vo;
    }
    if (kp)
        *kp = xstrdup (key);
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_kvs_put (cmb_t c, const char *key, json_object *val)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    json_object_get (val);
    json_object_object_add (o, key, val);
    if (_send_message (c, o, "kvs.put") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL || util_json_object_get_int (o, "errnum", &errno) < 0)
        goto eproto;
    if (errno != 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_kvs_flush (cmb_t c)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    if (_send_message (c, o, "kvs.flush") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL || util_json_object_get_int (o, "errnum", &errno) < 0)
        goto eproto;
    if (errno != 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_kvs_commit (cmb_t c, const char *name)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    util_json_object_add_string (o, "name", name);
    if (_send_message (c, o, "kvs.commit") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

static int kvs_get (cmb_t c, const char *key, json_object **op, const char *tag)
{
    json_object *o = util_json_object_new_object ();
    json_object *val = NULL;

    /* send request */
    json_object_object_add (o, key, NULL);
    if (_send_message (c, o, "%s", tag) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if ((val = json_object_object_get (o, key)))
        json_object_get (val);
    *op = val; 
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_kvs_get_val (cmb_t c, const char *key, json_object **op)
{
    return kvs_get (c, key, op, "kvs.get.val");
}

int cmb_kvs_get_dir (cmb_t c, const char *key, json_object **op)
{
    return kvs_get (c, key, op, "kvs.get.dir");
}

int cmb_kvs_clean (cmb_t c)
{
    json_object *o = util_json_object_new_object ();

    /* send request */
    if (_send_message (c, o, "kvs.clean") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (_recv_message (c, NULL, &o, false) < 0)
        goto error;
    if (o == NULL || util_json_object_get_int (o, "errnum", &errno) < 0)
        goto eproto;
    if (errno != 0)
        goto error;
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
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
    return c;
error:
    if (c)
        cmb_fini (c);
    return NULL;
}

cmb_t cmb_init (void)
{
    char path[PATH_MAX + 1];

    snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    return cmb_init_full (path, 0);
}

void cmb_fini (cmb_t c)
{
    if (c->fd >= 0)
        (void)close (c->fd);
    if (c->log_facility)
        free (c->log_facility);
    free (c);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
