/* kvssrv.c - key-value service */ 

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
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>
#include <hiredis/hiredis.h>

#include "zmq.h"
#include "cmb.h"
#include "route.h"
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "kvssrv.h"

typedef struct _kv_struct {
    char *key;
    char *val;
    struct _kv_struct *next;
    struct _kv_struct *prev;
} kv_t;

typedef struct _client_struct {
    struct _client_struct *next;
    struct _client_struct *prev;
    char *identity;
    int putcount;
    int errcount;
    kv_t *put_queue;
} client_t;

typedef struct {
    redisContext *rctx;
    client_t *clients;
} ctx_t;

static void _add_put_queue (client_t *c, const char *key, const char *val)
{
    kv_t *kv = xzmalloc (sizeof (kv_t));

    kv->key = xstrdup (key);
    kv->val = xstrdup (val);

    kv->next = c->put_queue;
    if (kv->next)
        kv->next->prev = kv;
    c->put_queue = kv;
}

static void _flush_put_queue (plugin_ctx_t *p, client_t *c)
{
    ctx_t *ctx = p->ctx;
    kv_t *kp;
    redisReply *rep;
    int replycount = 0;

    for (kp = c->put_queue; kp != NULL; kp = kp->next)
        if (kp->next == NULL)
            break;
    while (kp) {
        if (redisAppendCommand (ctx->rctx, "SET %s %s",
                                            kp->key, kp->val) == REDIS_ERR) {
            c->errcount++; /* FIXME: report error from ctx->rctx? */
        } else {
            replycount++;
        }
        c->putcount++;
        kp = kp->prev;
    }
    while (replycount-- > 0) {
        if (redisGetReply (ctx->rctx, (void **)&rep) == REDIS_ERR) {
            c->errcount++;
        } else {
            switch (rep->type) {
                case REDIS_REPLY_STATUS:
                    /* success */
                    break;
                case REDIS_REPLY_ERROR:
                    c->errcount++;
                    break;
                case REDIS_REPLY_INTEGER:
                case REDIS_REPLY_NIL:
                case REDIS_REPLY_STRING:
                case REDIS_REPLY_ARRAY:
                    msg ("redisCommand: unexpected reply type");
                    c->errcount++;
                    break;
            }
            freeReplyObject (rep);
        }
    }
    while (c->put_queue) {
        kp = c->put_queue;
        c->put_queue= kp->next;
        free (kp->key);
        free (kp->val);
        free (kp);
    }
}

static client_t *_client_create (plugin_ctx_t *p, const char *identity)
{
    ctx_t *ctx = p->ctx;
    client_t *c;

    c = xzmalloc (sizeof (client_t));
    c->identity = xstrdup (identity);
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;

    return c;
}

static void _client_destroy (plugin_ctx_t *p, client_t *c)
{   
    ctx_t *ctx = p->ctx;
    kv_t *kp, *deleteme;

    free (c->identity);
    for (kp = c->put_queue; kp != NULL; ) {
        deleteme = kp;
        kp = kp->next; 
        free (deleteme->key);
        free (deleteme->val);
        free (deleteme);
    }

    if (c->prev)
        c->prev->next = c->next;
    else
        ctx->clients = c->next;
    if (c->next)
        c->next->prev = c->prev;
    free (c);
}   

static client_t *_client_find (plugin_ctx_t *p, const char *identity)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
 
    for (c = ctx->clients; c != NULL; c = c->next) {
        if (!strcmp (c->identity, identity))
            return c;
    }
    return NULL;
}

static char *_redis_get (plugin_ctx_t *p, const char *key)
{
    ctx_t *ctx = p->ctx;
    redisReply *rep;
    char *val = NULL;

    rep = redisCommand (ctx->rctx, "GET %s", key);
    if (rep == NULL) {
        msg ("redisCommand: %s", ctx->rctx->errstr);
        return NULL; /* FIXME: rctx cannot be reused */
    }
    switch (rep->type) {
        case REDIS_REPLY_ERROR:
            assert (rep->str != NULL);
            msg ("redisCommand: error reply: %s", rep->str);
            break;
        case REDIS_REPLY_NIL:
            /* invalid key */ 
            break;
        case REDIS_REPLY_STRING:
            /* success */
            val = xstrdup (rep->str);
            break;
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
        case REDIS_REPLY_ARRAY:
            msg ("redisCommand: unexpected reply type (%d)", rep->type);
            break;
    }
    if (rep)
        freeReplyObject (rep);
    return val;
}

/* kvs.put just queues up key-val pairs.  There is no reply.
 * FIXME: auto-flush after some threshold to avoid DoS.
 */
static void _kvs_put (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    const char *key, *val;
    char *sender = NULL;
    client_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (o == NULL || !(sender = cmb_msg_sender (*zmsg))
                  || util_json_object_get_string (o, "key", &key) < 0
                  || util_json_object_get_string (o, "val", &val) < 0) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    if (!(c = _client_find (p, sender)))
        c = _client_create (p, sender);
    _add_put_queue (c, key, val);
done:
    zmsg_destroy (zmsg);
    if (o)
        json_object_put (o);
    if (sender)
        free (sender);
}

static void _kvs_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    const char *key;
    char *val;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (o == NULL || util_json_object_get_string (o, "key", &key) < 0) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    val = _redis_get (p, key);
    if (val) /* omit val in response on error */
        util_json_object_add_string (o, "val", val);
    plugin_send_response (p, zmsg, o);
done:
    if (o)
        json_object_put (o);
    if (val)
        free (val);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _kvs_commit (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    char *sender = NULL;
    int errcount = 0, putcount = 0;
    client_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (o == NULL || !(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    if ((c = _client_find (p, sender))) {
        _flush_put_queue (p, c);
        errcount = c->errcount;
        putcount = c->putcount;
        c->errcount = c->putcount = 0;
    }
    util_json_object_add_int (o, "errcount", errcount);
    util_json_object_add_int (o, "putcount", putcount);
    plugin_send_response (p, zmsg, o);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

static void _kvs_disconnect (plugin_ctx_t *p, zmsg_t **zmsg)
{
    char *sender = NULL;
    client_t *c;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    if ((c = _client_find (p, sender)))
        _client_destroy (p, c);
done:
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    if (cmb_msg_match (*zmsg, "kvs.put"))
        _kvs_put (p, zmsg);
    else if (cmb_msg_match (*zmsg, "kvs.get"))
        _kvs_get (p, zmsg);
    else if (cmb_msg_match (*zmsg, "kvs.commit"))
        _kvs_commit (p, zmsg);
    else if (cmb_msg_match (*zmsg, "kvs.disconnect"))
        _kvs_disconnect (p, zmsg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    int redis_port;
    char *redis_host;

    redis_host = plugin_conf_get_string (p, "kvs.redis.hostname");
    redis_port = plugin_conf_get_int (p, "kvs.redis.port");
    if (redis_port <= 0 || redis_port > 65535)
        msg_exit ("kvs: invalid redis port: %d", redis_port);

    p->ctx = ctx;
retryconnect:
    ctx->rctx = redisConnect (redis_host, redis_port);
    if (ctx->rctx == NULL) {
        err ("redisConnect returned NULL - abort");
        goto done;
    }
    if (ctx->rctx->err == REDIS_ERR_IO && errno == ECONNREFUSED) {
        redisFree (ctx->rctx);
        sleep (2);
        err ("redisConnect: retrying connect");
        goto retryconnect;
    }
    if (ctx->rctx->err) {
        err ("redisConnect: %s", ctx->rctx->errstr);
        redisFree (ctx->rctx);
        ctx->rctx = NULL;
    }
done:
    free (redis_host);
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    if (ctx->rctx)
        redisFree (ctx->rctx);
    while (ctx->clients != NULL)
        _client_destroy (p, ctx->clients);
    free (ctx);
}

struct plugin_struct kvssrv = {
    .name      = "kvs",
    .initFn    = _init,
    .recvFn    = _recv,
    .finiFn    = _fini,
};


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
