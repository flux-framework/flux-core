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
    char *subscription;
    kv_t *set_queue;
} client_t;

typedef struct {
    redisContext *rctx;
    client_t *clients;
} ctx_t;

static void _add_set_queue (client_t *c, const char *key, const char *val)
{
    kv_t *kv = xzmalloc (sizeof (kv_t));

    kv->key = xstrdup (key);
    kv->val = xstrdup (val);

    kv->next = c->set_queue;
    if (kv->next)
        kv->next->prev = kv;
    c->set_queue = kv;
}

static void _flush_set_queue (plugin_ctx_t *p, client_t *c)
{
    ctx_t *ctx = p->ctx;
    kv_t *kp;
    redisReply *rep;
    int replycount = 0;

    for (kp = c->set_queue; kp != NULL; kp = kp->next)
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
    while (c->set_queue) {
        kp = c->set_queue;
        c->set_queue= kp->next;
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
    if (asprintf (&c->subscription, "%s.disconnect", identity) < 0)
        oom ();
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;

    //zsocket_set_subscribe (p->zs_in, c->subscription);

    return c;
}

static void _client_destroy (plugin_ctx_t *p, client_t *c)
{   
    ctx_t *ctx = p->ctx;
    kv_t *kp, *deleteme;

    //zsocket_set_unsubscribe (p->zs_in, c->subscription);

    free (c->identity);
    free (c->subscription);
    for (kp = c->set_queue; kp != NULL; ) {
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

static client_t *_client_find_byidentity (plugin_ctx_t *p, const char *identity)
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
 */
static void _kvs_put (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL, *key, *val;
    char *sender = NULL;
    client_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (!(sender = cmb_msg_sender (*zmsg))
                            || !(key = json_object_object_get (o, "key"))
                            || !(val = json_object_object_get (o, "val"))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    if (!(c = _client_find_byidentity (p, sender)))
        c = _client_create (p, sender);
    _add_set_queue (c, json_object_get_string (key),
                       json_object_get_string (val));
done:
    zmsg_destroy (zmsg);
    if (o)
        json_object_put (o);
    if (sender)
        free (sender);
}

static void _kvs_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL, *key, *val;
    char *valstr;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (!(key = json_object_object_get (o, "key"))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    valstr = _redis_get (p, json_object_get_string (key));
    if (valstr) { /* omit val in response on error */
        if (!(val = json_object_new_string (valstr)))
            oom ();
        json_object_object_add (o, "val", val);
    }
    if (cmb_msg_rep_json (*zmsg, o) < 0)
        goto done;
    if (zmsg_send (zmsg, p->zs_out) < 0)
        err ("zmsg_send"); 
done:
    if (o)
        json_object_put (o);
    if (valstr)
        free (valstr);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _kvs_commit (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL, *no;
    char *sender = NULL;
    int errcount = 0, putcount = 0;
    client_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    if ((c = _client_find_byidentity (p, sender))) {
        _flush_set_queue (p, c);
        errcount = c->errcount;
        putcount = c->putcount;
        c->errcount = c->putcount = 0;
    }
    if (!(no = json_object_new_int (errcount)))
        oom ();
    json_object_object_add (o, "errcount", no);
    if (!(no = json_object_new_int (putcount)))
        oom ();
    json_object_object_add (o, "putcount", no);
    if (cmb_msg_rep_json (*zmsg, o) < 0)
        goto done;
    if (zmsg_send (zmsg, p->zs_out) < 0)
        err ("zmsg_send"); 
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, msg_type_t type)
{
#if 0 
    /* api.<uuid>.disconnect */
    if (!strncmp (tag, "api.", strlen ("api."))) {
        client_t *c = _client_find_bysubscription (p, tag);
        if (c)
            _client_destroy (p, c);
#endif

    if (cmb_msg_match (*zmsg, "kvs.put"))
        _kvs_put (p, zmsg);
    else if (cmb_msg_match (*zmsg, "kvs.get"))
        _kvs_get (p, zmsg);
    else if (cmb_msg_match (*zmsg, "kvs.commit"))
        _kvs_commit (p, zmsg);
    else {
        if (cmb_msg_rep_nak (*zmsg) >= 0)
            zmsg_send (zmsg, p->zs_out);
    }
    if (*zmsg)
        zmsg_destroy (zmsg);
}


static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));

    p->ctx = ctx;
retryconnect:
    ctx->rctx = redisConnect (p->conf->redis_server, 6379);
    if (ctx->rctx == NULL) {
        err ("redisConnect returned NULL - abort");
        return;
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
        return;
    }

    //zsocket_set_subscribe (p->zs_in, "kvs.");
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
