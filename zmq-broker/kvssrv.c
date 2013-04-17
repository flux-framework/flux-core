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
    kv_t *set_backlog;
} client_t;

typedef struct {
    redisContext *rctx;
    client_t *clients;
} ctx_t;

static void _add_set_backlog (client_t *c, const char *key, const char *val)
{
    kv_t *kv = xzmalloc (sizeof (kv_t));

    kv->key = xstrdup (key);
    kv->val = xstrdup (val);

    kv->next = c->set_backlog;
    if (kv->next)
        kv->next->prev = kv;
    c->set_backlog = kv;
}

static void _flush_set_backlog (plugin_ctx_t *p, client_t *c)
{
    ctx_t *ctx = p->ctx;
    kv_t *kp;
    redisReply *rep;
    int replycount = 0;

    for (kp = c->set_backlog; kp != NULL; kp = kp->next)
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
    while (c->set_backlog) {
        kp = c->set_backlog;
        c->set_backlog = kp->next;
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

    zsocket_set_subscribe (p->zs_in, c->subscription);

    return c;
}

static void _client_destroy (plugin_ctx_t *p, client_t *c)
{   
    ctx_t *ctx = p->ctx;
    kv_t *kp, *deleteme;

    zsocket_set_unsubscribe (p->zs_in, c->subscription);

    free (c->identity);
    free (c->subscription);
    for (kp = c->set_backlog; kp != NULL; ) {
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

static client_t *_client_find_bysubscription (plugin_ctx_t *p, const char *subscription)
{
    ctx_t *ctx = p->ctx;
    client_t *c;
 
    for (c = ctx->clients; c != NULL; c = c->next) {
        if (!strcmp (c->subscription, subscription))
            return c;
    }
    return NULL;
}

static int _parse_kvs_put (json_object *o, const char **kp, const char **vp,
                                           const char **sp)
{
    json_object *key, *val, *sender;

    if (!o)
        goto error;
    key = json_object_object_get (o, "key"); 
    if (!key)
        goto error;
    val = json_object_object_get (o, "val"); 
    if (!val)
        goto error;
    sender = json_object_object_get (o, "sender");
    if (!sender)
        goto error;
    *kp = json_object_get_string (key);
    *vp = json_object_get_string (val);
    *sp = json_object_get_string (sender);
    return 0;
error:
    return -1;
}

static int _parse_kvs_get (json_object *o, const char **kp, const char **sp)
{
    json_object *key, *sender;

    if (!o)
        goto error;
    key = json_object_object_get (o, "key"); 
    if (!key)
        goto error;
    sender = json_object_object_get (o, "sender");
    if (!sender)
        goto error;
    *kp = json_object_get_string (key);
    *sp = json_object_get_string (sender);
    return 0;
error:
    return -1;
}

static int _parse_kvs_commit (json_object *o, const char **sp)
{
    json_object *sender;

    if (!o)
        goto error;
    sender = json_object_object_get (o, "sender");
    if (!sender)
        goto error;
    *sp = json_object_get_string (sender);
    return 0;
error:
    return -1;
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

static void _reply_to_get (plugin_ctx_t *p, const char *sender, const char *val)
{
    json_object *o, *no;

    if (!(o = json_object_new_object ()))
        oom ();
    if (val) { /* if val is null, key was not found - omit 'val' in response */
        if (!(no = json_object_new_string (val)))
            oom ();
        json_object_object_add (o, "val", no);
    }
    cmb_msg_send (p->zs_out, o, "%s", sender);
    json_object_put (o);
}

static void _reply_to_commit (plugin_ctx_t *p, const char *sender,
                              int errcount, int putcount)
{
    json_object *o, *no;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_int (errcount)))
        oom ();
    json_object_object_add (o, "errcount", no);
    if (!(no = json_object_new_int (putcount)))
        oom ();
    json_object_object_add (o, "putcount", no);

    cmb_msg_send (p->zs_out, o, "%s", sender);

    json_object_put (o);
}

static void _recv (plugin_ctx_t *p, zmsg_t *zmsg)
{
    char *tag = NULL;
    json_object *o = NULL;

    if (cmb_msg_decode (zmsg, &tag, &o, NULL, NULL) < 0) {
        err ("kvssrv: recv");
        goto done;
    }

    /* api.<uuid>.disconnect */
    if (!strncmp (tag, "api.", strlen ("api."))) {
        client_t *c = _client_find_bysubscription (p, tag);
        if (c)
            _client_destroy (p, c);

    } else if (!strcmp (tag, "kvs.put")) {
        const char *key, *val, *sender;
        client_t *c;

        if (_parse_kvs_put (o, &key, &val, &sender) < 0){
            msg ("%s: parse error", tag);
            goto done;
        }
        c = _client_find_byidentity (p, sender);
        if (!c)
            c = _client_create (p, sender);
        _add_set_backlog (c, key, val);

    } else if (!strcmp (tag, "kvs.get")) {
        const char *key, *sender;
        char *val;

        if (_parse_kvs_get (o, &key, &sender) < 0){
            msg ("%s: parse error", tag);
            goto done;
        }
        val = _redis_get (p, key);
        _reply_to_get (p, sender, val);
        if (val)
            free (val);
    } else if (!strcmp (tag, "kvs.commit")) {
        const char *sender;
        client_t *c;

        if (_parse_kvs_commit (o, &sender) < 0) {
            msg ("%s: parse error", tag);
            goto done;
        }
        c = _client_find_byidentity (p, sender);
        if (c) {
            _flush_set_backlog (p, c);
            _reply_to_commit (p, sender, c->errcount, c->putcount);
            c->putcount = 0;
            c->errcount = 0;
        } else
            _reply_to_commit (p, sender, 0, 0);
    }
done:
    free (tag);
    if (o)
        json_object_put (o);
    if (zmsg)
        zmsg_destroy (&zmsg);
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

    zsocket_set_subscribe (p->zs_in, "kvs.");
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
    .initFn    = _init,
    .recvFn    = _recv,
    .finiFn    = _fini,
};


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
