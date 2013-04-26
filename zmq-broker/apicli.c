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
#include <uuid/uuid.h>
#include <json/json.h>
#include <czmq.h>

#include "cmbd.h"
#include "log.h"
#include "zmq.h"
#include "util.h"
#include "cmb.h"

struct cmb_struct {
    int fd;
    char uuid[64];
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

static int _json_object_add_string (json_object *o, char *name, const char *s)
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
    if (!no)
        return -1;
    *ip = json_object_get_int (no);
    return 0;
}

static int _json_object_get_string (json_object *o, char *name, const char **sp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *sp = json_object_get_string (no);
    return 0;
}

static int _json_object_get_int_array (json_object *o, char *name,
                                       int **ap, int *lp)
{
    json_object *no = json_object_object_get (o, name);
    json_object *vo;
    int i, len, *arr = NULL;

    if (!no)
        goto eproto;
    len = json_object_array_length (no);
    arr = malloc (sizeof (int) * len);
    if (!arr) {
        errno = ENOMEM;
        goto error;
    }
    for (i = 0; i < len; i++) {
        vo = json_object_array_get_idx (no, i); 
        if (!vo)
            goto eproto;
        arr[i] = json_object_get_int (vo);
    }
    *ap = arr;
    *lp = len;
    return 0; 
eproto:
    errno = EPROTO;
error:
    if (arr)
        free (arr);
    return -1;
}
                                       

static int _recvfd (int fd, int *fdp, char *name, int len)
{
    int fd_xfer, n;
    char buf[CMSG_SPACE (sizeof (fd_xfer)) ];
    struct msghdr msghdr;
    struct iovec iov;
    struct cmsghdr *cmsg;

    memset (&msghdr, 0, sizeof (msghdr));
    iov.iov_base = name;
    iov.iov_len = len - 1;
    msghdr.msg_iov = &iov;
    msghdr.msg_iovlen = 1;
    msghdr.msg_control = buf;
    msghdr.msg_controllen = sizeof (buf);

    if ((n = recvmsg (fd, &msghdr, 0)) < 0)
        return -1;
    
    name [n] = '\0';

    cmsg = CMSG_FIRSTHDR (&msghdr);
    if (cmsg == NULL) {
        msg ("_recvfd: no control message received");
        return -1;
    }
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        msg ("_recvfd: message level/rights have wrong values");
        return -1;
    }
    fd_xfer = *(int *) CMSG_DATA (cmsg);

    *fdp = fd_xfer;

    return 0;
}

int cmb_fd_open (cmb_t c, char *wname, char **np)
{
    int newfd = -1;
    char *name = NULL;
    char buf[1024];

    if (wname) {
        if (cmb_msg_send_fd (c->fd, NULL, "api.fdopen.write.%s", wname) < 0)
            goto error;
    } else {
        if (cmb_msg_send_fd (c->fd, NULL, "api.fdopen.read") < 0)
            goto error;
    }
    if (_recvfd (c->fd, &newfd, buf, sizeof (buf)) < 0)
        goto error;
    if (np) {
        name = strdup (buf);
        if (!name) {
            errno = ENOMEM;
            goto error;
        }
        *np = name;
    }
    return newfd;
error:
    if (name)
        free (name);
    if (newfd != -1)
        close (newfd);
    return -1;
}

int cmb_ping (cmb_t c, char *name, int seq, int padlen, char **tagp)
{
    json_object *o = NULL;
    int rseq;
    void *rpad = NULL, *pad = NULL;
    char *tag = NULL;
    int rpadlen;

    /* send request */
    if (!(o = json_object_new_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (_json_object_add_int (o, "seq", seq) < 0)
        goto error;
    if (padlen > 0) {
        pad = malloc (padlen);
        if (!pad)
            oom ();
        memset (pad, 'z', padlen);
    }
    if (cmb_msg_send_long_fd (c->fd, o, pad, padlen, "%s.ping", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive a copy back */
    if (cmb_msg_recv_fd (c->fd, &tag, &o, &rpad, &rpadlen, 0) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (_json_object_get_int (o, "seq", &rseq) < 0)
        goto eproto;
    if (seq != rseq) {
        msg ("cmb_ping: seq not the one I sent");
        goto eproto;
    }
    if (padlen != rpadlen) {
        msg ("cmb_ping: payload not the size I sent (%d != %d)",
             padlen, rpadlen);
        goto eproto;
    }
    if (padlen > 0) {
        if (memcmp (pad, rpad, padlen) != 0) {
            msg ("cmb_ping: received corrupted payload");
            goto eproto;
        }
    }
    if (tagp)
        *tagp = tag;
    else
        free (tag);

    json_object_put (o);
    if (pad)
        free (pad);
    if (rpad)
        free (rpad);
    return 0;
eproto:
    errno = EPROTO;
error:    
    if (o)
        json_object_put (o);
    if (pad)
        free (pad);
    if (rpad)
        free (rpad);
    if (tag)
        free (tag);
    return -1;
}

int cmb_stats (cmb_t c, char *name, int *req, int *rep, int *event)
{
    json_object *o = NULL;

    /* send request */
    if (!(o = json_object_new_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (cmb_msg_send_fd (c->fd, o, "%s.stats", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (cmb_msg_recv_fd (c->fd, NULL, &o, NULL, NULL, 0) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (_json_object_get_int (o, "req_count", req) < 0
                    || _json_object_get_int (o, "rep_count", rep) < 0
                    || _json_object_get_int (o, "event_count", event) < 0)
        goto eproto;
    if (o)
        json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:    
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_snoop (cmb_t c, bool enable)
{
    return cmb_msg_send_fd (c->fd, NULL, "api.snoop.%s", enable ? "on" : "off");
}

int cmb_snoop_one (cmb_t c)
{
    zmsg_t *zmsg; 
    int rc = -1;

    zmsg = zmsg_recv_fd (c->fd, 0); /* blocking */
    if (zmsg) {
        cmb_dump (zmsg);
        zmsg_destroy (&zmsg);
        rc = 0;
    }
    return rc;
}

int cmb_event_subscribe (cmb_t c, char *sub)
{
    return cmb_msg_send_fd (c->fd, NULL, "api.event.subscribe.%s",
                            sub ? sub : "");
}

int cmb_event_unsubscribe (cmb_t c, char *sub)
{
    return cmb_msg_send_fd (c->fd, NULL, "api.event.unsubscribe.%s",
                            sub ? sub : "");
}

char *cmb_event_recv (cmb_t c)
{
    char *tag = NULL;

    (void)cmb_msg_recv_fd (c->fd, &tag, NULL, NULL, NULL, 0);

    return tag;
}

int cmb_event_send (cmb_t c, char *event)
{
    return cmb_msg_send_fd (c->fd, NULL, "api.event.send.%s", event);
}

int cmb_barrier (cmb_t c, char *name, int nprocs)
{
    json_object *o = NULL;
    int count = 1;

    if (cmb_msg_send_fd (c->fd, NULL,
                         "api.event.subscribe.event.barrier.exit.%s", name) < 0)
        return -1;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_int (o, "count", count) < 0)
        goto error;
    if (_json_object_add_int (o, "nprocs", nprocs) < 0)
        goto error;
    if (cmb_msg_send_fd (c->fd, o, "barrier.enter.%s", name) < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* wait for event */
    if (cmb_msg_recv_fd (c->fd, NULL, NULL, NULL, NULL, 0) < 0)
        goto error;

    cmb_msg_send_fd (c->fd, NULL,
                     "api.event.unsubscribe.event.barrier.exit.%s", name);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

int cmb_kvs_put (cmb_t c, const char *key, const char *val)
{
    json_object *o;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_string (o, "key", key) < 0)
        goto error;
    if (_json_object_add_string (o, "val", val) < 0)
        goto error;
    if (cmb_msg_send_fd (c->fd, o, "kvs.put") < 0)
        goto error;
    json_object_put (o);
    o = NULL;
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

char *cmb_kvs_get (cmb_t c, const char *key)
{
    json_object *o = NULL;
    const char *val;
    char *ret;

    /* send request */
    if (!(o = json_object_new_object ()))
        goto nomem;
    if (_json_object_add_string (o, "key", key) < 0)
        goto error;
    if (cmb_msg_send_fd (c->fd, o, "kvs.get") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (cmb_msg_recv_fd (c->fd, NULL, &o, NULL, NULL, 0) < 0)
        goto error;
    if (_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (_json_object_get_string (o, "val", &val) < 0) {
        errno = 0; /* key was not set */
        ret = NULL;
    } else {
        ret = strdup (val);
        if (!ret)
            goto nomem;
    }
    json_object_put (o);
    return ret;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return NULL;
}

int cmb_live_query (cmb_t c, int **upp, int *ulp, int **dp, int *dlp, int *nnp)
{
    json_object *o = NULL;
    int nnodes;
    int *up, up_len;
    int *down, down_len;

    /* send request */
    if (!(o = json_object_new_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (cmb_msg_send_fd (c->fd, o, "live.query") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (cmb_msg_recv_fd (c->fd, NULL, &o, NULL, NULL, 0) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (_json_object_get_int (o, "nnodes", &nnodes) < 0)
        goto eproto;
    if (_json_object_get_int_array (o, "up", &up, &up_len) < 0)
        goto eproto;
    if (_json_object_get_int_array (o, "down", &down, &down_len) < 0)
        goto eproto;
    json_object_put (o);

    *upp = up;
    *ulp = up_len;

    *dp = down;
    *dlp = down_len;

    *nnp = nnodes;
    return 0; 
eproto:
    errno = EPROTO;
error:
    if (o)
        json_object_put (o);
    return -1; 
}

int cmb_kvs_commit (cmb_t c, int *ep, int *pp)
{
    json_object *o = NULL;
    int errcount, putcount;

    /* send request */
    if (!(o = json_object_new_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (cmb_msg_send_fd (c->fd, o, "kvs.commit") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    /* receive response */
    if (cmb_msg_recv_fd (c->fd, NULL, &o, NULL, NULL, 0) < 0)
        goto error;
    if (!o)
        goto eproto;
    if (_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    if (_json_object_get_int (o, "errcount", &errcount) < 0)
        goto eproto;
    if (_json_object_get_int (o, "putcount", &putcount) < 0)
        goto eproto;
    json_object_put (o);
    if (ep)
        *ep = errcount;
    if (pp)
        *pp = putcount;
    return 0;
eproto:
    errno = EPROTO;
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
