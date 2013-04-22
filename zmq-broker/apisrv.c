/* apisrv.c - bridge unix domain API socket and zmq message bus */

/* FIXME: consider adding SO_PEERCRED info for connected clients? */

/* FIXME: writes to fds can block and we have no buffering  */

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
#include <fcntl.h>
#include <zmq.h>
#include <czmq.h>
#include <uuid/uuid.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "apisrv.h"

#define LISTEN_BACKLOG      5

typedef struct _cfd_struct {
    int fd;
    struct _cfd_struct *next;
    struct _cfd_struct *prev;
    char *name; /* <uuid>.fd.<cfd_id> */
    char *wname; /* user-provided, indicates API will read */
    char buf[CMB_API_BUFSIZE / 2];
} cfd_t;

typedef struct _client_struct {
    int fd;
    struct _client_struct *next;
    struct _client_struct *prev;
    plugin_ctx_t *p;
    zhash_t *disconnect_notify;
    char *subscription;
    bool subscription_exact;
    char *uuid;
    cfd_t *cfds;
    int cfd_id;
} client_t;


typedef struct {
    int listen_fd;
    client_t *clients;
} ctx_t;

static char *_uuid_generate (void)
{
    char s[sizeof (uuid_t) * 2 + 1];
    uuid_t uuid;
    int i;

    uuid_generate (uuid);
    for (i = 0; i < sizeof (uuid_t); i++)
        snprintf (s + i*2, sizeof (s) - i*2, "%-.2x", uuid[i]);
    return xstrdup (s);
}

static int _fd_setmode (int fd, int mode)
{
    int flags;

    flags = fcntl (fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    flags &= ~O_ACCMODE;
    flags |= mode;
    return fcntl (fd, F_SETFL, flags);
}

static int _sendfd (int fd, int fd_xfer, char *name)
{
    struct msghdr msg;
    struct iovec iov;
    struct cmsghdr *cmsg;
    char buf[ CMSG_SPACE (sizeof (fd_xfer)) ];
    int *fdptr;

    memset (&msg, 0, sizeof (msg));

    iov.iov_base = name;
    iov.iov_len = strlen (name);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof (buf);

    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (sizeof (int));
    fdptr = (int *) CMSG_DATA (cmsg);
    memcpy (fdptr, &fd_xfer, sizeof (int));
    msg.msg_controllen = cmsg->cmsg_len;

    return sendmsg (fd, &msg, 0);
}

static cfd_t *_cfd_create (plugin_ctx_t *p, client_t *c, char *wname)
{
    cfd_t *cfd;
    int sv[2];

    cfd = xzmalloc (sizeof (cfd_t));
    cfd->fd = -1;
    if (socketpair (AF_LOCAL, SOCK_SEQPACKET, 0, sv) < 0)
        err_exit ("socketpair");
    if (_fd_setmode (sv[1], wname ? O_RDONLY : O_WRONLY) < 0)
        err_exit ("fcntl");
    if (_fd_setmode (sv[0], wname ? O_WRONLY : (O_RDONLY | O_NONBLOCK)) < 0)
        err_exit ("fcntl");
    cfd->fd = sv[0];
    if (asprintf (&cfd->name, "%s.fd.%d", c->uuid, c->cfd_id++) < 0)
        oom ();
    if (wname)
        cfd->wname = xstrdup (wname);
    if (_sendfd (c->fd, sv[1], cfd->name) < 0)
        err_exit ("sendfd");
    if (close (sv[1]) < 0)
        err_exit ("close");
    cmb_msg_send (p->zs_out, NULL, "%s.open", cfd->name);

    cfd->prev = NULL;
    cfd->next = c->cfds;
    if (cfd->next)
        cfd->next->prev = cfd;
    c->cfds = cfd;

    return cfd;
}

static void _cfd_destroy (plugin_ctx_t *p, client_t *c, cfd_t *cfd)
{
    if (cfd->fd != -1)
        close (cfd->fd);

    if (cfd->prev)
        cfd->prev->next = cfd->next;
    else
        c->cfds = cfd->next;
    if (cfd->next)
        cfd->next->prev = cfd->prev;

    cmb_msg_send (p->zs_out, NULL, "%s.close", cfd->name);
    free (cfd->name);
    free (cfd);
}

static int _cfd_count (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
    cfd_t *cfd;
    int count = 0;

    for (c = ctx->clients; c != NULL; c = c->next)
        for (cfd = c->cfds; cfd != NULL; cfd = cfd->next)
            count++;
    return count;
}

/* read from cfd->fd, send message to cfd->wname */
static int _cfd_read (plugin_ctx_t *p, cfd_t *cfd)
{
    int n;
    json_object *o, *no;

    assert (cfd->wname != NULL);
    n = read (cfd->fd, cfd->buf, sizeof (cfd->buf));
    if (n <= 0) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && n != 0)
            err ("apisrv: cfd read");
        return -1;
    }
    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_string (cfd->name)))
        oom ();
    json_object_object_add (o, "sender", no);
    cmb_msg_send_long (p->zs_out, o, cfd->buf, n, "%s", cfd->wname);
    json_object_put (o);
    return -1;
}

/* message received for cfd->name, write to cfd->fd */
static int _cfd_write (cfd_t *cfd, zmsg_t *zmsg)
{
    int len, n;

    if (cfd->wname != NULL) {
        msg ("_cfd_write: discarding message for O_WRONLY fd");
        return 0;
    }
    len = cmb_msg_datacpy (zmsg, cfd->buf, sizeof (cfd->buf));
    n = write (cfd->fd, cfd->buf, len);
    if (n < 0)
        return -1;
    if (n < len) {
        msg ("_cfd_write: short write");
        return 0;
    }
    return 0;
}

static void _client_create (plugin_ctx_t *p, int fd)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->fd = fd;
    c->cfds = NULL;
    c->uuid = _uuid_generate ();
    c->p = p;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    zhash_autofree (c->disconnect_notify);
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
}

static int _notify_srv (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    zmsg_t *zmsg; 

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (zmsg_pushstr (zmsg, "%s.disconnect", key) < 0)
        err_exit ("zmsg_pushstr");

    if (zmsg_pushmem (zmsg, NULL, 0) < 0)
        err_exit ("zmsg_pushmem");
    if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
        err_exit ("zmsg_pushmem");
    if (zmsg_send (&zmsg, c->p->zs_req) < 0)
        err_exit ("zmsg_send");

    return 0;
}

static void _client_destroy (plugin_ctx_t *p, client_t *c)
{
    ctx_t *ctx = p->ctx;

    zhash_foreach (c->disconnect_notify, _notify_srv, c);
    zhash_destroy (&c->disconnect_notify);

    while ((c->cfds) != NULL)
        _cfd_destroy (p, c, c->cfds);
    free (c->uuid);
    close (c->fd);

    if (c->prev)
        c->prev->next = c->next;
    else
        ctx->clients = c->next;
    if (c->next)
        c->next->prev = c->prev;
    free (c);
}

static int _client_count (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
    int count = 0;

    for (c = ctx->clients; c != NULL; c = c->next)
        count++;
    return count;
}

static void _accept (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int fd;

    fd = accept (ctx->listen_fd, NULL, NULL); 
    if (fd < 0)
        err_exit ("accept");
     _client_create (p, fd);
}

static int _client_read (plugin_ctx_t *p, client_t *c)
{
    const char *api_subscribe = "api.subscribe.";
    const char *api_xsubscribe = "api.xsubscribe.";
    const char *api_fdopen_write = "api.fdopen.write.";
    zmsg_t *zmsg = NULL;
    char *tag = NULL;

    zmsg = zmsg_recv_fd (c->fd, MSG_DONTWAIT);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            err ("API read");
        return -1;
    }
    if (cmb_msg_decode (zmsg, &tag, NULL, NULL, NULL) < 0) {
        err ("API decode");
        goto done;
    }
        
    /* internal: api.unsubscribe */
    if (!strcmp (tag, "api.unsubscribe")) {
        if (c->subscription) {
            free (c->subscription);
            c->subscription = NULL;
        }

    /* internal: api.subscribe */
    } else if (!strncmp (tag, api_subscribe, strlen (api_subscribe))) {
        char *q = tag + strlen (api_subscribe);
        if (c->subscription)
            free (c->subscription);
        c->subscription = xstrdup (q);
        c->subscription_exact = false;

    /* internal: api.xsubscribe */
    } else if (!strncmp (tag, api_xsubscribe, strlen (api_xsubscribe))) {
        char *q = tag + strlen (api_xsubscribe);
        if (c->subscription)
            free (c->subscription);
        c->subscription = xstrdup (q);
        c->subscription_exact = true;

    /* internal: api.fdopen.read */
    } else if (!strcmp (tag, "api.fdopen.read")) {
        _cfd_create (p, c, NULL);

    /* internal: api.fdopen.write */
    } else if (!strncmp (tag, api_fdopen_write, strlen (api_fdopen_write))) {
        char *q = tag + strlen (api_fdopen_write);
        _cfd_create (p, c, q);
            
    /* route other */
    } else {
        char *dot, *short_tag = xstrdup (tag);
        if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* env delimiter */
            err_exit ("zmsg_pushmem");
        if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
            err_exit ("zmsg_pushmem");
        if (zmsg_send (&zmsg, p->zs_req) < 0)
            err_exit ("zmsg_send");

        /* set up disconnect notification */
        if ((dot = strchr (short_tag, '.')))
            *dot = '\0';
        if (zhash_lookup (c->disconnect_notify, short_tag) == NULL) {
            if (zhash_insert (c->disconnect_notify, short_tag, short_tag) < 0)
                err_exit ("zhash_insert");
            zhash_freefn (c->disconnect_notify, short_tag, free);
        } else
            free (short_tag);
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);
    return 0;
}

/* Handle response
 */
static void _readmsg_req (plugin_ctx_t *p, void *socket)
{
    ctx_t *ctx = p->ctx;
    zmsg_t *zmsg = NULL;
    char *uuid = NULL;
    zframe_t *zf = NULL;
    client_t *c;

    zmsg = zmsg_recv (socket);
    if (!zmsg) {
        err ("zmsg_recv");
        goto done;
    }
    /* Strip off request header.  In the response direction, each hop
     * strips off their address.  This is the final hop where the address
     * is the uuid of the AF_UNIX connection who made the request.
     * Strip the (empty) message  delimiter and return only the response
     * to the client.
     */
    uuid = zmsg_popstr (zmsg);
    if (!uuid) {
        msg ("apisrv: bad request envelope: no last address part");
        goto done;
    }
    zf = zmsg_pop (zmsg);
    if (!zf || zframe_size (zf) != 0)
        msg ("apisrv: bad request envelope: no delimiter");

    /* Locate the client with the specified uuid */
    for (c = ctx->clients; c != NULL && zmsg != NULL; ) {
        client_t *deleteme = NULL;

        if (!strcmp (uuid, c->uuid)) {
            if (zmsg_send_fd (c->fd, &zmsg) < 0) {
                zmsg_destroy (&zmsg);
                deleteme = c; 
            }
        }
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }
done:
    if (zmsg) {
        //msg ("apisrv: discarding message for: %s (not found)", uuid);
        zmsg_destroy (&zmsg);
    }
    if (zf)
        zframe_destroy (&zf);
    if (uuid)
        free (uuid);
}

static void _readmsg (plugin_ctx_t *p, void *socket)
{
    ctx_t *ctx = p->ctx;
    zmsg_t *zmsg = NULL;
    client_t *c;
    cfd_t *cfd;

    zmsg = zmsg_recv (socket);
    if (!zmsg) {
        err ("zmsg_recv");
        goto done;
    }

    /* send it to all API clients whose subscription matches */
    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;
        client_t *deleteme = NULL;

        if (c->subscription) {
            if (c->subscription_exact ? cmb_msg_match (zmsg, c->subscription)
                         : cmb_msg_match_substr (zmsg, c->subscription, NULL)) {
                if (!(cpy = zmsg_dup (zmsg)))
                    oom ();
                if (zmsg_send_fd (c->fd, &cpy) < 0) {
                    zmsg_destroy (&cpy);
                    deleteme = c; 
                }
            }
        }
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }
    /* also look for matches on any open client fds */
    for (c = ctx->clients; c != NULL; c = c->next) {
        for (cfd = c->cfds; cfd != NULL; ) {
            cfd_t *deleteme = NULL;

            if (cmb_msg_match (zmsg, cfd->name)) {
                if (_cfd_write (cfd, zmsg) < 0)
                    deleteme = cfd;
            }
            cfd = cfd->next;
            if (deleteme)
                _cfd_destroy (p, c, deleteme);
        }
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
}

static void _poll_once (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
    cfd_t *cfd;
    int zpa_len = _client_count (p) + _cfd_count (p) + 4;
    zmq_pollitem_t *zpa = xzmalloc (sizeof (zmq_pollitem_t) * zpa_len);
    int i;

    /* zmq sockets */
    zpa[0].socket = p->zs_in;
    zpa[0].events = ZMQ_POLLIN;
    zpa[0].fd = -1;
    zpa[1].socket = p->zs_in_event;
    zpa[1].events = ZMQ_POLLIN;
    zpa[1].fd = -1;
    zpa[2].socket = p->zs_req;
    zpa[2].events = ZMQ_POLLIN;
    zpa[2].fd = -1;
    zpa[3].events = ZMQ_POLLIN | ZMQ_POLLERR;
    zpa[3].fd = ctx->listen_fd;
    /* client fds */ 
    for (i = 4, c = ctx->clients; c != NULL; c = c->next) {
        for (cfd = c->cfds; cfd != NULL; cfd = cfd->next, i++) {
            zpa[i].events = ZMQ_POLLERR;
            zpa[i].fd = cfd->fd;
            if (cfd->wname)
                zpa[i].events |= ZMQ_POLLIN;
        }
    }
    /* clients */
    for (c = ctx->clients; c != NULL; c = c->next, i++) {
        zpa[i].events = ZMQ_POLLIN | ZMQ_POLLERR;
        zpa[i].fd = c->fd;
    }
    assert (i == zpa_len);

    zpoll (zpa, zpa_len, -1);

    /* client fds */
    for (i = 4, c = ctx->clients; c != NULL; c = c->next) {
        for (cfd = c->cfds; cfd != NULL; i++) {
            cfd_t *deleteme = NULL;
            assert (cfd->fd == zpa[i].fd);
            if (zpa[i].revents & ZMQ_POLLIN) {
                while (_cfd_read (p, cfd) != -1)
                    ;
                if (errno != EWOULDBLOCK)
                    deleteme = cfd;
            }
            if (zpa[i].revents & ZMQ_POLLERR)
                deleteme = cfd;
            cfd = cfd->next;
            if (deleteme)
                _cfd_destroy (p, c, deleteme);
        }
    }
    /* clients - can modify client fds list (so do after client fds) */
    for (c = ctx->clients; c != NULL; i++) {
        client_t *deleteme = NULL;
        assert (c->fd == zpa[i].fd);
        if (zpa[i].revents & ZMQ_POLLIN) {
            while (_client_read (p, c) != -1)
                ;
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                deleteme = c;
        }
        if (zpa[i].revents & ZMQ_POLLERR)
            deleteme = c;
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }

    /* zmq sockets - can modify client list (so do after clients) */
    if (zpa[3].revents & ZMQ_POLLIN)
        _accept (p);
    if (zpa[3].revents & ZMQ_POLLERR)
        err_exit ("apisrv: poll on listen fd");
    if (zpa[0].revents & ZMQ_POLLIN)
        _readmsg (p, p->zs_in);
    if (zpa[1].revents & ZMQ_POLLIN)
        _readmsg (p, p->zs_in_event);
    if (zpa[2].revents & ZMQ_POLLIN)
        _readmsg_req (p, p->zs_req);

    free (zpa);
}

static void _listener_init (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    struct sockaddr_un addr;
    int fd;
    char *path = p->conf->apisockpath;

    fd = socket (AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0)
        err_exit ("socket");
    if (remove (path) < 0 && errno != ENOENT)
        err_exit ("remove %s", path);
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (bind (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un)) < 0)
        err_exit ("bind");
    if (listen (fd, LISTEN_BACKLOG) < 0)
        err_exit ("listen");

    ctx->listen_fd = fd;
}

static void _listener_fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    if (close (ctx->listen_fd) < 0)
        err_exit ("listen");
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    p->ctx = ctx = xzmalloc (sizeof (ctx_t));
    ctx->clients = NULL;

    //zsocket_set_subscribe (p->zs_in, "");
    zsocket_set_subscribe (p->zs_in_event, "");

    _listener_init (p);
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    _listener_fini (p);
    while (ctx->clients != NULL)
        _client_destroy (p, ctx->clients);
    free (ctx);
}

static void _poll (plugin_ctx_t *p)
{
    for (;;)
        _poll_once (p);
}

struct plugin_struct apisrv = {
    .name   = "api",
    .initFn = _init,
    .finiFn = _fini,
    .pollFn = _poll,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
