/* pmi.c -- PMI-1 on CMBD and SLURM */

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
#define PMI_MAX_ID_LEN 16

#define FORCE_HASH 0


typedef struct {
    int magic;
    int spawned;
    int size;
    int rank;
    int *clique_ranks;
    int clique_size;
    int universe_size;
    int appnum;
    int barrier_num;

    cmb_t cctx;
    char kvsname[PMI_MAX_KVSNAMELEN];
} pmi_ctx_t;
#define PMI_CTX_MAGIC 0xcafefaad

static pmi_ctx_t *ctx = NULL;

enum {
    PMI_TRACE_INIT          = 0x01,
    PMI_TRACE_PARAM         = 0x02,
    PMI_TRACE_KVS           = 0x04,
    PMI_TRACE_KVS_GET       = 0x08,
    PMI_TRACE_KVS_PUT       = 0x10,
    PMI_TRACE_BARRIER       = 0x20,
    PMI_TRACE_CLIQUE        = 0x40,
    PMI_TRACE_UNIMPL        = 0x80,
};
static int pmi_trace = 0;
#define trace(n, fmt, ...) do { \
    if ((n) & pmi_trace) msg (fmt, ##__VA_ARGS__); \
} while (0)
#define trace_simple(n, ctx) do { \
    if ((n) & pmi_trace) msg ("%d:%s", (ctx) ? (ctx)->rank : -1, __FUNCTION__);\
} while (0)

static int _env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 0) : dflt;
}

static int _strtoia (char *s, int *ia, int ia_len)
{
    char *next;
    int n, len = 0;

    while (*s) {
        n = strtoul (s, &next, 10);
        s = *next == '\0' ? next : next + 1;
        if (ia) {
            if (ia_len == len)
                break;
            ia[len] = n;
        }
        len++;
    }
    return len;
}

static int _env_getints (char *name, int **iap, int *lenp,
                         int dflt_ia[], int dflt_len)
{
    char *s = getenv (name);
    int *ia;
    int len;

    if (s) {
        len = _strtoia (s, NULL, 0);
        ia = malloc (len * sizeof (int));
        if (!ia)
            return -1;
        (void)_strtoia (s, ia, len);
    } else {
        ia = malloc (dflt_len * sizeof (int));
        if (!ia)
            return -1;
        for (len = 0; len < dflt_len; len++)
            ia[len] = dflt_ia[len];            
    }
    *lenp = len;
    *iap = ia;
    return 0;
}

#if FORCE_HASH
static int _key_tostore (const char *kvsname, const char *key, char **kp)
{
    const char *p;
    int n;

    p = key;
    while (*p && !isdigit (*p))
        p++;
    if (p) {
        n = strtoul (p, NULL, 10);
        if (asprintf (kp, "%s:{%d}%s", kvsname, n, key) < 0)
            return -1;
    } else {
        if (asprintf (kp, "%s:%s", kvsname, key) < 0)
            return -1;
    }
    return 0;
}
#else
static int _key_tostore (const char *kvsname, const char *key, char **kp)
{
    return asprintf (kp, "%s:%s", kvsname, key);
}
#endif

int PMI_Init( int *spawned )
{
    int dflt_clique_ranks[] = { 0 };
    int dflt_clique_size = 1;

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
    pmi_trace = _env_getint ("PMI_TRACE", 0);
    trace_simple (PMI_TRACE_INIT, ctx);
    ctx->universe_size = ctx->size;
    ctx->appnum = _env_getint ("SLURM_JOB_ID", 1);
    ctx->barrier_num = 0;
    if (_env_getints ("SLURM_GTIDS", &ctx->clique_ranks, &ctx->clique_size,
                                      dflt_clique_ranks, dflt_clique_size) < 0)
        goto nomem;
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "%d.%d", ctx->appnum,
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
    trace_simple (PMI_TRACE_INIT, ctx);
    if (initialized == NULL)
        return PMI_ERR_INVALID_ARG;

    *initialized = ctx ? PMI_TRUE : PMI_FALSE;
    return PMI_SUCCESS;
}

int PMI_Finalize( void )
{
    trace_simple (PMI_TRACE_INIT, ctx);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (ctx->cctx)
        cmb_fini (ctx->cctx);
    if (ctx->clique_ranks)
        free (ctx->clique_ranks);
    memset (ctx, 0, sizeof (pmi_ctx_t));
    free (ctx);
    ctx = NULL;

    return PMI_SUCCESS;
}

int PMI_Get_size( int *size )
{
    trace_simple (PMI_TRACE_PARAM, ctx);
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
    trace_simple (PMI_TRACE_PARAM, ctx);
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
    trace_simple (PMI_TRACE_PARAM, ctx);
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
    trace_simple (PMI_TRACE_PARAM, ctx);
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
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Unpublish_name( const char service_name[] )
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Lookup_name( const char service_name[], char port[] )
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Barrier( void )
{
    char *name = NULL;

    trace_simple (PMI_TRACE_BARRIER, ctx);
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
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_KVS_Get_my_name( char kvsname[], int length )
{
    trace_simple (PMI_TRACE_KVS, ctx);
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
    trace_simple (PMI_TRACE_KVS, ctx);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KVSNAMELEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max( int *length )
{
    trace_simple (PMI_TRACE_KVS, ctx);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KEYLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max( int *length )
{
    trace_simple (PMI_TRACE_KVS, ctx);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_VALLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Put( const char kvsname[], const char key[], const char value[])
{
    char *xkey = NULL;

    trace (PMI_TRACE_KVS_PUT, "%d:%s %s:%s = %s", ctx ? ctx->rank : -1,
           __FUNCTION__, kvsname, key, value);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;

    if (_key_tostore (kvsname, key, &xkey) < 0)
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

    trace (PMI_TRACE_KVS_PUT, "%d:%s %s", ctx ? ctx->rank : -1,
           __FUNCTION__, kvsname);
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

    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL)
        return PMI_ERR_INVALID_ARG;

    if (_key_tostore (kvsname, key, &xkey) < 0)
        return PMI_ERR_NOMEM;

    val = cmb_kvs_get (ctx->cctx, xkey);
    trace (PMI_TRACE_KVS_GET, "%d:%s %s:%s = %s", ctx ? ctx->rank : -1,
           __FUNCTION__, kvsname, key,
           val ? val : errno == 0 ? "[nonexistent key]" : "[error]");
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
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Get_id( char id_str[], int length )
{
    trace_simple (PMI_TRACE_PARAM, ctx);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (id_str == NULL || length < strlen (ctx->kvsname) + 1)
        return PMI_ERR_INVALID_ARG;

    snprintf (id_str, length + 1, "%s", ctx->kvsname);
    return PMI_SUCCESS;
}

int PMI_Get_kvs_domain_id( char id_str[], int length )
{
    trace_simple (PMI_TRACE_PARAM, ctx);
    return PMI_Get_id (id_str, length);
}

int PMI_Get_id_length_max( int *length )
{
    trace_simple (PMI_TRACE_PARAM, ctx);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;

    *length = strlen (ctx->kvsname) + 1;
    return PMI_SUCCESS;
}

int PMI_Get_clique_size( int *size )
{
    trace_simple (PMI_TRACE_CLIQUE, ctx);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    *size = ctx->clique_size;
    return PMI_SUCCESS;
}

int PMI_Get_clique_ranks( int ranks[], int length)
{
    int i;

    trace_simple (PMI_TRACE_CLIQUE, ctx);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length != ctx->clique_size)
        return PMI_ERR_INVALID_ARG;
    for (i = 0; i < length; i++)
        ranks[i] = ctx->clique_ranks[i];
    return PMI_SUCCESS;
}

int PMI_KVS_Create( char kvsname[], int length )
{
    trace_simple (PMI_TRACE_KVS, ctx);
    return PMI_SUCCESS;
}

int PMI_KVS_Destroy( const char kvsname[] )
{
    trace_simple (PMI_TRACE_KVS, ctx);
    return PMI_SUCCESS;
}

int PMI_KVS_Iter_first(const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    if (pmi_trace & PMI_TRACE_UNIMPL)
        msg ("PMI-TRACE %d:%s", ctx ? ctx->rank : -1, __FUNCTION__);
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_KVS_Iter_next(const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Parse_option(int num_args, char *args[], int *num_parsed,
                        PMI_keyval_t **keyvalp, int *size)
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Args_to_keyval(int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size)
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Free_keyvals(PMI_keyval_t keyvalp[], int size)
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

int PMI_Get_options(char *str, int *length)
{
    trace_simple (PMI_TRACE_UNIMPL, ctx);
    return PMI_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
