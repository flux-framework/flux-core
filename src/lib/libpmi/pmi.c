/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
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
#include "src/common/libutil/env.h"

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
    int *clique_ranks;
    int clique_size;
    int universe_size;
    int appnum;
    int barrier_num;
    int cmb_rank;
    flux_t fctx;
    char kvsname[PMI_MAX_KVSNAMELEN];
    bool trace;
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

static inline void trace (int flags, const char *fmt, ...)
{
    va_list ap;

    if (ctx && (flags & ctx->trace)) {
        char buf[64];
        snprintf (buf, sizeof (buf), "[%d.%d.%d] %s", ctx->cmb_rank,
                  ctx->appnum, ctx->rank, fmt);
        va_start (ap, fmt);
        flux_vlog (ctx->fctx, LOG_DEBUG, buf, ap);
        va_end (ap);
    }
}
#define trace_simple(n) do { \
    trace ((n), "%s", __FUNCTION__);\
} while (0)

int PMI_Init (int *spawned)
{
    int dflt_clique_ranks[] = { 0 };
    int dflt_clique_size = 1;

    log_init ("flux-pmi");
    if (spawned == NULL)
        return PMI_ERR_INVALID_ARG;
    if (ctx)
        goto fail;
    ctx = malloc (sizeof (pmi_ctx_t));
    if (ctx == NULL)
        goto nomem;
    memset (ctx, 0, sizeof (pmi_ctx_t));

    ctx->trace = env_getint ("PMI_TRACE", 0);

    ctx->size = env_getint ("FLUX_LWJ_NTASKS", 1);
    ctx->rank = env_getint ("FLUX_LWJ_TASK_ID", 0);
    ctx->appnum = env_getint ("FLUX_LWJ_ID", 1);
    if (env_getints ("FLUX_LWJ_GTIDS", &ctx->clique_ranks, &ctx->clique_size,
                     dflt_clique_ranks, dflt_clique_size) < 0)
        goto fail;

    ctx->magic = PMI_CTX_MAGIC;
    ctx->spawned = PMI_FALSE;
    ctx->universe_size = ctx->size;
    ctx->barrier_num = 0;
    snprintf (ctx->kvsname, sizeof (ctx->kvsname), "%d", ctx->appnum);
    if (!(ctx->fctx = flux_open (NULL, 0))) {
        err ("flux_open");
        goto fail;
    }
    flux_log_set_facility (ctx->fctx, "pmi");
    ctx->cmb_rank = flux_rank (ctx->fctx);
    trace_simple (PMI_TRACE_INIT);
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

int PMI_Initialized (int *initialized)
{
    trace_simple (PMI_TRACE_INIT);
    if (initialized == NULL)
        return PMI_ERR_INVALID_ARG;

    *initialized = ctx ? PMI_TRUE : PMI_FALSE;
    return PMI_SUCCESS;
}

int PMI_Finalize (void)
{
    trace_simple (PMI_TRACE_INIT);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (ctx->fctx)
        flux_close (ctx->fctx);
    if (ctx->clique_ranks)
        free (ctx->clique_ranks);
    memset (ctx, 0, sizeof (pmi_ctx_t));
    free (ctx);
    ctx = NULL;

    return PMI_SUCCESS;
}

int PMI_Get_size (int *size)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (size == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *size = ctx->size;
    return PMI_SUCCESS;
}

int PMI_Get_rank (int *rank)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (rank == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *rank = ctx->rank;
    return PMI_SUCCESS;
}

int PMI_Get_universe_size (int *size)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (size == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *size = ctx->universe_size;
    return PMI_SUCCESS;
}

int PMI_Get_appnum (int *appnum)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    if (appnum == NULL)
        return PMI_ERR_INVALID_ARG;
    assert (ctx->magic == PMI_CTX_MAGIC);

    *appnum = ctx->appnum;
    return PMI_SUCCESS;
}

int PMI_Publish_name (const char service_name[], const char port[])
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Unpublish_name (const char service_name[])
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Lookup_name (const char service_name[], char port[])
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

/* PMI_Barrier is co-opted as the KVS collective fence (citation required).
 */
int PMI_Barrier (void)
{
    char *name = NULL;
    int rc = PMI_SUCCESS;

    trace_simple (PMI_TRACE_BARRIER);
    if (ctx == NULL) {
        rc = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);

    if (asprintf (&name, "%s:%d", ctx->kvsname, ctx->barrier_num++) < 0) {
        rc = PMI_ERR_NOMEM;
        goto done;
    }
    if (kvs_fence (ctx->fctx, name, ctx->universe_size) < 0) {
        rc = PMI_FAIL;
        goto done;
    }
done:
    if (name)
        free (name);
    return rc;
}

int PMI_Abort (int exit_code, const char error_msg[])
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_KVS_Get_my_name (char kvsname[], int length)
{
    trace_simple (PMI_TRACE_KVS);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || length < strlen (ctx->kvsname) + 1)
        return PMI_ERR_INVALID_ARG;

    strcpy (kvsname, ctx->kvsname);
    return PMI_SUCCESS;
}

int PMI_KVS_Get_name_length_max (int *length)
{
    trace_simple (PMI_TRACE_KVS);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KVSNAMELEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max (int *length)
{
    trace_simple (PMI_TRACE_KVS);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_KEYLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max (int *length)
{
    trace_simple (PMI_TRACE_KVS);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;
    *length = PMI_MAX_VALLEN;
    return PMI_SUCCESS;
}

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[])
{
    char *xkey = NULL;
    int rc = PMI_SUCCESS;

    trace (PMI_TRACE_KVS_PUT, "%s pmi.%s.%s = %s",
           __FUNCTION__, kvsname, key, value);

    if (ctx == NULL) {
        rc = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL) {
        rc = PMI_ERR_INVALID_ARG;
        goto done;
    }
    if (asprintf (&xkey, "pmi.%s.%s", kvsname, key) < 0) {
        rc = PMI_ERR_NOMEM;
        goto done;
    }
    if (kvs_put_string (ctx->fctx, xkey, value) < 0) {
        rc = PMI_FAIL;
        goto done;
    }
done:
    if (xkey)
        free (xkey);
    return rc;
}

/* This is a no-op.  The "commit" actually takes place in PMI_Barrier
 * as a collective operation.
 */
int PMI_KVS_Commit (const char kvsname[])
{
    trace (PMI_TRACE_KVS_PUT, "%s pmi.%s", __FUNCTION__, kvsname);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL)
        return PMI_ERR_INVALID_ARG;
    return PMI_SUCCESS;
}

int PMI_KVS_Get (const char kvsname[], const char key[], char value[],
                 int length)
{
    char *xkey = NULL;
    int rc = PMI_SUCCESS;
    char *val = NULL;

    trace (PMI_TRACE_KVS_GET, "%s pmi.%s.%s", __FUNCTION__, kvsname, key);
    if (ctx == NULL) {
        rc = PMI_ERR_INIT;
        goto done;
    }
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (kvsname == NULL || key == NULL || value == NULL) {
        rc = PMI_ERR_INVALID_ARG;
        goto done;
    }
    if (asprintf (&xkey, "pmi.%s.%s", kvsname, key) < 0) {
        rc = PMI_ERR_NOMEM;
        goto done;
    }
    if (kvs_get_string (ctx->fctx, xkey, &val) < 0) {
        if (errno == ENOENT)
            rc = PMI_ERR_INVALID_KEY;
        else
            rc = PMI_FAIL;
        goto done;
    }
    snprintf (value, length, "%s", val);
done:
    if (xkey)
        free (xkey);
    if (val)
        free (val);
    return rc;
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
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Get_id (char id_str[], int length)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (id_str == NULL || length < strlen (ctx->kvsname) + 1)
        return PMI_ERR_INVALID_ARG;

    snprintf (id_str, length + 1, "%s", ctx->kvsname);
    return PMI_SUCCESS;
}

int PMI_Get_kvs_domain_id (char id_str[], int length)
{
    trace_simple (PMI_TRACE_PARAM);
    return PMI_Get_id (id_str, length);
}

int PMI_Get_id_length_max (int *length)
{
    trace_simple (PMI_TRACE_PARAM);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length == NULL)
        return PMI_ERR_INVALID_ARG;

    *length = strlen (ctx->kvsname) + 1;
    return PMI_SUCCESS;
}

int PMI_Get_clique_size (int *size)
{
    trace_simple (PMI_TRACE_CLIQUE);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    *size = ctx->clique_size;
    return PMI_SUCCESS;
}

int PMI_Get_clique_ranks (int ranks[], int length)
{
    int i;

    trace_simple (PMI_TRACE_CLIQUE);
    if (ctx == NULL)
        return PMI_ERR_INIT;
    assert (ctx->magic == PMI_CTX_MAGIC);
    if (length != ctx->clique_size)
        return PMI_ERR_INVALID_ARG;
    for (i = 0; i < length; i++)
        ranks[i] = ctx->clique_ranks[i];
    return PMI_SUCCESS;
}

int PMI_KVS_Create (char kvsname[], int length)
{
    trace_simple (PMI_TRACE_KVS);
    return PMI_SUCCESS;
}

int PMI_KVS_Destroy (const char kvsname[])
{
    trace_simple (PMI_TRACE_KVS);
    return PMI_SUCCESS;
}

int PMI_KVS_Iter_first (const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_KVS_Iter_next (const char kvsname[], char key[], int key_len,
                       char val[], int val_len)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Parse_option (int num_args, char *args[], int *num_parsed,
                      PMI_keyval_t **keyvalp, int *size)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Args_to_keyval (int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Free_keyvals (PMI_keyval_t keyvalp[], int size)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

int PMI_Get_options (char *str, int *length)
{
    trace_simple (PMI_TRACE_UNIMPL);
    return PMI_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
