/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pmi.c - canonical PMI-1 API/ABI for libpmi.so
 *
 * A client (e.g. an MPI runtime) may use PMI in one of three modes:
 * 1) link with this library in the normal way
 * 2) dlopen() this library and use standard ABI
 * 3) Interpret PMI environ variables and bypass the library,
 *    speaking the standard PMI-1 wire protocol directly.
 *
 * This library only talks to the process manager via the standard
 * wire protocol.
 *
 * PMI_Init() will fail if PMI_FD, PMI_RANK, or PMI_SIZE is unset.
 * It is up to the caller to fall back to singleton operation, if desired.
 *
 * See Flux RFC 13 for more detail.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <stdarg.h>

#include "pmi.h"
#include "pmi_strerror.h"
#include "simple_client.h"
#include "clique.h"

static struct pmi_simple_client *pmi_global_ctx;

#define DPRINTF(fmt,...) do { \
    if (pmi_global_ctx && pmi_global_ctx->debug) \
        fprintf (stderr, fmt, ##__VA_ARGS__); \
} while (0)

#define DRETURN(rc) do { \
    DPRINTF ("%d: %s rc=%d %s\n", \
            pmi_global_ctx ? pmi_global_ctx->rank : -1, \
            __func__, (rc), \
            rc == PMI_SUCCESS ? "" : pmi_strerror (rc)); \
    return (rc); \
} while (0);


int PMI_Init (int *spawned)
{
    int result = PMI_FAIL;
    const char *pmi_debug;
    struct pmi_simple_client *ctx;

    pmi_debug = getenv ("FLUX_PMI_DEBUG");
    if (!pmi_debug)
        pmi_debug = getenv ("PMI_DEBUG");

    if (pmi_global_ctx)
        return PMI_ERR_INIT;

    ctx = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                       getenv ("PMI_RANK"),
                                       getenv ("PMI_SIZE"),
                                       pmi_debug,
                                       getenv ("PMI_SPAWNED"));
    if (!ctx) {
        if (errno == ENOMEM)
            return PMI_ERR_NOMEM;
        return PMI_FAIL;
    }

    result = pmi_simple_client_init (ctx);
    if (result != PMI_SUCCESS) {
        pmi_simple_client_destroy (ctx);
        return result;
    }
    pmi_global_ctx = ctx;
    if (spawned)
        *spawned = ctx->spawned;
    DRETURN (result);
}

int PMI_Initialized (int *initialized)
{
    int result = PMI_SUCCESS;

    if (initialized)
        *initialized = pmi_global_ctx ? pmi_global_ctx->initialized : 0;
    else
        result = PMI_ERR_INVALID_ARG;
    DRETURN (result);
}

int PMI_Finalize (void)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx) {
        result = pmi_simple_client_finalize (pmi_global_ctx);
        pmi_simple_client_destroy (pmi_global_ctx);
        pmi_global_ctx = NULL;
    }
    DRETURN (result);
}

int PMI_Abort (int exit_code, const char error_msg[])
{
    fprintf (stderr, "PMI_Abort: (%d) %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             error_msg);
    exit (exit_code);
    /*NOTREACHED*/
    DRETURN (PMI_SUCCESS);
}

int PMI_Get_size (int *size)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx) {
        if (!size)
            result = PMI_ERR_INVALID_ARG;
        else {
            *size = pmi_global_ctx->size;
            result = PMI_SUCCESS;
        }
    }
    DRETURN (result);
}

int PMI_Get_rank (int *rank)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx) {
        if (!rank)
            result = PMI_ERR_INVALID_ARG;
        else {
            *rank = pmi_global_ctx->rank;
            result = PMI_SUCCESS;
        }
    }
    DRETURN (result);
}

int PMI_Get_universe_size (int *size)
{
    int result;

    result = pmi_simple_client_get_universe_size (pmi_global_ctx, size);
    DRETURN (result);
}

int PMI_Get_appnum (int *appnum)
{
    int result;

    result = pmi_simple_client_get_appnum (pmi_global_ctx, appnum);
    DRETURN (result);
}

int PMI_KVS_Get_my_name (char kvsname[], int length)
{
    int result;

    result = pmi_simple_client_kvs_get_my_name (pmi_global_ctx,
                                                kvsname,
                                                length);

    DPRINTF ("%d: %s (\"%s\", %d) rc=%d %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             __func__,
             result == PMI_SUCCESS ? kvsname : "",
             length,
             result,
             result == PMI_SUCCESS ? "" : pmi_strerror (result));

    return result;
}

int PMI_KVS_Get_name_length_max (int *length)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx && pmi_global_ctx->initialized) {
        if (!length)
            result = PMI_ERR_INVALID_ARG;
        else {
            *length = pmi_global_ctx->kvsname_max;
            result = PMI_SUCCESS;
        }
    }
    DRETURN (result);
}

int PMI_KVS_Get_key_length_max (int *length)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx && pmi_global_ctx->initialized) {
        if (!length)
            result = PMI_ERR_INVALID_ARG;
        else {
            *length = pmi_global_ctx->keylen_max;
            result = PMI_SUCCESS;
        }
    }
    DRETURN (result);
}

int PMI_KVS_Get_value_length_max (int *length)
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx && pmi_global_ctx->initialized) {
        if (!length)
            result = PMI_ERR_INVALID_ARG;
        else {
            *length = pmi_global_ctx->vallen_max;
            result = PMI_SUCCESS;
        }
    }
    DRETURN (result);
}

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[])
{
    int result;

    result = pmi_simple_client_kvs_put (pmi_global_ctx, kvsname, key, value);

    DPRINTF ("%d: %s (\"%s\", \"%s\", \"%s\") rc=%d %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             __func__,
             kvsname ? kvsname : "NULL",
             key ? key : "NULL",
             value ? value : "NULL",
             result,
             result == PMI_SUCCESS ? "" : pmi_strerror (result));

    return result;
}

int PMI_KVS_Get (const char kvsname[], const char key[],
                 char value[], int length)
{
    int result;

    result = pmi_simple_client_kvs_get (pmi_global_ctx, kvsname, key,
                                        value, length);

    DPRINTF ("%d: %s (\"%s\", \"%s\", \"%s\") rc=%d %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             __func__,
             kvsname ? kvsname : "NULL",
             key ? key : "NULL",
             result == PMI_SUCCESS ? value : "",
             result,
             result == PMI_SUCCESS ? "" : pmi_strerror (result));

    return result;
}

int PMI_KVS_Commit (const char kvsname[])
{
    int result = PMI_ERR_INIT;

    if (pmi_global_ctx && pmi_global_ctx->initialized) {
        if (!kvsname)
            result = PMI_ERR_INVALID_ARG;
        else
            result = PMI_SUCCESS; // no-op in this implementation
    }
    DRETURN (result);
}

int PMI_Barrier (void)
{
    int result = PMI_ERR_INIT;

    result = pmi_simple_client_barrier (pmi_global_ctx);
    DRETURN (result);
}

int PMI_Publish_name (const char service_name[], const char port[])
{
    DRETURN (PMI_FAIL);
}

int PMI_Unpublish_name (const char service_name[])
{
    DRETURN (PMI_FAIL);
}

int PMI_Lookup_name (const char service_name[], char port[])
{
    DRETURN (PMI_FAIL);
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
    DRETURN (PMI_FAIL);
}

/* Old API funcs - signatures needed for ABI compliance.
 */

int PMI_Get_clique_ranks (int ranks[], int length)
{
    int result;

    result = pmi_simple_client_get_clique_ranks (pmi_global_ctx, ranks, length);
    DRETURN (result);
}

int PMI_Get_clique_size (int *size)
{
    int result;

    result = pmi_simple_client_get_clique_size (pmi_global_ctx, size);
    DRETURN (result);
}

int PMI_Get_id_length_max (int *length)
{
    int result;

    result  = PMI_KVS_Get_name_length_max (length);
    DRETURN (result);
}

int PMI_Get_id (char kvsname[], int length)
{
    int result;

    result = PMI_KVS_Get_my_name (kvsname, length);
    DRETURN (result);
}

int PMI_Get_kvs_domain_id (char kvsname[], int length)
{
    int result;

    result = PMI_KVS_Get_my_name (kvsname, length);
    DRETURN (result);
}

int PMI_KVS_Create (char kvsname[], int length)
{
    DRETURN (PMI_FAIL);
}

int PMI_KVS_Destroy (const char kvsname[])
{
    DRETURN (PMI_FAIL);
}

int PMI_KVS_Iter_first (const char kvsname[], char key[], int key_len,
                        char val[], int val_len)
{
    DRETURN (PMI_FAIL);
}

int PMI_KVS_Iter_next (const char kvsname[], char key[], int key_len,
                       char val[], int val_len)
{
    DRETURN (PMI_FAIL);
}

int PMI_Parse_option (int num_args, char *args[], int *num_parsed,
                      PMI_keyval_t **keyvalp, int *size)
{
    DRETURN (PMI_FAIL);
}

int PMI_Args_to_keyval (int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size)
{
    DRETURN (PMI_FAIL);
}

int PMI_Free_keyvals (PMI_keyval_t keyvalp[], int size)
{
    DRETURN (PMI_FAIL);
}

int PMI_Get_options (char *str, int *length)
{
    DRETURN (PMI_FAIL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
