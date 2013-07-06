/* confsrv.c - another key value store */

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
#include <sys/time.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "confsrv.h"

typedef struct {
    zhash_t *clients;       /* client identity -> client req message */
    const char *val;
    plugin_ctx_t *p;
} request_t;

typedef struct {
    zhash_t *store;
    int store_version;
    zhash_t *requests;      /* key -> request struct */
} ctx_t;

static request_t *_request_create (plugin_ctx_t *p)
{
    request_t *req = xzmalloc (sizeof (request_t));
    if (!(req->clients = zhash_new ()))
        oom ();
    req->p = p;
    return req;
}

static void _request_destroy (request_t *req)
{
    zhash_destroy (&req->clients);
}

static void _free_zmsg (void *item)
{
    zmsg_destroy ((zmsg_t **)&item);
}

static void _conf_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o = NULL;
    char *sender = NULL;
    const char *key, *val;
    request_t *req;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || !(sender = cmb_msg_sender (*zmsg))
            || util_json_object_get_string (o, "key", &key) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    /* handle request out of cache? */
    if ((val = zhash_lookup (ctx->store, key)) != NULL) {
        util_json_object_add_string (o, "val", val);
        plugin_send_response (p, zmsg, o);

    /* we are the root - if key is not found, return that to client */
    } else if (plugin_treeroot (p)) {
        plugin_send_response (p, zmsg, o);

    /* request pending, add client request message */
    } else if ((req = zhash_lookup (ctx->requests, key))) {
        zhash_update (req->clients, sender, *zmsg);
        zhash_freefn (req->clients, sender, _free_zmsg);
        *zmsg = NULL;

    /* send request, and create pending state */
    } else {
        req = _request_create (p);
        zhash_update (req->clients, sender, *zmsg);
        zhash_freefn (req->clients, sender, _free_zmsg);
        *zmsg = NULL;
        zhash_update (ctx->requests, key, req);
        zhash_freefn (req->clients, sender, (zhash_free_fn *)_request_destroy);
        plugin_send_request (p, o, "conf.get");
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

static void _conf_put (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o = NULL;
    char *sender = NULL;
    const char *key, *val;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || !(sender = cmb_msg_sender (*zmsg))
            || util_json_object_get_string (o, "key", &key) < 0
            || util_json_object_get_string (o, "val", &val) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    /* we are the root, perform update */
    if (plugin_treeroot (p)) {
        zhash_update (ctx->store, key, xstrdup (val));
        zhash_freefn (ctx->store, key, (zhash_free_fn *) free);
        plugin_send_response_errnum (p, zmsg, 0);
        plugin_send_event (p, "event.conf.commit.%d", ++ctx->store_version);

    /* recurse */
    } else {
        /* FIXME */
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

static int _conf_get_respond_one (const char *key, void *item, void *arg)
{
    zmsg_t *cpy, *zmsg = item;
    request_t *req = arg;
    json_object *o;

    if (cmb_msg_decode (zmsg, NULL, &o) == 0 && o != NULL) {
        util_json_object_add_string (o, "val", req->val);
        cpy = zmsg_dup (zmsg);
        plugin_send_response (req->p, &cpy, o);
    }
    return 0;
}

static void _conf_get_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o = NULL;
    const char *key, *val = NULL;
    request_t *req;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "key", &key) < 0)
        goto done;
    if (!(req = zhash_lookup (ctx->requests, key)))
        goto done;
    (void)util_json_object_get_string (o, "val", &val);
    if (val) {
        zhash_update (ctx->store, key, xstrdup (val));    
        zhash_freefn (ctx->store, key, (zhash_free_fn *)free);
    }
    req->val = val;
    zhash_foreach (req->clients, _conf_get_respond_one, req);
    zhash_delete (ctx->requests, key);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg); 
}

static void _conf_set_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    /* FIXME */
}

static void _conf_disconnect (plugin_ctx_t *p, zmsg_t **zmsg)
{
    /* FIXME */
}

static void _event_conf_commit (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int new_version = strtoul (arg, NULL, 10);

    if (ctx->store_version != new_version) {
        zhash_destroy (&ctx->store);
        if (!(ctx->store = zhash_new ()))
            oom ();
        ctx->store_version = new_version;
    }
}

/* Define plugin entry points.
 */

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;

    switch (type) {
        case ZMSG_REQUEST:
            if (cmb_msg_match (*zmsg, "conf.get"))
                _conf_get (p, zmsg);
            else if (cmb_msg_match (*zmsg, "conf.put"))
                _conf_put (p, zmsg);
            else if (cmb_msg_match (*zmsg, "conf.disconnect"))
                _conf_disconnect (p, zmsg);
            break;
        case ZMSG_EVENT:
            if (cmb_msg_match_substr (*zmsg, "event.conf.commit.", &arg))
                _event_conf_commit (p, arg, zmsg);
            break;
        case ZMSG_RESPONSE:
            if (cmb_msg_match (*zmsg, "conf.get"))
                _conf_get_response (p, zmsg);
            else if (cmb_msg_match (*zmsg, "conf.set"))
                _conf_set_response (p, zmsg);
            break;
        case ZMSG_SNOOP:
            break;
    }

    if (arg)
        free (arg);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    zsocket_set_subscribe (p->zs_evin, "event.conf.");
    if (!(ctx->store = zhash_new ()))
        oom ();
    if (!(ctx->requests = zhash_new ()))
        oom ();
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zhash_destroy (&ctx->store);
    zhash_destroy (&ctx->requests);
    free (ctx);
}

struct plugin_struct confsrv = {
    .name      = "conf",
    .initFn    = _init,
    .finiFn    = _fini,
    .recvFn    = _recv,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
