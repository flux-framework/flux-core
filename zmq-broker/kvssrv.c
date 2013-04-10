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
#include <json/json.h>
#include <hiredis/hiredis.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "kvssrv.h"
#include "util.h"

typedef struct _kv_struct {
    char *key;
    char *val;
    struct _kv_struct *next;
} kv_t;

typedef struct _client_struct {
    struct _client_struct *next;
    struct _client_struct *prev;
    char *identity;
    int putcount;
    int errcount;
    char *subscription;
    kv_t *set_backlog;
    kv_t *set_backlog_last;
} client_t;

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    void *zs_in;
    void *zs_out;
    pthread_t t;
    conf_t *conf;
    redisContext *rctx;
    client_t *clients;
};

static ctx_t ctx = NULL;

static void _add_set_backlog (client_t *c, const char *key, const char *val)
{
    kv_t *kv = xzmalloc (sizeof (kv_t));

    kv->key = xstrdup (key);
    kv->val = xstrdup (val);

    if (c->set_backlog_last)
        c->set_backlog_last->next = kv;
    else
        c->set_backlog = kv;
    c->set_backlog_last = kv;
}

static void _flush_set_backlog (client_t *c)
{
    kv_t *kp, *deleteme;
    redisReply *rep;

    for (kp = c->set_backlog; kp != NULL; kp = kp->next) {
        redisAppendCommand (ctx->rctx, "SET %s %s", kp->key, kp->val);
        c->putcount++;
    }
    for (kp = c->set_backlog; kp != NULL; ) {
        if (redisGetReply (ctx->rctx, (void **)&rep) == REDIS_ERR) {
            c->errcount++;
            goto next;
        }
        switch (rep->type) {
            case REDIS_REPLY_STATUS:
                /* success */
                //fprintf (stderr, "redis put: %s\n", rep->str);
                break;
            case REDIS_REPLY_ERROR:
                c->errcount++;
                //fprintf (stderr, "redis put: %s\n", rep->str);
                break;
            case REDIS_REPLY_INTEGER:
            case REDIS_REPLY_NIL:
            case REDIS_REPLY_STRING:
            case REDIS_REPLY_ARRAY:
                fprintf (stderr, "redisCommand: unexpected reply type\n");
                c->errcount++;
                break;
        }
        freeReplyObject (rep);
next:
        deleteme = kp;
        kp = kp->next; 
        free (deleteme->key);
        free (deleteme->val);
        free (deleteme);
    }
    c->set_backlog = NULL;
    c->set_backlog_last = NULL;
}

static client_t *_client_create (const char *identity)
{
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

    _zmq_subscribe (ctx->zs_in, c->subscription);

    return c;
}

static void _client_destroy (client_t *c)
{   
    kv_t *kp, *deleteme;

    _zmq_unsubscribe (ctx->zs_in, c->subscription);

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

static client_t *_client_find_byidentity (const char *identity)
{
    client_t *c;
 
    for (c = ctx->clients; c != NULL; c = c->next) {
        if (!strcmp (c->identity, identity))
            return c;
    }
    return NULL;
}

static client_t *_client_find_bysubscription (const char *subscription)
{
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

static char *_redis_get (const char *key)
{
    redisReply *rep;
    char *val = NULL;

    rep = redisCommand (ctx->rctx, "GET %s", key);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr);
        return NULL; /* FIXME: rctx cannot be reused */
    }
    switch (rep->type) {
        case REDIS_REPLY_ERROR:
            assert (rep->str != NULL);
            fprintf (stderr, "redisCommand: error reply: %s\n", rep->str);
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
            fprintf (stderr, "redisCommand: unexpected reply type\n");
            break;
    }
    if (rep)
        freeReplyObject (rep);
    return val;
}

static void _reply_to_get (const char *sender, const char *val)
{
    json_object *o, *no;

    if (!(o = json_object_new_object ()))
        oom ();
    if (val) { /* if val is null, key was not found - omit 'val' in response */
        if (!(no = json_object_new_string (val)))
            oom ();
        json_object_object_add (o, "val", no);
    }
    cmb_msg_send (ctx->zs_out, o, NULL, 0, 0, "%s", sender);
    json_object_put (o);
}

static void _reply_to_commit (const char *sender, int errcount, int putcount)
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

    cmb_msg_send (ctx->zs_out, o, NULL, 0, 0, "%s", sender);

    json_object_put (o);
}

static void *_thread (void *arg)
{
    bool shutdown = false;
    char *tag;
    json_object *o;

again:
    ctx->rctx = redisConnect (ctx->conf->redis_server, 6379);
    if (ctx->rctx == NULL) {
        fprintf (stderr, "redisConnect returned NULL - abort\n");
        shutdown = true;
    } else if (ctx->rctx->err == REDIS_ERR_IO && errno == ECONNREFUSED) {
            redisFree (ctx->rctx);
            sleep (2);
            fprintf (stderr, "redisConnect: retrying connect");
            goto again;
    } else if (ctx->rctx->err) {
        fprintf (stderr, "redisConnect: %s\n", ctx->rctx->errstr);
        shutdown = true;
        redisFree (ctx->rctx);
        ctx->rctx = NULL;
    }

    while (!shutdown) {
        if (cmb_msg_recv (ctx->zs_in, &tag, &o, NULL, NULL, 0) < 0) {
            fprintf (stderr, "cmb_msg_recv: %s\n", strerror (errno));
            continue;
        }

        /* api.<uuid>.disconnect */
        if (!strncmp (tag, "api.", strlen ("api."))) {
            client_t *c = _client_find_bysubscription (tag);
            if (c)
                _client_destroy (c);

        } else if (!strcmp (tag, "kvs.put")) {
            const char *key, *val, *sender;
            client_t *c;

            if (_parse_kvs_put (o, &key, &val, &sender) < 0){
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            c = _client_find_byidentity (sender);
            if (!c)
                c = _client_create (sender);
            _add_set_backlog (c, key, val);

        } else if (!strcmp (tag, "kvs.get")) {
            const char *key, *sender;
            char *val;

            if (_parse_kvs_get (o, &key, &sender) < 0){
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            val = _redis_get (key);
            _reply_to_get (sender, val);
            if (val)
                free (val);
        } else if (!strcmp (tag, "kvs.commit")) {
            const char *sender;
            client_t *c;

            if (_parse_kvs_commit (o, &sender) < 0) {
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            c = _client_find_byidentity (sender);
            if (c) {
                _flush_set_backlog (c);
                _reply_to_commit (sender, c->errcount, c->putcount);
                c->putcount = 0;
                c->errcount = 0;
            } else
                _reply_to_commit (sender, 0, 0);
        }
next:
        free (tag);
        if (o)
            json_object_put (o);
    }

    if (ctx->rctx)
        redisFree (ctx->rctx);

    return NULL;
}


void kvssrv_init (conf_t *conf, void *zctx)
{
    int err;

    ctx = xzmalloc (sizeof (struct ctx_struct));
    ctx->conf = conf;

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe (ctx->zs_in, "kvs.");

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

    err = pthread_create (&ctx->t, NULL, _thread, NULL);
    if (err) {
        fprintf (stderr, "%s: pthread_create: %s\n", __FUNCTION__,
                 strerror (err));
        exit (1);
    }
}

void kvssrv_fini (void)
{
    int err;

    err = pthread_join (ctx->t, NULL);
    if (err) {
        fprintf (stderr, "%s: pthread_join: %s\n", __FUNCTION__,
                 strerror (err));
        exit (1);
    }
    _zmq_close (ctx->zs_in);
    _zmq_close (ctx->zs_out);

    while (ctx->clients != NULL)
        _client_destroy (ctx->clients);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
