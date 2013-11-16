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

struct _client_struct;

typedef struct {
    int listen_fd;
    struct _client_struct *clients;
    flux_t h;
} ctx_t;

typedef struct _client_struct {
    int fd;
    struct _client_struct *next;
    struct _client_struct *prev;
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zhash_t *event_subscriptions;
    zhash_t *snoop_subscriptions;
    char *uuid;
    int cfd_id;
} client_t;

static void freectx (ctx_t *ctx)
{
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "apisrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        flux_aux_set (h, "apisrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static client_t * client_create (ctx_t *ctx, int fd)
{
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->fd = fd;
    c->uuid = uuid_generate_str ();
    c->ctx = ctx;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    if (!(c->event_subscriptions = zhash_new ()))
        oom ();
    if (!(c->snoop_subscriptions = zhash_new ()))
        oom ();
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
    return (c);
}

static int _event_unsubscribe (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    if (flux_event_unsubscribe (c->ctx->h, key) < 0)
        err_exit ("%s: flux_event_unsubscribe", __FUNCTION__);
    return 0;
}

static int _snoop_unsubscribe (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    if (flux_snoop_unsubscribe (c->ctx->h, key) < 0)
        err_exit ("%s: flux_snoop_unsubscribe", __FUNCTION__);
    return 0;
}

static int notify_srv (const char *key, void *item, void *arg)
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

    flux_request_sendmsg (c->ctx->h, &zmsg);

    return 0;
}

static void client_destroy (ctx_t *ctx, client_t *c)
{
    zhash_foreach (c->disconnect_notify, notify_srv, c);
    zhash_destroy (&c->disconnect_notify);

    zhash_foreach (c->event_subscriptions, _event_unsubscribe, c);
    zhash_destroy (&c->event_subscriptions);

    zhash_foreach (c->snoop_subscriptions, _snoop_unsubscribe, c);
    zhash_destroy (&c->snoop_subscriptions);

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

static int client_read (ctx_t *ctx, client_t *c)
{
    zmsg_t *zmsg = NULL;
    char *name = NULL;

    zmsg = zmsg_recv_fd (c->fd, true);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            err ("API read");
        return -1;
    }
        
    if (cmb_msg_match_substr (zmsg, "api.snoop.subscribe.", &name)) {
        zhash_insert (c->snoop_subscriptions, name, name);
        zhash_freefn (c->snoop_subscriptions, name, free);
        if (flux_snoop_subscribe (ctx->h, name) < 0)
            err_exit ("%s: flux_snoop_subscribe", __FUNCTION__);
        name = NULL;
    } else if (cmb_msg_match_substr (zmsg, "api.snoop.unsubscribe.", &name)) {
        if (zhash_lookup (c->snoop_subscriptions, name)) {
            zhash_delete (c->snoop_subscriptions, name);
            if (flux_snoop_unsubscribe (ctx->h, name) < 0)
                err_exit ("%s: flux_snoop_unsubscribe", __FUNCTION__);
        }
    } else if (cmb_msg_match_substr (zmsg, "api.event.subscribe.", &name)) {
        zhash_insert (c->event_subscriptions, name, name);
        zhash_freefn (c->event_subscriptions, name, free);
        if (flux_event_subscribe (ctx->h, name) < 0)
            err_exit ("%s: flux_event_subscribe", __FUNCTION__);
        name = NULL;
    } else if (cmb_msg_match_substr (zmsg, "api.event.unsubscribe.", &name)) {
        if (zhash_lookup (c->event_subscriptions, name)) {
            zhash_delete (c->event_subscriptions, name);
            if (flux_event_unsubscribe (ctx->h, name) < 0)
                err_exit ("%s: flux_event_unsubscribe", __FUNCTION__);
        }
    } else if (cmb_msg_match_substr (zmsg, "api.event.send.", &name)) {
        json_object *o;
        if (cmb_msg_decode (zmsg, NULL, &o) < 0)
            err_exit ("%s: cmb_msg_decode", __FUNCTION__);
        if (flux_event_send (ctx->h, o, "%s", name) < 0)
            err_exit ("flux_event_send");
        if (o)
            json_object_put (o);
    } else if (cmb_msg_match (zmsg, "api.session.info.query")) {
        json_object *o = util_json_object_new_object ();
        zframe_t *zf = zmsg_pop (zmsg);
        assert (zf != NULL);
        assert (zframe_size (zf) == 0);
        zframe_destroy (&zf);
        util_json_object_add_int (o, "rank", flux_rank (ctx->h));
        util_json_object_add_int (o, "size", flux_size (ctx->h));
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
        if (zmsg_pushstr (zmsg, "%s", c->uuid) < 0)
            err_exit ("zmsg_pushmem");
        flux_request_sendmsg (ctx->h, &zmsg);
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (name)
        free (name);
    return 0;
}

static int client_cb (zloop_t *zl, zmq_pollitem_t *zp, client_t *c)
{
    ctx_t *ctx = c->ctx;
    bool delete = false;

    if (zp->revents & ZMQ_POLLIN) {
        while (client_read (ctx, c) != -1)
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
        client_destroy (ctx, c);
    }
    return (0);
}

static void recv_response (ctx_t *ctx, zmsg_t **zmsg)
{
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

static int match_subscription (const char *key, void *item, void *arg)
{
    return cmb_msg_match_substr ((zmsg_t *)arg, key, NULL) ? 1 : 0;
}

static void recv_event (ctx_t *ctx, zmsg_t **zmsg)
{
    client_t *c;

    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;

        if (zhash_foreach (c->event_subscriptions, match_subscription, *zmsg)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0)
                zmsg_destroy (&cpy);
        }
        c = c->next;
    }
}

static void recv_snoop (ctx_t *ctx, zmsg_t **zmsg)
{
    client_t *c;

    for (c = ctx->clients; c != NULL; ) {
        zmsg_t *cpy;

        if (zhash_foreach (c->snoop_subscriptions, match_subscription, *zmsg)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            if (zmsg_send_fd (c->fd, &cpy) < 0)
                zmsg_destroy (&cpy);
        }
        c = c->next;
    }
}

static void apisrv_recv (flux_t h, zmsg_t **zmsg, zmsg_type_t type)
{
    ctx_t *ctx = getctx (h);

    switch (type) {
        case ZMSG_REQUEST:
            break;
        case ZMSG_EVENT:
            recv_event (ctx, zmsg);
            break;
        case ZMSG_RESPONSE:
            recv_response (ctx, zmsg);
            break;
        case ZMSG_SNOOP:
            recv_snoop (ctx, zmsg);
            break;
    }
}

static int listener_cb (zloop_t *zl, zmq_pollitem_t *zp, ctx_t *ctx)
{
    if (zp->revents & ZMQ_POLLIN) {       /* listenfd */
        zloop_t *zloop = flux_get_zloop (ctx->h);
        zmq_pollitem_t nzp = { .events = ZMQ_POLLIN | ZMQ_POLLERR };
        client_t *c;

        if ((nzp.fd = accept (ctx->listen_fd, NULL, NULL)) < 0)
            err_exit ("accept");
        c = client_create (ctx, nzp.fd);
        zloop_poller (zloop, &nzp, (zloop_fn *) client_cb, (void *) c);
    }
    if (zp->revents & ZMQ_POLLERR)       /* listenfd - error */
        err_exit ("apisrv: poll on listen fd");
    return (0);
}

static int listener_init (ctx_t *ctx)
{
    struct sockaddr_un addr;
    int fd;
    char *path = getenv ("CMB_API_PATH"); /* set by cmbd after getopt */

    if (!path)
        msg_exit ("CMB_API_PATH is not set");
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

    return fd;
}

static void apisrv_init (flux_t h)
{
    ctx_t *ctx = getctx (h);
    zloop_t *zloop = flux_get_zloop (h);
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN | ZMQ_POLLERR };

    zp.fd = ctx->listen_fd = listener_init (ctx);
    zloop_poller (zloop, &zp, (zloop_fn *) listener_cb, (void *)ctx);
}

static void apisrv_fini (flux_t h)
{
    ctx_t *ctx = getctx (h);

    if (close (ctx->listen_fd) < 0)
        err_exit ("listen");
    while (ctx->clients != NULL)
        client_destroy (ctx, ctx->clients);
}

const struct plugin_ops apisrv = {
    .name   = "api",
    .recv = apisrv_recv,
    .init = apisrv_init,
    .fini = apisrv_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
