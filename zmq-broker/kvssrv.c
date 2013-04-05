/* kvssrv.c - key-value service */ 

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

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    void *zs_in;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t t;
    conf_t *conf;
    redisContext *rctx;
};

static ctx_t ctx = NULL;

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

static void _redis_set (const char *key, const char *val)
{
    redisReply *rep;

    rep = redisCommand (ctx->rctx, "SET %s %s", key, val);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr);
        return; /* XXX rctx cannot be reused? */
    }
    switch (rep->type) {
        case REDIS_REPLY_STATUS:
            //fprintf (stderr, "redisCommand: status reply: %s\n", rep->str);
            /* success */
            break;
        case REDIS_REPLY_ERROR: /* FIXME */
            fprintf (stderr, "redisCommand: error reply: %s\n", rep->str);
            break;
        case REDIS_REPLY_INTEGER:
        case REDIS_REPLY_NIL:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_ARRAY:
            fprintf (stderr, "redisCommand: unexpected reply type\n");
            break;
    }
    freeReplyObject (rep);
}

static char *_redis_get (const char *key)
{
    redisReply *rep;
    char *val = NULL;

    rep = redisCommand (ctx->rctx, "GET %s", key);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr); /* FIXME */
        return NULL; /* FIXME: context cannot be reused */
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
    cmb_msg_send (ctx->zs_out, o, NULL, 0, "%s", sender);
    json_object_put (o);
}

static void *_thread (void *arg)
{
    bool shutdown = false;
    char *tag;
    json_object *o;

again:
    ctx->rctx = redisConnect (ctx->conf->rootnode, 6379);
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
        if (cmb_msg_recv (ctx->zs_in, &tag, &o, NULL, 0) < 0) {
            fprintf (stderr, "cmb_msg_recv: %s\n", strerror (errno));
            continue;
        }
        if (!strcmp (tag, "event.cmb.shutdown")) {
            shutdown = true;
            goto next;
        } else if (!strcmp (tag, "kvs.put")) {
            const char *key, *val, *sender;

            if (_parse_kvs_put (o, &key, &val, &sender) < 0){
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            _redis_set (key, val);
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

            if (_parse_kvs_commit (o, &sender) < 0) {
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            cmb_msg_send (ctx->zs_out, NULL, NULL, 0, "%s", sender);
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
    _zmq_subscribe (ctx->zs_in, "event.cmb.shutdown");

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    if (conf->root_server)
        _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    ctx->zs_out_tree = _zmq_socket (zctx, ZMQ_PUSH);
    if (!conf->root_server)
        _zmq_connect (ctx->zs_out_tree, conf->plin_tree_uri);

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
    _zmq_close (ctx->zs_out_event);
    _zmq_close (ctx->zs_out_tree);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
