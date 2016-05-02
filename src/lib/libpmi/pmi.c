/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This library is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the license, or (at
 *  your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <json.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/nodeset.h"

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

typedef struct {
    int magic;
    int spawned;
    int size;
    int rank;
    nodeset_t *clique;
    int universe_size;
    int appnum;
    int barrier_num;
    char barrier_name[PMI_MAX_KVSNAMELEN + 16];
    uint32_t cmb_rank;
    flux_t h;
    char kvsname[PMI_MAX_KVSNAMELEN];
    char key[PMI_MAX_KVSNAMELEN + PMI_MAX_KEYLEN + 2];
    char val[PMI_MAX_VALLEN + 1];
    int trace;
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

struct errmap {
    int err;
    const char *s;
};

static struct errmap pmi_errstr[] = {
    { PMI_SUCCESS,                  "SUCCESS" },
    { PMI_FAIL,                     "FAIL" },
    { PMI_ERR_INIT,                 "ERR_INIT" },
    { PMI_ERR_NOMEM,                "ERR_NOMEM" },
    { PMI_ERR_INVALID_ARG,          "ERR_INVALID_ARG" },
    { PMI_ERR_INVALID_KEY,          "ERR_INVALID_KEY" },
    { PMI_ERR_INVALID_KEY_LENGTH,   "ERR_INVALID_KEY_LENGTH" },
    { PMI_ERR_INVALID_VAL,          "ERR_INVALID_VAL" },
    { PMI_ERR_INVALID_VAL_LENGTH,   "ERR_INVALID_VAL_LENGTH" },
    { PMI_ERR_INVALID_LENGTH,       "ERR_INVALID_LENGTH" },
    { PMI_ERR_INVALID_NUM_ARGS,     "ERR_INVALID_NUM_ARGS" },
    { PMI_ERR_INVALID_ARGS,         "ERR_INVALID_ARGS" },
    { PMI_ERR_INVALID_NUM_PARSED,   "ERR_INVALID_NUM_PARSED" },
    { PMI_ERR_INVALID_KEYVALP,      "ERR_INVALID_KEYVALP" },
    { PMI_ERR_INVALID_SIZE,         "ERR_INVALID_SIZE" },
    { -1, NULL },
};

static const char *pmi_strerror (int errnum)
{
    int i = 0;
    static char buf[16];
    while (pmi_errstr[i].s != NULL) {
        if (errnum == pmi_errstr[i].err)
            return pmi_errstr[i].s;
        i++;
    }
    snprintf (buf, sizeof (buf), "%d", errnum);
    return buf;
}

static inline void trace (int tracebit, int ret, const char *func)
{
    const char *s = pmi_strerror (ret);

    if (ctx == NULL) {
        if (ret != PMI_SUCCESS ) {
            fprintf (stderr, "%s (pre-init) rc=%s", func, s);
            fflush (stderr);
        }
        return;
    }
    if (!(tracebit & ctx->trace))
        return;

    switch (tracebit) {
        case PMI_TRACE_KVS_GET:
        case PMI_TRACE_KVS_PUT:
            flux_log (ctx->h, LOG_DEBUG, "%s (%s = \"%s\") = %s",
                      func, ctx->key, ctx->val, s);
            break;
        case PMI_TRACE_BARRIER:
            flux_log (ctx->h, LOG_DEBUG, "%s (%s, %d) = %s",
                      func, ctx->barrier_name, ctx->universe_size, s);
            break;
        case PMI_TRACE_INIT:
        case PMI_TRACE_PARAM:
        case PMI_TRACE_KVS:
        case PMI_TRACE_CLIQUE:
            flux_log (ctx->h, LOG_DEBUG, "%s = %s", func, s);
            break;
        case PMI_TRACE_UNIMPL:
            flux_log (ctx->h, LOG_DEBUG, "%s = %s (unimplemented)", func, s);
            break;
    }
}

#define return_trace(n,ret) do { \
    trace ((n), (ret), __FUNCTION__);\
    return (ret);\
} while (0);

static int env_getint (char *name, int dflt)
{
    char *s = getenv (name);
    return s ? strtol (s, NULL, 0) : dflt;
}

static void destroy_ctx (void)
{
    if (ctx) {
        assert (ctx->magic == PMI_CTX_MAGIC);
        if (ctx->h)
            flux_close (ctx->h);
        if (ctx->clique)
            nodeset_destroy (ctx->clique);
        memset (ctx, 0, sizeof (pmi_ctx_t));
        free (ctx);
        ctx = NULL;
    }
}

int PMI_Init (int *spawned)
{
    int ret = PMI_FAIL;
    const char *local_ranks;

    if (ctx) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    if (spawned == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    ctx = malloc (sizeof (pmi_ctx_t));
    if (ctx == NULL) {
        ret = PMI_ERR_NOMEM;
        goto done;
    }
    memset (ctx, 0, sizeof (pmi_ctx_t));
    ctx->magic = PMI_CTX_MAGIC;

    ctx->trace = env_getint ("PMI_TRACE", 0);

    ctx->size = env_getint ("FLUX_JOB_SIZE", 1);
    ctx->rank = env_getint ("FLUX_TASK_RANK", 0);
    ctx->appnum = env_getint ("FLUX_JOB_ID", 1);
    if (!(local_ranks = getenv ("FLUX_LOCAL_RANKS")))
        local_ranks = "[0]";
    if (!(ctx->clique = nodeset_create_string (local_ranks))) {
        fprintf (stderr, "nodeset_create_string failed: %s", local_ranks);
        goto done_destroy;
    }
    ctx->spawned = PMI_FALSE;
    ctx->universe_size = ctx->size;
    ctx->barrier_num = 0;
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "lwj.%d.pmi", ctx->appnum);
    if (!(ctx->h = flux_open (NULL, 0))) {
        fprintf (stderr, "flux_open: %s", strerror (errno));
        goto done_destroy;
    }
    if (flux_get_rank (ctx->h, &ctx->cmb_rank) < 0) {
        fprintf (stderr, "flux_get_rank: %s", strerror (errno));
        goto done_destroy;
    }
    flux_log_set_facility (ctx->h, "libpmi");
    *spawned = ctx->spawned;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_INIT, ret);
done_destroy:
    destroy_ctx ();
    return_trace (PMI_TRACE_INIT, ret);
}

int PMI_Initialized (int *initialized)
{
    int ret = PMI_FAIL;

    if (initialized == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *initialized = ctx ? PMI_TRUE : PMI_FALSE;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_INIT, ret);
}

int PMI_Finalize (void)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    ret = PMI_SUCCESS;
done:
    trace (PMI_TRACE_INIT, ret, __FUNCTION__);
    destroy_ctx ();
    return ret;
}

int PMI_Get_size (int *size)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (size == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *size = ctx->size;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_rank (int *rank)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (rank == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *rank = ctx->rank;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_universe_size (int *size)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (size == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *size = ctx->universe_size;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_appnum (int *appnum)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (appnum == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *appnum = ctx->appnum;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Publish_name (const char service_name[], const char port[])
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_Unpublish_name (const char service_name[])
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_Lookup_name (const char service_name[], char port[])
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

/* PMI_Barrier is co-opted as the KVS collective fence (citation required).
 */
int PMI_Barrier (void)
{
    int ret = PMI_FAIL;
    int n;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);

    n = snprintf (ctx->barrier_name, sizeof (ctx->barrier_name), "%s:%d",
                        ctx->kvsname, ctx->barrier_num++);
    assert (n < sizeof (ctx->barrier_name));
    if (kvs_fence (ctx->h, ctx->barrier_name, ctx->universe_size) < 0)
        goto done;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_BARRIER, ret);
}

int PMI_Abort (int exit_code, const char error_msg[])
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_KVS_Get_my_name (char kvsname[], int length)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || length < strlen (ctx->kvsname) + 1) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    strcpy (kvsname, ctx->kvsname);
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS, ret);
}

int PMI_KVS_Get_name_length_max (int *length)
{
    int ret = PMI_FAIL;

    if (length == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *length = PMI_MAX_KVSNAMELEN;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS, ret);
}

int PMI_KVS_Get_key_length_max (int *length)
{
    int ret = PMI_FAIL;

    if (length == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *length = PMI_MAX_KEYLEN;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS, ret);
}

int PMI_KVS_Get_value_length_max (int *length)
{
    int ret = PMI_FAIL;

    if (length == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *length = PMI_MAX_VALLEN;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS, ret);
}

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[])
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    if (snprintf (ctx->key, sizeof (ctx->key), "%s.%s",
                                    kvsname, key) >= sizeof (ctx->key)) {
        ret = PMI_ERR_INVALID_KEY_LENGTH;
        goto done;
    }
    if (snprintf (ctx->val, sizeof (ctx->val), "%s",
                                    value) >= sizeof (ctx->val)) {
        ret = PMI_ERR_INVALID_VAL_LENGTH;
        goto done;
    }
    if (kvs_put_string (ctx->h, ctx->key, value) < 0)
        goto done;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS_PUT, ret);
}

/* This is a no-op.  The "commit" actually takes place in PMI_Barrier
 * as a collective operation.
 */
int PMI_KVS_Commit (const char kvsname[])
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_KVS, ret);
}

int PMI_KVS_Get (const char kvsname[], const char key[], char value[],
                 int length)
{
    int ret = PMI_FAIL;
    char *val = NULL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    if (snprintf (ctx->key, sizeof (ctx->key), "%s.%s",
                                kvsname, key) >= sizeof (ctx->key)) {
        ret = PMI_ERR_INVALID_KEY_LENGTH;
        goto done;
    }
    if (kvs_get_string (ctx->h, ctx->key, &val) < 0) {
        if (errno == ENOENT)
            ret = PMI_ERR_INVALID_KEY;
        goto done;
    }
    if (snprintf (ctx->val, sizeof (ctx->val), "%s", val) >= sizeof (ctx->val)
            || snprintf (value, length, "%s", val) >= length) {
        ret = PMI_ERR_INVALID_VAL_LENGTH;
        goto done;
    }
    ret = PMI_SUCCESS;
done:
    if (val)
        free (val);
    return_trace (PMI_TRACE_KVS_GET, ret);
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
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

/* These functions were removed from the MPICH pmi.h by
 *
 * commit f17423ef535f562bcacf981a9f7e379838962c6e
 * Author: Pavan Balaji <balaji@mcs.anl.gov>
 * Date:   Fri Jan 28 22:40:33 2011 +0000
 *
 *  [svn-r7858] Cleanup unused PMI functions. Note that this does not break PMI-
 *  compatibility with exiting process managers, as these functions are
 *  unused. Even if other PMI client library implementations chose to
 *  implement them, we still do not have to use them.
 *
 *  Reviewed by buntinas.
 */

int PMI_Get_id (char id_str[], int length)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (id_str == NULL || length <= 1) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    if (snprintf (id_str, length + 1, "%d", ctx->appnum) == length + 1) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_kvs_domain_id (char id_str[], int length)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (id_str == NULL || length < strlen (ctx->kvsname) + 1) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    snprintf (id_str, length + 1, "%s", ctx->kvsname);
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_id_length_max (int *length)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length == NULL) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    *length = strlen (ctx->kvsname) + 1;
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_PARAM, ret);
}

int PMI_Get_clique_size (int *size)
{
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    *size = nodeset_count (ctx->clique);
    ret = PMI_SUCCESS;
done:
    return_trace (PMI_TRACE_CLIQUE, ret);
}

int PMI_Get_clique_ranks (int ranks[], int length)
{
    int i;
    nodeset_iterator_t *itr = NULL;
    int ret = PMI_FAIL;

    if (ctx == NULL) {
        ret = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length < nodeset_count (ctx->clique)) {
        ret = PMI_ERR_INVALID_ARG;
        goto done;
    }
    itr = nodeset_iterator_create (ctx->clique);
    for (i = 0; i < length; i++) {
        ranks[i] = nodeset_next (itr);
        if (ranks[i] == NODESET_EOF)
            goto done;
    }
    ret = PMI_SUCCESS;
done:
    if (itr)
        nodeset_iterator_destroy (itr);
    return_trace (PMI_TRACE_CLIQUE, ret);
}

int PMI_KVS_Create (char kvsname[], int length)
{
    return_trace (PMI_TRACE_KVS, PMI_SUCCESS);
}

int PMI_KVS_Destroy (const char kvsname[])
{
    return_trace (PMI_TRACE_KVS, PMI_SUCCESS);
}

int PMI_KVS_Iter_first (const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_KVS_Iter_next (const char kvsname[], char key[], int key_len,
                       char val[], int val_len)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

/* These functions were removed from the MPICH pmi.h by
 *
 * commit 52c462d2be6a8d0720788d36e1e096e991dcff38
 * Author: William Gropp <gropp@mcs.anl.gov>
 * Date:   Fri May 1 17:53:02 2009 +0000
 *
 * [svn-r4377] removed dead and invalid PMI routines from pmi.h and from the si
 */

int PMI_Parse_option (int num_args, char *args[], int *num_parsed,
                      PMI_keyval_t **keyvalp, int *size)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_Args_to_keyval (int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_Free_keyvals (PMI_keyval_t keyvalp[], int size)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

int PMI_Get_options (char *str, int *length)
{
    return_trace (PMI_TRACE_UNIMPL, PMI_FAIL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
