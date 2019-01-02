/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include "wrap.h"
#include "single.h"
#include "clique.h"

struct pmi_context {
    void *impl;
    struct pmi_operations *ops;
    int debug;
    int rank; /* for debug */
};

static struct pmi_context ctx = { .rank = -1 };

#define DPRINTF(fmt,...) do { \
    if (ctx.debug) fprintf (stderr, fmt, ##__VA_ARGS__); \
} while (0)

#define DRETURN(rc) do { \
    DPRINTF ("%d: %s rc=%d %s\n", ctx.rank, __FUNCTION__, (rc), \
            rc == PMI_SUCCESS ? "" : pmi_strerror (rc)); \
    return (rc); \
} while (0);


int PMI_Init (int *spawned)
{
    int result = PMI_FAIL;
    const char *library;
    const char *debug;

    if (ctx.impl != NULL)
        goto done;

    if ((debug = getenv ("FLUX_PMI_DEBUG")))
        ctx.debug = strtol (debug, NULL, 0);
    else
        ctx.debug = 0;

    /* If PMI_FD is set, the simple protocol service is offered.
     * Use it directly.
     */
    if (getenv ("PMI_FD")) {
        DPRINTF ("%s: PMI_FD is set, selecting simple_client\n", __FUNCTION__);
        if (!(ctx.impl = pmi_simple_client_create (&ctx.ops)))
            goto done;
    }
    /* If PMI_LIBRARY is set, we are directed to open a specific library.
     */
    else if ((library = getenv ("PMI_LIBRARY"))) {
        DPRINTF ("%s: PMI_LIBRARY is set, use %s\n", __FUNCTION__, library);
        if (!(ctx.impl = pmi_wrap_create (library, &ctx.ops, false)))
            goto done;
    }

    /* No obvious directives.
     * Try to dlopen another PMI library, e.g. SLURM's.
     * If that fails, fall through to singleton.
     */
    else if (!getenv ("FLUX_PMI_SINGLETON")
                    && (ctx.impl = pmi_wrap_create (NULL, &ctx.ops, false))) {
    }
    /* Singleton.
     */
    else {
        DPRINTF ("%s: library search failed, use singleton\n", __FUNCTION__);
        if (!(ctx.impl = pmi_single_create (&ctx.ops)))
            goto done;
    }

    /* call PMI_Init method */
    result = ctx.ops->init (ctx.impl, spawned);
    if (result != PMI_SUCCESS) {
        ctx.ops->destroy (ctx.impl);
        ctx.impl = NULL;
        goto done;
    }
done:
    /* Cache the rank for logging.
     */
    if (ctx.debug && result == PMI_SUCCESS) {
        int debug_save = ctx.debug;
        ctx.debug = 0;
        (void)PMI_Get_rank (&ctx.rank);
        ctx.debug = debug_save;
    }
    DRETURN (result);
}

int PMI_Initialized (int *initialized)
{
    int result;
    if (ctx.impl && ctx.ops->initialized)
        result = ctx.ops->initialized (ctx.impl, initialized);
    else {
        *initialized = 0;
        result = PMI_SUCCESS;
    }
    DRETURN (result);
    return result;
}

int PMI_Finalize (void)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->finalize)
        result = ctx.ops->finalize (ctx.impl);
    if (ctx.impl && ctx.ops->destroy)
        ctx.ops->destroy (ctx.impl);
    ctx.impl = NULL;

    DRETURN (result);
}

int PMI_Abort (int exit_code, const char error_msg[])
{
    int result = PMI_ERR_INIT;
    DPRINTF ("%d: %s\n", ctx.rank, __FUNCTION__);
    if (ctx.impl && ctx.ops->abort)
        result = ctx.ops->abort (ctx.impl, exit_code, error_msg);
    /* unlikely to return */
    DRETURN (result);
}

int PMI_Get_size (int *size)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->get_size)
        result = ctx.ops->get_size (ctx.impl, size);
    DRETURN (result);
}

int PMI_Get_rank (int *rank)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->get_rank)
        result = ctx.ops->get_rank (ctx.impl, rank);
    if (result == PMI_SUCCESS)
        ctx.rank = *rank;
    DRETURN (result);
}

int PMI_Get_universe_size (int *size)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->get_universe_size)
        result = ctx.ops->get_universe_size (ctx.impl, size);
    DRETURN (result);
}

int PMI_Get_appnum (int *appnum)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->get_appnum)
        result = ctx.ops->get_appnum (ctx.impl, appnum);
    DRETURN (result);
}

int PMI_KVS_Get_my_name (char kvsname[], int length)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_get_my_name)
        result = ctx.ops->kvs_get_my_name (ctx.impl, kvsname, length);
    if (result == PMI_SUCCESS) {
        DPRINTF ("%d: %s (\"%s\") rc=%d\n", ctx.rank, __FUNCTION__,
                 kvsname, result);
    } else {
        DPRINTF ("%d: %s rc=%d %s\n", ctx.rank, __FUNCTION__,
                result, pmi_strerror (result));
    }
    return result;
}

int PMI_KVS_Get_name_length_max (int *length)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_get_name_length_max)
        result = ctx.ops->kvs_get_name_length_max (ctx.impl, length);
    DRETURN (result);
}

int PMI_KVS_Get_key_length_max (int *length)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_get_key_length_max)
        result = ctx.ops->kvs_get_key_length_max (ctx.impl, length);
    DRETURN (result);
}

int PMI_KVS_Get_value_length_max (int *length)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_get_value_length_max)
        result = ctx.ops->kvs_get_value_length_max (ctx.impl, length);
    DRETURN (result);
}

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[])
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_put)
        result = ctx.ops->kvs_put (ctx.impl, kvsname, key, value);
    DPRINTF ("%d: PMI_KVS_Put (\"%s\", \"%s\", \"%s\") rc=%d %s\n",
             ctx.rank, kvsname, key, value, result,
             result == PMI_SUCCESS ? "" : pmi_strerror (result));
    return result;
}

int PMI_KVS_Get (const char kvsname[], const char key[],
                 char value[], int length)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_get)
        result = ctx.ops->kvs_get (ctx.impl, kvsname, key, value, length);
    if (result == PMI_SUCCESS) {
        DPRINTF ("%d: PMI_KVS_Get (\"%s\", \"%s\", \"%s\") rc=%d\n",
                 ctx.rank, kvsname, key, value, result);
    } else {
        DPRINTF ("%d: PMI_KVS_Get (\"%s\", \"%s\") rc=%d %s\n",
                 ctx.rank, kvsname, key, result, pmi_strerror (result));
    }
    return result;
}

int PMI_KVS_Commit (const char kvsname[])
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->kvs_commit)
        result = ctx.ops->kvs_commit (ctx.impl, kvsname);
    DRETURN (result);
}

int PMI_Barrier (void)
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->barrier)
        result = ctx.ops->barrier (ctx.impl);
    DRETURN (result);
}

int PMI_Publish_name (const char service_name[], const char port[])
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->publish_name)
        result  = ctx.ops->publish_name (ctx.impl, service_name, port);
    DRETURN (result);
}

int PMI_Unpublish_name (const char service_name[])
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->unpublish_name)
        result  = ctx.ops->unpublish_name (ctx.impl, service_name);
    DRETURN (result);
}

int PMI_Lookup_name (const char service_name[], char port[])
{
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->lookup_name)
        result  = ctx.ops->lookup_name (ctx.impl, service_name, port);
    DRETURN (result);
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
    int result = PMI_ERR_INIT;
    if (ctx.impl && ctx.ops->spawn_multiple)
        result = ctx.ops->spawn_multiple (ctx.impl, count, cmds, argvs,
                                          maxprocs, info_keyval_sizesp,
                                          info_keyval_vectors,
                                          preput_keyval_size,
                                          preput_keyval_vector, errors);
    DRETURN (result);
}

/* Old API funcs - signatures needed for ABI compliance.
 */

int PMI_Get_clique_ranks (int ranks[], int length)
{
    int result = PMI_FAIL;
    if (ctx.impl && ctx.ops->get_clique_ranks)
        result = ctx.ops->get_clique_ranks (ctx.impl, ranks, length);
    if (result == PMI_FAIL)
        result = pmi_process_mapping_get_clique_ranks (ranks, length);
    DRETURN (result);
}

int PMI_Get_clique_size (int *size)
{
    int result = PMI_FAIL;
    if (ctx.impl && ctx.ops->get_clique_size)
        result = ctx.ops->get_clique_size (ctx.impl, size);
    if (result == PMI_FAIL)
        result = pmi_process_mapping_get_clique_size (size);
    DRETURN (result);
}

int PMI_Get_id_length_max (int *length)
{
    int result = PMI_KVS_Get_name_length_max (length);
    DRETURN (result);
}

int PMI_Get_id (char kvsname[], int length)
{
    int result = PMI_KVS_Get_my_name (kvsname, length);
    DRETURN (result);
}

int PMI_Get_kvs_domain_id (char kvsname[], int length)
{
    int result = PMI_KVS_Get_my_name (kvsname, length);
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
