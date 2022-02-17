/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pmi2.c - canonical PMI-2 API/ABI for libpmi2.so.0.0.0
 *
 * This is pretty much the minimum needed to bootstrap MPICH and
 * derivatives under Flux, when they are configured with --with-pm=slurm
 * --with-pmi=pmi2.  This configuration forces a dlopen of libpmi2.so.0.0.0,
 * so use LD_LIBRARY_PATH to make it find ours before Slurm's.
 *
 * Caveats:
 * - Only the API functions and attrs needed for bootstrap are implemented.
 * - This is based on pmi_simple_client, which only supports the v1 wire proto.
 * - Although the pmi_simple_client calls return PMI-1 error codes, PMI-2's
 *   error codes are numerically identical so we don't bother converting.
 * - The kvsname is cached in the pmi_simple_client aux cache on first use.
 *   It is needed internally by PMI2_KVS_Put(), PMI2_Info_GetJobAttr(),
 *   and caching it avoids the RTT of fetching it on each use.
 * - This implementation is not thread safe.  Locks should be added so that
 *   the global context can be shared across threads as is seemingly required
 *   by the PMI-2 design documents (see references below).
 * - Even if locks are added, pmi_simple_client does not support the v1 wire
 *   proto split operations, so multiple PMI_KVS_Get() requests could not be
 *   outstanding in multiple threads.
 * - Is providing the 'PMI_process_mapping' attribute sufficient "clique"
 *   support to allow MPI to use shmem to communicate on co-located ranks?
 *
 * See also:
 * - https://wiki.mpich.org/mpich/index.php/PMI_v2_API
 * - https://wiki.mpich.org/mpich/index.php/PMI_v2_Wire_Protocol
 * - https://wiki.mpich.org/mpich/index.php/PMI_v2_Design_Thoughts
 *   https://www.mcs.anl.gov/papers/P1760.pdf
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>

#include "pmi.h"
#include "pmi2.h"
#include "pmi_strerror.h"
#include "simple_client.h"

static struct pmi_simple_client *pmi_global_ctx;

int PMI2_Init (int *spawned, int *size, int *rank, int *appnum)
{
    int result = PMI2_FAIL;
    struct pmi_simple_client *ctx;

    if (pmi_global_ctx)
        return PMI2_ERR_INIT;

    ctx = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                       getenv ("PMI_RANK"),
                                       getenv ("PMI_SIZE"),
                                       getenv ("PMI_SPAWNED"));
    if (!ctx) {
        if (errno == ENOMEM)
            return PMI2_ERR_NOMEM;
        return PMI2_FAIL;
    }

    result = pmi_simple_client_init (ctx);
    if (result != PMI2_SUCCESS) {
        pmi_simple_client_destroy (ctx);
        return result;
    }
    if (appnum) {
        result = pmi_simple_client_get_appnum (ctx, appnum);
        if (result != PMI2_SUCCESS) {
            pmi_simple_client_destroy (ctx);
            return result;
        }
    }
    if (spawned)
        *spawned = ctx->spawned;
    if (size)
        *size = ctx->size;
    if (rank)
        *rank = ctx->rank;

    pmi_global_ctx = ctx;
    return PMI2_SUCCESS;
}

int PMI2_Finalize (void)
{
    int result;

    result = pmi_simple_client_finalize (pmi_global_ctx);
    pmi_simple_client_destroy (pmi_global_ctx);

    pmi_global_ctx = NULL;
    return result;
}

int PMI2_Initialized (void)
{
    if (pmi_global_ctx && pmi_global_ctx->initialized)
        return 1;
    return 0;
}

int PMI2_Abort (int flag, const char msg[])
{
    /* pmi_simple_client_abort() only returns on error, in which case
     * we fall back to printing the msg on stderr and call exit().
     * (return code not checked because we don't do anything with it)
     */
    (void) pmi_simple_client_abort (pmi_global_ctx, 1, msg);
    fprintf (stderr, "PMI2_Abort: (%d) %s\n",
             pmi_global_ctx ? pmi_global_ctx->rank : -1,
             msg ? msg : "NULL");
    exit (1);
    /*NOTREACHED*/
    return PMI_SUCCESS;
}

int PMI2_Job_Spawn (int count, const char * cmds[],
                    int argcs[], const char ** argvs[],
                    const int maxprocs[],
                    const int info_keyval_sizes[],
                    const struct MPID_Info *info_keyval_vectors[],
                    int preput_keyval_size,
                    const struct MPID_Info *preput_keyval_vector[],
                    char jobId[], int jobIdSize,
                    int errors[])
{
    return PMI_FAIL;
}

/* Look up kvsname on first request, then cache for subsequent requests.
 */
static int get_cached_kvsname (struct pmi_simple_client *pmi, const char **name)
{
    const char *auxkey = "flux::kvsname";
    char *kvsname;
    int result;

    if (!pmi)
        return PMI2_ERR_INIT;
    if ((kvsname = pmi_simple_client_aux_get (pmi, auxkey))) {
        *name = kvsname;
        return PMI2_SUCCESS;
    }
    if (!(kvsname = calloc (1, pmi->kvsname_max)))
        return PMI2_ERR_NOMEM;
    result = pmi_simple_client_kvs_get_my_name (pmi,
                                                kvsname,
                                                pmi->kvsname_max);
    if (result != PMI2_SUCCESS) {
        free (kvsname);
        return result;
    }
    if (pmi_simple_client_aux_set (pmi, auxkey, kvsname, free) < 0) {
        free (kvsname);
        return PMI2_FAIL;
    }
    *name = kvsname;
    return PMI2_SUCCESS;
}

/* MPICH: treats PMI2_Job_GetId() equivalent to PMI_KVS_Get_my_name().
 */
int PMI2_Job_GetId (char jobid[], int jobid_size)
{
    const char *kvsname;
    int result;

    result = get_cached_kvsname (pmi_global_ctx, &kvsname);
    if (result != PMI2_SUCCESS)
        return result;
    if (!jobid || jobid_size < (int)strlen (kvsname) + 1)
        return PMI2_ERR_INVALID_ARGS;
    strcpy (jobid, kvsname);
    return PMI2_SUCCESS;
}

int PMI2_Job_GetRank (int *rank)
{
    return PMI2_FAIL;
}

int PMI2_Job_Connect (const char jobid[], PMI2_Connect_comm_t *conn)
{
    return PMI2_FAIL;
}

int PMI2_Job_Disconnect (const char jobid[])
{
    return PMI2_FAIL;
}

int PMI2_KVS_Put (const char key[], const char value[])
{
    const char *kvsname;
    int result;

    result = get_cached_kvsname (pmi_global_ctx, &kvsname);
    if (result != PMI2_SUCCESS)
        return result;

    return pmi_simple_client_kvs_put (pmi_global_ctx, kvsname, key, value);
}

/* MPICH: treats jobid equivalent to kvsname.
 * We ignore src_pmi_id.
 */
int PMI2_KVS_Get (const char *jobid,
                  int src_pmi_id,
                  const char key[],
                  char value [],
                  int maxvalue,
                  int *vallen)
{
    int result;

    if (!jobid) {
        result = get_cached_kvsname (pmi_global_ctx, &jobid);
        if (result != PMI2_SUCCESS)
            return result;
    }
    result = pmi_simple_client_kvs_get (pmi_global_ctx,
                                        jobid,
                                        key,
                                        value,
                                        maxvalue);
    if (vallen)
        *vallen = (result == PMI2_SUCCESS) ? strlen (value) : 0;
    return result;
}

int PMI2_KVS_Fence (void)
{
    return pmi_simple_client_barrier (pmi_global_ctx);
}

int PMI2_Info_GetSize (int *size)
{
    return PMI2_FAIL;
}

/* Cray MPI: look up a node-scope key stored with PMI2_Info_PutNodeAttr().
 * If waitfor is nonzero, try once per second until the key is available.
 */
int PMI2_Info_GetNodeAttr (const char name[],
                           char value[], int valuelen, int *found, int waitfor)
{
    const char *kvsname;
    int result;
    int tries = 0;
    char local_name[PMI2_MAX_KEYLEN + 8];

    if (!name || !value)
        return PMI2_ERR_INVALID_ARG;
    result = get_cached_kvsname (pmi_global_ctx, &kvsname);
    if (result != PMI2_SUCCESS)
        return result;
    if (snprintf (local_name,
                  sizeof (local_name),
                  "local::%s", name) >= sizeof (local_name))
        return PMI2_ERR_INVALID_KEY_LENGTH;
    do {
        if (tries++ > 0)
            sleep (1);
        result = pmi_simple_client_kvs_get (pmi_global_ctx,
                                            kvsname,
                                            local_name,
                                            value,
                                            valuelen);
        if (result != PMI2_ERR_INVALID_KEY && result != PMI2_SUCCESS)
            return result;
        tries++;
    } while (result == PMI2_ERR_INVALID_KEY && waitfor != 0);
    if (found) {
        *found = (result == PMI2_SUCCESS) ? 1 : 0;
        return PMI2_SUCCESS;
    }
    return result;
}

int PMI2_Info_GetNodeAttrIntArray (const char name[], int array[],
                                   int arraylen, int *outlen, int *found)
{
    return PMI2_FAIL;
}

/* Cray MPI: prefix node local keys with local:: to tell the flux PMI plugin
 * not to exchange them.  They are immediately available for kvs_get by procs
 * on the same shell.
 */
int PMI2_Info_PutNodeAttr (const char name[], const char value[])
{
    const char *kvsname;
    int result;
    char local_name[PMI2_MAX_KEYLEN + 8];

    if (!name || !value)
        return PMI2_ERR_INVALID_ARG;
    result = get_cached_kvsname (pmi_global_ctx, &kvsname);
    if (result != PMI2_SUCCESS)
        return result;

    if (snprintf (local_name,
                  sizeof (local_name),
                  "local::%s", name) >= sizeof (local_name))
        return PMI2_ERR_INVALID_KEY_LENGTH;

    return pmi_simple_client_kvs_put (pmi_global_ctx,
                                      kvsname,
                                      local_name,
                                      value);
}

/* MPICH: only fetches PMI_process_mapping and universeSize
 * with PMI2_Info_GetJobAttr().
 */
int PMI2_Info_GetJobAttr (const char name[],
                          char value[], int valuelen, int *found)
{
    int result;

    if (!name || !value) {
        result = PMI2_ERR_INVALID_ARG;
        goto error;
    }
    if (!strcmp (name, "PMI_process_mapping")) {
        const char *kvsname;

        result = get_cached_kvsname (pmi_global_ctx, &kvsname);
        if (result != PMI2_SUCCESS)
            goto error;
        result = pmi_simple_client_kvs_get (pmi_global_ctx,
                                            kvsname,
                                            name,
                                            value,
                                            valuelen);
        if (result != PMI2_SUCCESS)
            goto error;
    }
    else if (!strcmp (name, "universeSize")) {
        int universe_size;

        result = pmi_simple_client_get_universe_size (pmi_global_ctx,
                                                      &universe_size);
        if (result != PMI2_SUCCESS)
            goto error;
        if (snprintf (value,
                      valuelen,
                      "%d",
                      universe_size) >= valuelen) {
            result = PMI2_ERR_INVALID_VAL_LENGTH;
            goto error;
        }
    }
    else {
        result = PMI2_ERR_INVALID_KEY;
        goto error;
    }
    if (found)
        *found = 1;
    return PMI2_SUCCESS;
error:
    if (found)
        *found = 0;
    return result;
}

int PMI2_Info_GetJobAttrIntArray (const char name[], int array[],
                                  int arraylen, int *outlen, int *found)
{
    return PMI2_FAIL;
}

int PMI2_Nameserv_publish (const char service_name[],
                           const struct MPID_Info *info_ptr, const char port[])
{
    return PMI2_FAIL;
}

int PMI2_Nameserv_lookup (const char service_name[],
                          const struct MPID_Info *info_ptr,
                          char port[], int portLen)
{
    return PMI2_FAIL;
}

int PMI2_Nameserv_unpublish (const char service_name[],
                             const struct MPID_Info *info_ptr)
{
    return PMI2_FAIL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
