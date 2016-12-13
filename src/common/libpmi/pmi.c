/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

typedef enum {
    IMPL_UNKNOWN,
    IMPL_SIMPLE,
    IMPL_WRAP,
    IMPL_SINGLETON,
} implementation_t;

struct pmi_context {
    implementation_t type;
    union {
        struct pmi_wrap *wrap;
        struct pmi_simple_client *cli;
        struct pmi_single *single;
    };
    int debug;
    int rank; /* for debug */
};

static struct pmi_context ctx = { .type = IMPL_UNKNOWN, .rank = -1 };

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

    if (ctx.type != IMPL_UNKNOWN)
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
        if (!(ctx.cli = pmi_simple_client_create ()))
            goto done;
        result = pmi_simple_client_init (ctx.cli, spawned);
        if (result != PMI_SUCCESS) {
            pmi_simple_client_destroy (ctx.cli);
            ctx.cli = NULL;
            goto done;
        }
        ctx.type = IMPL_SIMPLE;
        goto done;
    }
    /* If PMIX_SERVER_URI is set, this indicates PMIx service is offered.
     * Use it via the PMI v1 API provided in libpmix.so.
     */
    if (getenv ("PMIX_SERVER_URI")) {
        DPRINTF ("%s: PMIX_SERVER_URI is set, use libpmix.so\n", __FUNCTION__);
        if (!(ctx.wrap = pmi_wrap_create ("libpmix.so")))
            goto done;
        result = pmi_wrap_init (ctx.wrap, spawned);
        if (result != PMI_SUCCESS) {
            pmi_wrap_destroy (ctx.wrap);
            ctx.wrap = NULL;
            goto done;
        }
        ctx.type = IMPL_WRAP;
        goto done;
    }
    /* If PMI_LIBRARY is set, we are directed to open a specific library.
     */
    if ((library = getenv ("PMI_LIBRARY"))) {
        DPRINTF ("%s: PMI_LIBRARY is set, use %s\n", __FUNCTION__, library);
        if (!(ctx.wrap = pmi_wrap_create (library)))
            goto done;
        result = pmi_wrap_init (ctx.wrap, spawned);
        if (result != PMI_SUCCESS) {
            pmi_wrap_destroy (ctx.wrap);
            ctx.wrap = NULL;
            goto done;
        }
        ctx.type = IMPL_WRAP;
        goto done;
    }

    /* No obvious directives.
     * Try to dlopen another PMI library, e.g. SLURM's.
     * If that fails, fall through to singleton.
     */
    if ((ctx.wrap = pmi_wrap_create (NULL))) {
        result = pmi_wrap_init (ctx.wrap, spawned);
        if (result != PMI_SUCCESS) {
            pmi_wrap_destroy (ctx.wrap);
            ctx.wrap = NULL;
        } else {
            ctx.type = IMPL_WRAP;
            goto done;
        }
    }
    /* Singleton.
     */
    if ((ctx.single = pmi_single_create ())) {
        DPRINTF ("%s: library search failed, use singleton\n", __FUNCTION__);
        result = pmi_single_init (ctx.single, spawned);
        if (result != PMI_SUCCESS) {
            pmi_single_destroy (ctx.single);
            ctx.single = NULL;
            goto done;
        }
        ctx.type = IMPL_SINGLETON;
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
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_initialized (ctx.single, initialized);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_initialized (ctx.wrap, initialized);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_initialized (ctx.cli, initialized);
            break;
        default:
            *initialized = 0;
            result = PMI_SUCCESS;
            break;
    }
    DRETURN (result);
    return result;
}

int PMI_Finalize (void)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_finalize (ctx.single);
            pmi_single_destroy (ctx.single);
            ctx.single = NULL;
            break;
        case IMPL_WRAP:
            result = pmi_wrap_finalize (ctx.wrap);
            pmi_wrap_destroy (ctx.wrap);
            ctx.wrap = NULL;
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_finalize (ctx.cli);
            pmi_simple_client_destroy (ctx.cli);
            ctx.cli = NULL;
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    ctx.type = IMPL_UNKNOWN;
    DRETURN (result);
}

int PMI_Abort (int exit_code, const char error_msg[])
{
    DPRINTF ("%d: %s\n", ctx.rank, __FUNCTION__);
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_abort (ctx.single, exit_code, error_msg);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_abort (ctx.wrap, exit_code, error_msg);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_abort (ctx.cli, exit_code, error_msg);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    /* unlikely to return */
    DRETURN (result);
}

int PMI_Get_size (int *size)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_get_size (ctx.single, size);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_get_size (ctx.wrap, size);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_get_size (ctx.cli, size);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Get_rank (int *rank)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_get_rank (ctx.single, rank);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_get_rank (ctx.wrap, rank);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_get_rank (ctx.cli, rank);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    if (result == PMI_SUCCESS)
        ctx.rank = *rank;
    DRETURN (result);
}

int PMI_Get_universe_size (int *size)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_get_universe_size (ctx.single, size);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_get_universe_size (ctx.wrap, size);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_get_universe_size (ctx.cli, size);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Get_appnum (int *appnum)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_get_appnum (ctx.single, appnum);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_get_appnum (ctx.wrap, appnum);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_get_appnum (ctx.cli, appnum);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_KVS_Get_my_name (char kvsname[], int length)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_get_my_name (ctx.single, kvsname, length);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_get_my_name (ctx.wrap, kvsname, length);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_get_my_name (ctx.cli, kvsname,
                                                        length);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
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
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_get_name_length_max (ctx.single, length);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_get_name_length_max (ctx.wrap, length);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_get_name_length_max (ctx.cli,
                                                                length);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_KVS_Get_key_length_max (int *length)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_get_key_length_max (ctx.single, length);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_get_key_length_max (ctx.wrap, length);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_get_key_length_max (ctx.cli, length);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_KVS_Get_value_length_max (int *length)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_get_value_length_max (ctx.single, length);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_get_value_length_max (ctx.wrap, length);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_get_value_length_max (ctx.cli,
                                                                 length);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[])
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_put (ctx.single, kvsname, key, value);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_put (ctx.wrap, kvsname, key, value);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_put (ctx.cli, kvsname, key, value);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DPRINTF ("%d: PMI_KVS_Put (\"%s\", \"%s\", \"%s\") rc=%d %s\n",
             ctx.rank, kvsname, key, value, result,
             result == PMI_SUCCESS ? "" : pmi_strerror (result));
    return result;
}

int PMI_KVS_Get (const char kvsname[], const char key[],
                          char value[], int length)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_get (ctx.single, kvsname,
                                         key, value, length);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_get (ctx.wrap, kvsname, key, value, length);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_get (ctx.cli, kvsname,
                                                key, value, length);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
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
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_kvs_commit (ctx.single, kvsname);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_kvs_commit (ctx.wrap, kvsname);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_kvs_commit (ctx.cli, kvsname);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Barrier (void)
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_barrier (ctx.single);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_barrier (ctx.wrap);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_barrier (ctx.cli);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Publish_name (const char service_name[], const char port[])
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result  = pmi_single_publish_name (ctx.single, service_name, port);
            break;
        case IMPL_WRAP:
            result  = pmi_wrap_publish_name (ctx.wrap, service_name, port);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_publish_name (ctx.cli,
                                                     service_name, port);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Unpublish_name (const char service_name[])
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result  = pmi_single_unpublish_name (ctx.single, service_name);
            break;
        case IMPL_WRAP:
            result  = pmi_wrap_unpublish_name (ctx.wrap, service_name);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_unpublish_name (ctx.cli, service_name);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

int PMI_Lookup_name (const char service_name[], char port[])
{
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result  = pmi_single_lookup_name (ctx.single, service_name, port);
            break;
        case IMPL_WRAP:
            result  = pmi_wrap_lookup_name (ctx.wrap, service_name, port);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_lookup_name (ctx.cli,
                                                    service_name, port);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
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
    int result;
    switch (ctx.type) {
        case IMPL_SINGLETON:
            result = pmi_single_spawn_multiple (ctx.single, count, cmds, argvs,
                                                maxprocs, info_keyval_sizesp,
                                                info_keyval_vectors,
                                                preput_keyval_size,
                                                preput_keyval_vector, errors);
            break;
        case IMPL_WRAP:
            result = pmi_wrap_spawn_multiple (ctx.wrap, count, cmds, argvs,
                                              maxprocs, info_keyval_sizesp,
                                              info_keyval_vectors,
                                              preput_keyval_size,
                                              preput_keyval_vector, errors);
            break;
        case IMPL_SIMPLE:
            result = pmi_simple_client_spawn_multiple (ctx.cli, count, cmds,
                                                       argvs, maxprocs,
                                                       info_keyval_sizesp,
                                                       info_keyval_vectors,
                                                       preput_keyval_size,
                                                       preput_keyval_vector,
                                                       errors);
            break;
        default:
            result = PMI_ERR_INIT;
            break;
    }
    DRETURN (result);
}

/* Old API funcs - signatures needed for ABI compliance.
 */

int PMI_Get_clique_ranks (int ranks[], int length)
{
    int result;
    switch (ctx.type) {
        case IMPL_WRAP:
            result = pmi_wrap_get_clique_ranks (ctx.wrap, ranks, length);
            break;
        default:
            result = PMI_FAIL;
            break;
    }
    if (result == PMI_FAIL)
        result = pmi_process_mapping_get_clique_ranks (ranks, length);
    DRETURN (result);
}

int PMI_Get_clique_size (int *size)
{
    int result;
    switch (ctx.type) {
        case IMPL_WRAP:
            result = pmi_wrap_get_clique_size (ctx.wrap, size);
            break;
        default:
            result = PMI_FAIL;
            break;
    }
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
