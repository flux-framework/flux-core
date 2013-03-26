/* pmi.c -- libpmi (v1) built directly on hiredis */

/* Presumes slurm underneath. */

#include <stdio.h>
#include <hiredis/hiredis.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pmi.h"

#ifndef PMI_FALSE
#define PMI_FALSE 0
#endif
#ifndef PMI_TRUE
#define PMI_TRUE 1
#endif

#define PMI_MAX_KEYLEN 64
#define PMI_MAX_VALLEN 1024
#define PMI_MAX_KVSNAMELEN 64


typedef struct {
    int magic;
    int spawned;
    int size;
    int rank;
    int universe_size;
    int appnum;
    int barrier_num;

    char *rhostname;
    int rport;
    redisContext *rctx;
    redisContext *bctx;

    char kvsname[PMI_MAX_KVSNAMELEN];
} pmi_ctx_t;
#define PMI_CTX_MAGIC 0xcafefaad

#define RECONNECT_DELAY_START   1
#define RECONNECT_DELAY_MAX     10
#define RECONNECT_DELAY_INCR(x) (x++)

static int _publish (char *channel, char *msg);
static int _barrier_subscribe (void);

static pmi_ctx_t *ctx = NULL;

static int _env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}

static char *_env_getstr (char *name, char *dflt)
{
    char *ev = getenv (name);
    return ev ? strdup (ev) : strdup (dflt);
}

int PMI_Init( int *spawned )
{
    int secs = RECONNECT_DELAY_START;

    if (spawned == NULL)
        return PMI_ERR_INVALID_ARG;
    if (ctx)
        goto fail;
    ctx = malloc (sizeof (pmi_ctx_t));
    if (ctx == NULL)
        goto nomem;
    memset (ctx, 0, sizeof (pmi_ctx_t));
    ctx->magic = PMI_CTX_MAGIC;
    ctx->spawned = PMI_FALSE;
    ctx->size = _env_getint ("SLURM_NTASKS", 0);
    ctx->rank = _env_getint ("SLURM_PROCID", 1);
    ctx->universe_size = _env_getint ("SLURM_NTASKS", 1);
    ctx->appnum = 0;
    ctx->barrier_num = 0;
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "job%d",
                _env_getint ("SLURM_JOB_ID", 0));
    ctx->rhostname = _env_getstr ("SLURM_LAUNCH_NODE_IPADDR", "127.0.0.1");
    if (ctx->rhostname == NULL)
        goto nomem;
    ctx->rport = 6379;

again:
    ctx->rctx = redisConnect (ctx->rhostname, ctx->rport);
    if (ctx->rctx == NULL)
        goto fail;
    if (secs <= RECONNECT_DELAY_MAX && ctx->rctx->err == REDIS_ERR_IO
                                    && errno == ECONNREFUSED) {
        redisFree (ctx->rctx);
        sleep (secs);
        RECONNECT_DELAY_INCR(secs);
        goto again;
    }
        
    if (ctx->rctx->err) {
        fprintf (stderr, "redisConnect: %s\n", ctx->rctx->errstr); /* FIXME */
        goto fail;
    }

    ctx->bctx = redisConnect (ctx->rhostname, ctx->rport);
    if (ctx->bctx == NULL)
        goto fail;
    if (ctx->bctx->err) {
        fprintf (stderr, "redisConnect: %s\n", ctx->bctx->errstr); /* FIXME */
        goto fail;
    }
    if (_barrier_subscribe () == PMI_FAIL)
        goto fail;

    *spawned = ctx->spawned;
    return PMI_SUCCESS;
nomem:
    if (ctx)
        PMI_Finalize ();
    return PMI_ERR_NOMEM;
fail:
    if (ctx)
        PMI_Finalize ();
    return PMI_FAIL;
}

int PMI_Initialized( int *initialized )
{
    if (initialized == NULL)
        return PMI_ERR_INVALID_ARG;

    *initialized = ctx ? PMI_TRUE : PMI_FALSE;
    return PMI_SUCCESS;
}

int PMI_Finalize( void )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (ctx->rhostname)
        free (ctx->rhostname);
    if (ctx->bctx)
        redisFree (ctx->bctx);
    if (ctx->rctx)
        redisFree (ctx->rctx);
    memset (ctx, 0, sizeof (pmi_ctx_t));
    free (ctx);
    ctx = NULL;

    return PMI_SUCCESS;
}

int PMI_Get_size( int *size )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (size == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *size = ctx->size;
    return PMI_SUCCESS;
}

int PMI_Get_rank( int *rank )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (rank == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *rank = ctx->rank;
    return PMI_SUCCESS;
}

int PMI_Get_universe_size( int *size )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (size == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *size = ctx->universe_size;
    return PMI_SUCCESS;
}

int PMI_Get_appnum( int *appnum )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (appnum == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *appnum = ctx->appnum;
    return PMI_SUCCESS;
}

int PMI_Publish_name( const char service_name[], const char port[] )
{
    char msg[64];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (service_name == NULL || port == NULL)
        return PMI_ERR_INVALID_ARG;
    snprintf (msg, sizeof (msg), "PMI_Publish_name %s:%s", service_name, port);

    return _publish ("PMI", msg);
}

int PMI_Unpublish_name( const char service_name[] )
{
    char msg[64];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (service_name == NULL)
        return PMI_ERR_INVALID_ARG;
    snprintf (msg, sizeof (msg), "PMI_Unpublish_name %s", service_name);

    return _publish ("PMI", msg);
}

int PMI_Lookup_name( const char service_name[], char port[] )
{
    char msg[64];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (service_name == NULL || port == NULL)
        return PMI_ERR_INVALID_ARG;
    snprintf (msg, sizeof (msg), "PMI_Lookup_name %s:%s", service_name, port);

    return _publish ("PMI", msg);
}

static int _barrier_subscribe (void)
{
    redisReply *rep;
    int ret = PMI_FAIL;

    rep = redisCommand (ctx->bctx, "SUBSCRIBE %s:barrier", ctx->kvsname);
    if (rep == NULL) {
        fprintf (stderr, "barrier_subscribe: %s\n", ctx->bctx->errstr);
        return PMI_FAIL;
    }
    switch (rep->type) {
        case REDIS_REPLY_ARRAY:
            ret = PMI_SUCCESS;
            break;
        case REDIS_REPLY_ERROR:
            fprintf (stderr, "barrier_subscribe: error: %s\n", rep->str); /* FIXME */
            break;
        case REDIS_REPLY_NIL:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
            fprintf (stderr, "barrier_subscribe: unexpected reply type\n");
            break;
    }

    freeReplyObject (rep);
    return ret;
}

static int _barrier_enter (void)
{
    redisReply *rep;
    int ret = PMI_FAIL;

    rep = redisCommand (ctx->rctx, "EVAL %s 2 %s:barrier%d %s:barrier %d",
                        "if redis.call('incr', KEYS[1]) == tonumber(ARGV[1])"
                        "  then redis.call('publish', KEYS[2], KEYS[1]) end",
                        ctx->kvsname, ctx->barrier_num,
                        ctx->kvsname, ctx->universe_size);
    if (rep == NULL) {
        fprintf (stderr, "barrier_enter: %s\n", ctx->rctx->errstr); /* FIXME */
        return PMI_FAIL;
    }
    switch (rep->type) {
        case REDIS_REPLY_NIL:
            ret = PMI_SUCCESS;
            break;
        case REDIS_REPLY_ERROR:
            fprintf (stderr, "barrier_enter: error: %s\n", rep->str);/* FIXME */
            break;
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
        case REDIS_REPLY_ARRAY:
            fprintf (stderr, "barrier_enter: unexpected reply type\n");
            break;
    }

    freeReplyObject (rep);
    return ret;
}

static int _barrier_exit (void)
{
    redisReply *rep;
    int ret = PMI_FAIL;

    if (redisGetReply (ctx->bctx, (void **)&rep) != REDIS_OK) {
        fprintf (stderr, "barrier_exit: error: %s\n", ctx->bctx->errstr);
        return PMI_FAIL;
    }
    switch (rep->type) {
        case REDIS_REPLY_ARRAY:
            // 'message' 'kvsname:barrier' 'kvsname:barriern'
            ret = PMI_SUCCESS;
            break;
        case REDIS_REPLY_ERROR:
            fprintf (stderr, "barrier_exit: error: %s\n", rep->str);/* FIXME */
            break;
        case REDIS_REPLY_NIL:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
            fprintf (stderr, "barrier_exit: unexpected reply type\n");
            break;
    }

    freeReplyObject (rep);
    return ret;
}

int PMI_Barrier( void )
{
    redisReply *rep;
    int ret;

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);

    ret = _barrier_enter ();
    if (ret != PMI_SUCCESS)
        return ret;

    ret = _barrier_exit ();
    if (ret != PMI_SUCCESS)
        return ret;

    ctx->barrier_num++;
    return PMI_SUCCESS;
}
  
int PMI_Abort(int exit_code, const char error_msg[])
{
    char msg[64];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    snprintf (msg, sizeof (msg), "PMI_Abort %d:%s", exit_code,
                error_msg ? error_msg : "<null>");

    return _publish ("PMI", msg);
}

int PMI_KVS_Get_my_name( char kvsname[], int length )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || length < strlen (ctx->kvsname) + 1)
        return PMI_ERR_INVALID_ARG;
    
    strcpy (kvsname, ctx->kvsname);
    return PMI_SUCCESS;
}

int PMI_KVS_Get_name_length_max( int *length )
{
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KVSNAMELEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max( int *length )
{
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KEYLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max( int *length )
{
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_VALLEN;
    return PMI_SUCCESS;
}

/* FIXME: ignoring max lengths of strings for now */
int PMI_KVS_Put( const char kvsname[], const char key[], const char value[])
{
    redisReply *rep;
    int ret = PMI_FAIL;

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;
    
    rep = redisCommand (ctx->rctx, "SET %s:%s %s", ctx->kvsname, key, value);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr); /* FIXME */
        /* FIXME: context cannot be reused */
        return PMI_FAIL;
    }
    switch (rep->type) {
        case REDIS_REPLY_STATUS:
            //fprintf (stderr, "redisCommand: status reply: %s\n", rep->str);
            ret = PMI_SUCCESS;
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
        
    return ret;
}

static int _publish (char *channel, char *msg)
{
    redisReply *rep;
    int ret = PMI_FAIL;

    rep = redisCommand (ctx->rctx, "PUBLISH %s %d:%s", channel, ctx->rank, msg);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr); /* FIXME */
        return PMI_FAIL;
    }
    switch (rep->type) {
        case REDIS_REPLY_ERROR: /* FIXME */
            fprintf (stderr, "redisCommand: error reply: %s\n", rep->str);
            break;
        case REDIS_REPLY_INTEGER: /* number of clients receiving message */
            ret = PMI_SUCCESS;
            break;
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_NIL:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_ARRAY:
            fprintf (stderr, "redisCommand: unexpected reply type\n");
            break;
    }
    freeReplyObject (rep);
        
    return ret;
}

int PMI_KVS_Commit( const char kvsname[] )
{
    char msg[PMI_MAX_KVSNAMELEN + 16];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL)
        return PMI_ERR_INVALID_ARG;
    snprintf (msg, sizeof (msg), "PMI_KVS_Commit %s", kvsname);

    return _publish ("PMI", msg);
}

int PMI_KVS_Get( const char kvsname[], const char key[], char value[], int length)
{
    redisReply *rep;
    int ret = PMI_FAIL;
    char *p;

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;
    
    rep = redisCommand (ctx->rctx, "GET %s:%s", ctx->kvsname, key);
    if (rep == NULL) {
        fprintf (stderr, "redisCommand: %s\n", ctx->rctx->errstr); /* FIXME */
        /* FIXME: context cannot be reused */
        goto done;
    }
    switch (rep->type) {
        case REDIS_REPLY_ERROR:
            assert (rep->str != NULL);
            fprintf (stderr, "redisCommand: error reply: %s\n", rep->str);
            break;
        case REDIS_REPLY_NIL:
            ret = PMI_ERR_INVALID_KEY;
            break;
        case REDIS_REPLY_STRING:
            assert (rep->str != NULL);
            p = strchr (rep->str, ':');
            snprintf (value, length, p ? p + 1 : rep->str);
            ret = PMI_SUCCESS;
            break;
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_INTEGER:
        case REDIS_REPLY_ARRAY:
            fprintf (stderr, "redisCommand: unexpected reply type\n");
            break;
    }
done:
    if (rep)
        freeReplyObject (rep);
    return ret;
}

int PMI_Spawn_multiple(int count,
                       const char * cmds[],
                       const char ** argvs[],
                       const int maxprocs[],
                       const int info_keyval_sizesp[],
                       const PMI_keyval_t * info_keyval_vectors[],
                       int preput_keyval_size,
                       const PMI_keyval_t preput_keyval_vector[],
                       int errors[])
{
    char msg[64];

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    snprintf (msg, sizeof (msg), "PMI_Spawn_multiple");

    return _publish ("PMI", msg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
