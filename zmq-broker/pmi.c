/* pmi.c -- libpmi (v1) built directly on hiredis */

/* Presumes slurm underneath. */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

#include "cmb.h"
#include "log.h"

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

#define FORCE_HASH 0


typedef struct {
    int magic;
    int spawned;
    int size;
    int rank;
    int universe_size;
    int appnum;
    int barrier_num;

    cmb_t cctx;
    char kvsname[PMI_MAX_KVSNAMELEN];
} pmi_ctx_t;
#define PMI_CTX_MAGIC 0xcafefaad

static pmi_ctx_t *ctx = NULL;

static int _env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}

#if FORCE_HASH
static int _key_tostore (const char *key, char **kp)
{
    const char *p;
    int n;

    p = key;
    while (*p && !isdigit (*p))
        p++;
    if (p) {
        n = strtoul (p, NULL, 10);
        if (asprintf (kp, "%s:{%d}%s", ctx->kvsname, n, key) < 0)
            return -1;
    } else {
        if (asprintf (kp, "%s:%s", ctx->kvsname, key) < 0)
            return -1;
    }
    return 0;
}
#else
static int _key_tostore (const char *key, char **kp)
{
    return asprintf (kp, "%s:%s", ctx->kvsname, key);
}
#endif

int PMI_Init( int *spawned )
{
    log_init ("cmb-pmi");
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
    ctx->size = _env_getint ("SLURM_NTASKS", 1);
    ctx->rank = _env_getint ("SLURM_PROCID", 0);
    //msg ("XXX %d:%s", ctx->rank, __FUNCTION__);
    ctx->universe_size = _env_getint ("SLURM_NTASKS", 1);
    ctx->appnum = _env_getint ("SLURM_JOB_ID", 1);
    ctx->barrier_num = 0;
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "job%d",
                _env_getint ("SLURM_STEP_ID", 0));
    if (!(ctx->cctx = cmb_init ())) {
        err ("cmb_init");
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
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (initialized == NULL)
        return PMI_ERR_INVALID_ARG;

    *initialized = ctx ? PMI_TRUE : PMI_FALSE;
    return PMI_SUCCESS;
}

int PMI_Finalize( void )
{
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (ctx->cctx)
        cmb_fini (ctx->cctx);
    memset (ctx, 0, sizeof (pmi_ctx_t));
    free (ctx);
    ctx = NULL;

    return PMI_SUCCESS;
}

int PMI_Get_size( int *size )
{
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
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
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
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
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
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
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
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
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Unpublish_name( const char service_name[] )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Lookup_name( const char service_name[], char port[] )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Barrier( void )
{
    char *name = NULL;

    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);

    if (asprintf (&name, "%s:%d", ctx->kvsname, ctx->barrier_num) < 0)
        return PMI_ERR_NOMEM;
    if (cmb_barrier (ctx->cctx, name, ctx->universe_size) < 0)
        goto error;
    ctx->barrier_num++;
    free (name);
    return PMI_SUCCESS;
error:
    if (name)
        free (name);
    return PMI_FAIL;
}
  
int PMI_Abort(int exit_code, const char error_msg[])
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_KVS_Get_my_name( char kvsname[], int length )
{
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
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
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KVSNAMELEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max( int *length )
{
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KEYLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max( int *length )
{
    //msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_VALLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Put( const char kvsname[], const char key[], const char value[])
{
    char *xkey = NULL;

    //msg ("XXX %d:%s %s:%s %s", ctx ? ctx->rank : -1, __FUNCTION__, kvsname, key, value);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;

    if (_key_tostore (key, &xkey) < 0)
        return PMI_ERR_NOMEM;

    if (cmb_kvs_put (ctx->cctx, xkey, value) < 0)
        goto error;
    free (xkey);
    return PMI_SUCCESS;
error:
    if (xkey)
        free (xkey);
    return PMI_FAIL;
}

int PMI_KVS_Commit( const char kvsname[] )
{
    int errcount, putcount;
    //msg ("XXX %d:%s %s", ctx ? ctx->rank : -1, __FUNCTION__, kvsname);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL)
        return PMI_ERR_INVALID_ARG;

    if (cmb_kvs_commit (ctx->cctx, &errcount, &putcount) < 0)
        goto error;
    if (errcount > 0)
        goto error;
    return PMI_SUCCESS;
error:
    return PMI_FAIL;
}

int PMI_KVS_Get( const char kvsname[], const char key[], char value[], int length)
{
    char *xkey = NULL;
    char *val = NULL;

    //msg ("XXX %d:%s %s:%s", ctx ? ctx->rank : -1, __FUNCTION__, kvsname, key);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;

    if (_key_tostore (key, &xkey) < 0)
        return PMI_ERR_NOMEM;

    val = cmb_kvs_get (ctx->cctx, xkey);
    if (!val && errno == 0) {
        free (xkey);
        return PMI_ERR_INVALID_KEY;
    }
    if (!val && errno != 0)
        goto error;
    snprintf (value, length, "%s", val);
    free (val); 
    free (xkey); 
    return PMI_SUCCESS;
error:
    if (xkey)
        free (xkey);
    if (val)
        free (val);
    return PMI_FAIL;
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
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Get_id( char id_str[], int length )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

/* openmpi */
int PMI_Get_kvs_domain_id( char id_str[], int length )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (id_str == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    snprintf (id_str, length, "%s", "foo");
    return PMI_SUCCESS;
}

/* openmpi */
int PMI_Get_id_length_max( int *length )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *length = 42;
    return PMI_SUCCESS;
}

/* openmpi */
int PMI_Get_clique_size( int *size )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    *size = ctx->size;
    return PMI_SUCCESS;
}

/* openmpi */
int PMI_Get_clique_ranks( int ranks[], int length)
{
    int i;

    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length != ctx->size)
        return PMI_ERR_INVALID_ARG;
    for (i = 0; i < length; i++)
        ranks[i] = i;
    return PMI_SUCCESS;
}

int PMI_KVS_Create( char kvsname[], int length )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_KVS_Destroy( const char kvsname[] )
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_KVS_Iter_first(const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_KVS_Iter_next(const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Parse_option(int num_args, char *args[], int *num_parsed,
                        PMI_keyval_t **keyvalp, int *size)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Args_to_keyval(int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Free_keyvals(PMI_keyval_t keyvalp[], int size)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

int PMI_Get_options(char *str, int *length)
{
    msg ("XXX %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    return PMI_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
