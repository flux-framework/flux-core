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
#include <json/json.h>

#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

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

static client_t * _client_create (plugin_ctx_t *p, int fd)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->fd = fd;
    c->uuid = uuid_generate_str ();
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
    return (c);
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
    o = util_json_object_new_object ();
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

static int _client_read (plugin_ctx_t *p, client_t *c);

static int client_cb (zloop_t *zl, zmq_pollitem_t *zp, client_t *c)
{
    plugin_ctx_t *p = c->p;
    bool delete = false;

    if (zp->revents & ZMQ_POLLIN) {
        while (_client_read (p, c) != -1)
            ;
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            delete = true;
    }
    if (zp->revents & ZMQ_POLLERR)
        delete = true;

    if (delete) {
        /*  Cancel this client's fd from the zloop and destroy client
         */
        zloop_poller_end (zl, zp);
        _client_destroy (c->p, c);
    }
    return (0);
}

static void _accept (plugin_ctx_t *p)
{
    client_t *c;
    int fd;
    ctx_t *ctx = p->ctx;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN | ZMQ_POLLERR };

    fd = accept (ctx->listen_fd, NULL, NULL); 
    if (fd < 0)
        err_exit ("accept");
    c = _client_create (p, fd);

    /*
     *  Add this client's fd to the plugin's event loop:
     */
    zp.fd = fd;
    zloop_poller (p->zloop, &zp, (zloop_fn *) client_cb, (void *) c);
}

static int _client_read (plugin_ctx_t *p, client_t *c)
{
    zmsg_t *zmsg = NULL;
    char *name = NULL;

    zmsg = zmsg_recv_fd (c->fd, true);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            err ("API read");
        return -1;
    }
        
    if (cmb_msg_match (zmsg, "api.snoop.on")) {
        c->snoop = true;
        zsocket_set_subscribe (p->zs_snoop, "");
    } else if (cmb_msg_match (zmsg, "api.snoop.off")) {
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
    } else if (cmb_msg_match (zmsg, "api.session.info.query")) {
        json_object *o = util_json_object_new_object ();
        util_json_object_add_int (o, "rank", p->conf->rank);
        util_json_object_add_int (o, "size", p->conf->size);
        if (cmb_msg_replace_json (zmsg, o) == 0)
            (void)zmsg_send_fd (c->fd, &zmsg);
        json_object_put (o);
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

        if (!strcmp (uuid, c->uuid)) {
            if (zmsg_send_fd (c->fd, zmsg) < 0)
                zmsg_destroy (zmsg);
            break;
        }
        c = c->next;
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

        if (zhash_foreach (c->subscriptions, _match_subscription, *zmsg)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0)
                zmsg_destroy (&cpy);
        }
        c = c->next;
    }
}

static void _recv_snoop (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;

        if (c->snoop) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0)
                zmsg_destroy (&cpy);
        }
        c = c->next;
    }
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    switch (type) {
        case ZMSG_REQUEST:
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

static int accept_cb (zloop_t *zl, zmq_pollitem_t *zp, plugin_ctx_t *p)
{
    if (zp->revents & ZMQ_POLLIN)        /* listenfd */
        _accept (p);
    if (zp->revents & ZMQ_POLLERR)       /* listenfd - error */
        err_exit ("apisrv: poll on listen fd");
    return (0);
}

static void _listener_init (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    struct sockaddr_un addr;
    int fd;
    char *path = p->conf->api_sockpath;

    if (setenv ("CMB_API_PATH", path, 1) < 0)
        err_exit ("setenv (CMB_API_PATH=%s)", path);

    fd = socket (AF_UNIX, SOCK_STREAM, 0);
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
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN | ZMQ_POLLERR };

    p->ctx = ctx = xzmalloc (sizeof (ctx_t));
    ctx->clients = NULL;

    _listener_init (p);

    /*
     *  Add listen fd to zloop reactor:
     */
    zp.fd = ctx->listen_fd;
    zloop_poller (p->zloop, &zp, (zloop_fn *) accept_cb, (void *) p);
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    _listener_fini (p);
    while (ctx->clients != NULL)
        _client_destroy (p, ctx->clients);
    free (ctx);
}

struct plugin_struct apisrv = {
    .name   = "api",
    .recvFn = _recv,
    .initFn = _init,
    .finiFn = _fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
