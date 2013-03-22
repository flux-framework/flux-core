/* pmi.c -- libpmi (v1) built directly on hiredis */

/* Presumes slurm underneath. */

#include <stdio.h>
#include <hiredis/hiredis.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "pmi.h"

#ifndef PMI_FALSE
#define PMI_FALSE 0
#endif
#ifndef PMI_TRUE
#define PMI_TRUE 1
#endif

#define KVSNAME_MAXLEN  16
#define KEY_MAXLEN      64
#define VAL_MAXLEN      64

typedef struct {
    int magic;
    int spawned;
    int size;
    int rank;
    int universe_size;
    int appnum;

    char *rhostname;
    int rport;
    redisContext *rctx;

    char kvsname[KVSNAME_MAXLEN];
} pmi_ctx_t;
#define PMI_CTX_MAGIC 0xcafefaad

static int _publish (char *channel, char *msg);

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
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "job%d",
                _env_getint ("SLURM_JOB_ID", 0));
    ctx->rhostname = _env_getstr ("SLURM_LAUNCH_NODE_IPADDR", "127.0.0.1");
    if (ctx->rhostname == NULL)
        goto nomem;
    ctx->rport = 6379;

    ctx->rctx = redisConnect (ctx->rhostname, ctx->rport);
    if (ctx->rctx == NULL)
        goto fail;
    if (ctx->rctx->err) {
        fprintf (stderr, "redisConnect: %s\n", ctx->rctx->errstr); /* FIXME */
        goto fail;
    }

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
    return PMI_FAIL;
}

int PMI_Unpublish_name( const char service_name[] )
{
    return PMI_FAIL;
}

int PMI_Lookup_name( const char service_name[], char port[] )
{
    return PMI_FAIL;
}

int PMI_Barrier( void )
{
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);

    return _publish ("PMI", "PMI_Barrier");
}

int PMI_Abort(int exit_code, const char error_msg[])
{
    return PMI_FAIL;
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
    *length = KVSNAME_MAXLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max( int *length )
{
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = KEY_MAXLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max( int *length )
{
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = VAL_MAXLEN;
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
        /* FIXME: context cannot be reused */
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
    char msg[KVSNAME_MAXLEN + 16];

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
    return PMI_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
