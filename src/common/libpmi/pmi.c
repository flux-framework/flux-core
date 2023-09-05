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

#include "pmi.h"
#include "pmi_strerror.h"
#include "simple_client.h"

static struct pmi_simple_client *pmi_global_ctx;

int PMI_Init (int *spawned)
{
    int result;
    struct pmi_simple_client *ctx;

    if (pmi_global_ctx)
        return PMI_ERR_INIT;

    ctx = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                       getenv ("PMI_RANK"),
                                       getenv ("PMI_SIZE"),
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
    return PMI_SUCCESS;
}

int PMI_Initialized (int *initialized)
{
    if (!initialized)
        return PMI_ERR_INVALID_ARG;
    *initialized = pmi_global_ctx ? pmi_global_ctx->initialized : 0;
    return PMI_SUCCESS;
}

int PMI_Finalize (void)
{
    int result;

    if (!pmi_global_ctx)
        return PMI_ERR_INIT;
    result = pmi_simple_client_finalize (pmi_global_ctx);
    pmi_simple_client_destroy (pmi_global_ctx);
    pmi_global_ctx = NULL;
    return result;
}

int PMI_Abort (int exit_code, const char *error_msg)
{
    /* pmi_simple_client_abort() only returns on error, in which case
     * we fall back to printing the message on stderr and call exit()
     * (return code not checked because we don't do anything with it)
     */
    (void) pmi_simple_client_abort (pmi_global_ctx, exit_code, error_msg);
    fprintf (stderr,
             "PMI_Abort: (%d) %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             error_msg);
    exit (exit_code);
    /*NOTREACHED*/
    return PMI_SUCCESS;
}

int PMI_Get_size (int *size)
{
    if (!pmi_global_ctx)
        return PMI_ERR_INIT;
    if (!size)
        return PMI_ERR_INVALID_ARG;
    *size = pmi_global_ctx->size;
    return PMI_SUCCESS;
}

int PMI_Get_rank (int *rank)
{
    if (!pmi_global_ctx)
        return PMI_ERR_INIT;
    if (!rank)
        return PMI_ERR_INVALID_ARG;
    *rank = pmi_global_ctx->rank;
    return PMI_SUCCESS;
}

int PMI_Get_universe_size (int *size)
{
    return pmi_simple_client_get_universe_size (pmi_global_ctx, size);
}

int PMI_Get_appnum (int *appnum)
{
    return pmi_simple_client_get_appnum (pmi_global_ctx, appnum);
}

int PMI_KVS_Get_my_name (char *kvsname, int length)
{
    return pmi_simple_client_kvs_get_my_name (pmi_global_ctx,
                                              kvsname,
                                              length);
}

int PMI_KVS_Get_name_length_max (int *length)
{
    if (!pmi_global_ctx || !pmi_global_ctx->initialized)
        return PMI_ERR_INIT;
    if (!length)
        return PMI_ERR_INVALID_ARG;
    *length = pmi_global_ctx->kvsname_max;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_key_length_max (int *length)
{
    if (!pmi_global_ctx || !pmi_global_ctx->initialized)
        return PMI_ERR_INIT;
    if (!length)
        return PMI_ERR_INVALID_ARG;
    *length = pmi_global_ctx->keylen_max;
    return PMI_SUCCESS;
}

int PMI_KVS_Get_value_length_max (int *length)
{
    if (!pmi_global_ctx || !pmi_global_ctx->initialized)
        return PMI_ERR_INIT;
    if (!length)
        return PMI_ERR_INVALID_ARG;
    *length = pmi_global_ctx->vallen_max;
    return PMI_SUCCESS;
}

int PMI_KVS_Put (const char *kvsname, const char *key, const char *value)
{
    return pmi_simple_client_kvs_put (pmi_global_ctx, kvsname, key, value);
}

int PMI_KVS_Get (const char *kvsname,
                 const char *key,
                 char *value,
                 int length)
{
    return pmi_simple_client_kvs_get (pmi_global_ctx,
                                      kvsname,
                                      key,
                                      value,
                                      length);
}

int PMI_KVS_Commit (const char *kvsname)
{
    if (!pmi_global_ctx || !pmi_global_ctx->initialized)
        return PMI_ERR_INIT;
    if (!kvsname)
        return PMI_ERR_INVALID_ARG;
    return PMI_SUCCESS; // no-op in this implementation
}

int PMI_Barrier (void)
{
    return pmi_simple_client_barrier (pmi_global_ctx);
}

int PMI_Publish_name (const char *service_name, const char *port)
{
    return PMI_FAIL;
}

int PMI_Unpublish_name (const char *service_name)
{
    return PMI_FAIL;
}

int PMI_Lookup_name (const char *service_name, char *port)
{
    return PMI_FAIL;
}

int PMI_Spawn_multiple(int count,
                       const char **cmds,
                       const char **argvs[],
                       const int *maxprocs,
                       const int *info_keyval_sizesp,
                       const PMI_keyval_t **info_keyval_vectors,
                       int preput_keyval_size,
                       const PMI_keyval_t *preput_keyval_vector,
                       int *errors)
{
    return PMI_FAIL;
}

/* Old API funcs - signatures needed for ABI compliance.
 */

int PMI_Get_clique_ranks (int *ranks, int length)
{
    return pmi_simple_client_get_clique_ranks (pmi_global_ctx, ranks, length);
}

int PMI_Get_clique_size (int *size)
{
    return pmi_simple_client_get_clique_size (pmi_global_ctx, size);
}

int PMI_Get_id_length_max (int *length)
{
    return PMI_KVS_Get_name_length_max (length);
}

int PMI_Get_id (char *kvsname, int length)
{
    return PMI_KVS_Get_my_name (kvsname, length);
}

int PMI_Get_kvs_domain_id (char *kvsname, int length)
{
    return PMI_KVS_Get_my_name (kvsname, length);
}

int PMI_KVS_Create (char *kvsname, int length)
{
    return PMI_FAIL;
}

int PMI_KVS_Destroy (const char *kvsname)
{
    return PMI_FAIL;
}

int PMI_KVS_Iter_first (const char *kvsname,
                        char *key,
                        int key_len,
                        char *val,
                        int val_len)
{
    return PMI_FAIL;
}

int PMI_KVS_Iter_next (const char *kvsname,
                       char *key,
                       int key_len,
                       char *val,
                       int val_len)
{
    return PMI_FAIL;
}

int PMI_Parse_option (int num_args,
                      char **args,
                      int *num_parsed,
                      PMI_keyval_t **keyvalp,
                      int *size)
{
    return PMI_FAIL;
}

int PMI_Args_to_keyval (int *argcp,
                        char *((*argvp)[]),
                        PMI_keyval_t **keyvalp,
                        int *size)
{
    return PMI_FAIL;
}

int PMI_Free_keyvals (PMI_keyval_t *keyvalp, int size)
{
    return PMI_FAIL;
}

int PMI_Get_options (char *str, int *length)
{
    return PMI_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
