/* apisrv.c - bridge unix domain API socket and zmq message broker */

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
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "apisrv.h"

#define LISTEN_BACKLOG      5

typedef struct _client_struct {
    int fd;
    struct _client_struct *next;
    struct _client_struct *prev;
    plugin_ctx_t *p;
    zhash_t *disconnect_notify;
    zhash_t *subscriptions;
    bool snoop;
    char *uuid;
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

static void _client_create (plugin_ctx_t *p, int fd)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->fd = fd;
    c->uuid = _uuid_generate ();
    c->p = p;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    if (!(c->subscriptions = zhash_new ()))
        oom ();
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
}

static int _unsubscribe (const char *key, void *item, void *arg)
{
    client_t *c = arg;

    /* FIXME: this assumes zmq subscriptions have use counts (verify this) */
    zsocket_set_unsubscribe (c->p->zs_evin, (char *)key);
    return 0;
}

static int _notify_srv (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    zmsg_t *zmsg; 
    json_object *o;

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (!(o = json_object_new_object ()))
        oom ();
    if (zmsg_pushstr (zmsg, "%s", json_object_to_json_string (o)) < 0)
        err_exit ("zmsg_pushstr");
    json_object_put (o);
    if (zmsg_pushstr (zmsg, "%s.disconnect", key) < 0)
        err_exit ("zmsg_pushstr");
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* delimiter frame */
        err_exit ("zmsg_pushmem");
    if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
        err_exit ("zmsg_pushmem");

    plugin_send_request_raw (c->p, &zmsg);

    return 0;
}

static void _client_destroy (plugin_ctx_t *p, client_t *c)
{
    ctx_t *ctx = p->ctx;

    zhash_foreach (c->disconnect_notify, _notify_srv, c);
    zhash_destroy (&c->disconnect_notify);

    zhash_foreach (c->subscriptions, _unsubscribe, c);
    zhash_destroy (&c->subscriptions);

    if (c->snoop)
        zsocket_set_unsubscribe (p->zs_snoop, "");

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
    zmsg_t *zmsg = NULL;
    char *name = NULL;

    zmsg = zmsg_recv_fd (c->fd, MSG_DONTWAIT);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            err ("API read");
        return -1;
    }
        
    if (cmb_msg_match (zmsg, "api.snoop.on")) {
        c->snoop = true;
        zsocket_set_subscribe (p->zs_snoop, "");
    } else if (cmb_msg_match (zmsg, "api.snoop.on")) {
        c->snoop = false;
        zsocket_set_unsubscribe (p->zs_snoop, "");
    } else if (cmb_msg_match_substr (zmsg, "api.event.subscribe.", &name)) {
        zhash_insert (c->subscriptions, name, name);
        zhash_freefn (c->subscriptions, name, free);
        zsocket_set_subscribe (p->zs_evin, name);
        name = NULL;
    } else if (cmb_msg_match_substr (zmsg, "api.event.unsubscribe.", &name)) {
        if (zhash_lookup (c->subscriptions, name)) {
            zhash_delete (c->subscriptions, name);
            zsocket_set_unsubscribe (p->zs_evin, name);
        }
    } else if (cmb_msg_match_substr (zmsg, "api.event.send.", &name)) {
        plugin_send_event (p, "%s", name);
    } else {
        /* insert disconnect notifier before forwarding request */
        if (c->disconnect_notify) {
            char *tag = cmb_msg_tag (zmsg, true); /* first component only */
            if (!tag)
                goto done;
            if (zhash_lookup (c->disconnect_notify, tag) == NULL) {
                if (zhash_insert (c->disconnect_notify, tag, tag) < 0)
                    err_exit ("zhash_insert");
                zhash_freefn (c->disconnect_notify, tag, free);
            } else
                free (tag);
        }
        if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* env delimiter */
            err_exit ("zmsg_pushmem");
        if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
            err_exit ("zmsg_pushmem");
        plugin_send_request_raw (p, &zmsg);
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (name)
        free (name);
    return 0;
}

static void _recv_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    char *uuid = NULL;
    zframe_t *zf = NULL;
    client_t *c;

    if (zmsg_hopcount (*zmsg) != 1) {
        msg ("apisrv: ignoring response with bad envelope");
        return;
    }
    uuid = zmsg_popstr (*zmsg);
    assert (uuid != NULL);
    zf = zmsg_pop (*zmsg);
    assert (zf != NULL);
    assert (zframe_size (zf) == 0);

    for (c = ctx->clients; c != NULL && *zmsg != NULL; ) {
        client_t *deleteme = NULL;

        if (!strcmp (uuid, c->uuid)) {
            if (zmsg_send_fd (c->fd, zmsg) < 0) {
                zmsg_destroy (zmsg);
                deleteme = c; 
            }
        }
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }
    if (*zmsg) {
        //msg ("apisrv: discarding response for unknown uuid %s", uuid);
        zmsg_destroy (zmsg);
    }
    if (zf)
        zframe_destroy (&zf);
    if (uuid)
        free (uuid);
}

static int _match_subscription (const char *key, void *item, void *arg)
{
    return cmb_msg_match_substr ((zmsg_t *)arg, key, NULL) ? 1 : 0;
}

static void _recv_event (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;
        client_t *deleteme = NULL;

        if (zhash_foreach (c->subscriptions, _match_subscription, *zmsg)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0) {
                zmsg_destroy (&cpy);
                deleteme = c; 
            }
        }
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }
}

static void _recv_snoop (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;
        client_t *deleteme = NULL;

        if (c->snoop) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0) {
                zmsg_destroy (&cpy);
                deleteme = c; 
            }
        }
        c = c->next;
        if (deleteme)
            _client_destroy (p, deleteme);
    }
}

/* N.B. local api can't send to api, so "cmbutil [-p|-x] api" will get ENOSYS.
 * Use fully qualified names like "cmbutil [-p|-x] N!api".
 */
static void _recv_request (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (cmb_msg_match (*zmsg, "api.ping")) {
        plugin_ping_respond (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "api.stats")) {
        plugin_stats_respond (p, zmsg);
    }
}


static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    switch (type) {
        case ZMSG_REQUEST:
            _recv_request (p, zmsg);
            break;
        case ZMSG_EVENT:
            _recv_event (p, zmsg);
            break;
        case ZMSG_RESPONSE:
            _recv_response (p, zmsg);
            break;
        case ZMSG_SNOOP:
            _recv_snoop (p, zmsg);
            break;
    }
}

static void _poll_once (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
    int zpa_len = _client_count (p) + 5;
    zmq_pollitem_t *zpa = xzmalloc (sizeof (zmq_pollitem_t) * zpa_len);
    int i;
    zmsg_t *zmsg;
    zmsg_type_t type;

    /* zmq sockets */
    zpa[0].socket = p->zs_dnreq;
    zpa[0].events = ZMQ_POLLIN;
    zpa[0].fd = -1;
    zpa[1].socket = p->zs_evin;
    zpa[1].events = ZMQ_POLLIN;
    zpa[1].fd = -1;
    zpa[2].socket = p->zs_upreq;
    zpa[2].events = ZMQ_POLLIN;
    zpa[2].fd = -1;
    zpa[3].socket = p->zs_snoop;
    zpa[3].events = ZMQ_POLLIN;
    zpa[3].fd = -1;

    /* listen fd */
    zpa[4].events = ZMQ_POLLIN | ZMQ_POLLERR;
    zpa[4].fd = ctx->listen_fd;

    /* clients */
    for (i = 5, c = ctx->clients; c != NULL; c = c->next, i++) {
        zpa[i].events = ZMQ_POLLIN | ZMQ_POLLERR;
        zpa[i].fd = c->fd;
    }
    assert (i == zpa_len);

    if ((zmq_poll (zpa, zpa_len, -1)) < 0)
        err_exit ("zmq_poll");

    /* clients */
    for (i = 5, c = ctx->clients; c != NULL; i++) {
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

    /* accept new client connection */
    if (zpa[4].revents & ZMQ_POLLIN)        /* listenfd */
        _accept (p);
    if (zpa[4].revents & ZMQ_POLLERR)       /* listenfd - error */
        err_exit ("apisrv: poll on listen fd");

    /* zmq sockets - can modify client list (so do after clients) */
    if (zpa[0].revents & ZMQ_POLLIN) {      /* request on 'dnreq' */
        zmsg = zmsg_recv (p->zs_dnreq);
        if (!zmsg)
            err ("zmsg_recv");
        type = ZMSG_REQUEST;
        p->stats.dnreq_recv_count++;
    } else if (zpa[1].revents & ZMQ_POLLIN) {/* event on 'evin' */
        zmsg = zmsg_recv (p->zs_evin);
        if (!zmsg)
            err ("zmsg_recv");
        type = ZMSG_EVENT;
        p->stats.event_recv_count++;
    } else if (zpa[2].revents & ZMQ_POLLIN) {/* response on 'upreq' */
        zmsg = zmsg_recv (p->zs_upreq);
        if (!zmsg)
            err ("zmsg_recv");
        type = ZMSG_RESPONSE;
        p->stats.upreq_recv_count++;
    } else if (zpa[3].revents & ZMQ_POLLIN) {/* 'snoop' */
        zmsg = zmsg_recv (p->zs_snoop);
        if (!zmsg)
            err ("zmsg_recv");
        type = ZMSG_SNOOP;
    } else
        zmsg = NULL;

    if (zmsg)
        _recv (p, &zmsg, type);
    if (zmsg && type == ZMSG_REQUEST)
        plugin_send_response_errnum (p, &zmsg, ENOSYS);

    free (zpa);
}

static void _listener_init (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    struct sockaddr_un addr;
    int fd;
    char *path = p->conf->api_sockpath;

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
