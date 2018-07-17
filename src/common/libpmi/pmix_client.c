/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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

/* pmix_client.c - bootstrap with PMIx */

/* The main purpose of this code is to allow Flux to be launched by LSF
 * on IBM spectrum_mpi system that provides libpmix.so but (currently) doesn't
 * distribute its PMI-1 compatability library.
 *
 * As such, this code borrows from pmix/src/client/pmi1.c, which was
 * licensed under a 3-clause BSD license and:
 *
 *   Copyright (c) 2014-2017 Intel, Inc. All rights reserved.
 *   Copyright (c) 2014      Research Organization for Information Science
 *                           and Technology (RIST). All rights reserved.
 *   Copyright (c) 2016      Mellanox Technologies, Inc.
 *                           All rights reserved.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <pmix.h>

#include "pmi.h"

#include "pmix_client.h"

#define KVS_VAL_MAX 4096

static pmix_status_t convert_int (int *value, pmix_value_t *kv);
static int convert_err (pmix_status_t rc);

struct pmix_client {
    pmix_proc_t myproc;
    int init;
};

static int pmix_client_init (void *impl, int *spawned)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_proc_t proc;
    pmix_info_t info[1];
    bool val_optional = 1;

    if ((rc = PMIx_Init (&pmi->myproc, NULL, 0)) != PMIX_SUCCESS)
        return PMI_ERR_INIT;

    /* getting internal key requires special rank value */
    proc = pmi->myproc;
    proc.rank = PMIX_RANK_UNDEF;

    /* set controlling parameters
     * PMIX_OPTIONAL - expect that these keys should be available on startup
     */
    PMIX_INFO_CONSTRUCT (&info[0]);
    PMIX_INFO_LOAD (&info[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);

    if (spawned) {
        /* get the spawned flag */
        if (PMIx_Get (&proc, PMIX_SPAWNED, info, 1, &val) == PMIX_SUCCESS) {
            rc = convert_int (spawned, val);
            PMIX_VALUE_RELEASE(val);
            if (rc != PMIX_SUCCESS)
                goto error;
        }
        /* if not found, default to not spawned */
        else {
            *spawned = 0;
        }
    }
    pmi->init = 1;
    rc = PMIX_SUCCESS;
error:
    PMIX_INFO_DESTRUCT (&info[0]);
    return convert_err (rc);
}

static int pmix_client_initialized (void *impl, int *initialized)
{
    if (!initialized)
        return PMI_ERR_INVALID_ARG;
    *initialized = PMIx_Initialized();
    return PMI_SUCCESS;
}

static int pmix_client_finalize (void *impl)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;

    if (!pmi->init)
        return PMI_FAIL;
    pmi->init = 0;
    rc = PMIx_Finalize (NULL, 0);
    return convert_err (rc);
}

static int pmix_client_get_size (void *impl, int *size)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_info_t info[1];
    bool  val_optional = 1;
    pmix_proc_t proc;

    if (!pmi->init)
        return PMI_FAIL;

    /* set controlling parameters
     * PMIX_OPTIONAL - expect that these keys should be available on startup
     */
    PMIX_INFO_CONSTRUCT (&info[0]);
    PMIX_INFO_LOAD (&info[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);

    proc = pmi->myproc;
    proc.rank = PMIX_RANK_WILDCARD;
    rc = PMIx_Get (&proc, PMIX_JOB_SIZE, info, 1, &val);
    if (rc == PMIX_SUCCESS) {
        rc = convert_int (size, val);
        PMIX_VALUE_RELEASE (val);
    }
    PMIX_INFO_DESTRUCT (&info[0]);
    return convert_err (rc);
}

static int pmix_client_get_rank (void *impl, int *rank)
{
    struct pmix_client *pmi = impl;

    if (!pmi->init)
        return PMI_FAIL;
    if (!rank)
        return PMI_ERR_INVALID_ARG;
    *rank = pmi->myproc.rank;
    return PMI_SUCCESS;
}

static int pmix_client_get_appnum (void *impl, int *appnum)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_info_t info[1];
    bool  val_optional = 1;
    pmix_proc_t proc;

    if (!pmi->init)
        return PMI_FAIL;
    if (!appnum)
        return PMI_ERR_INVALID_ARG;
    proc = pmi->myproc;
    proc.rank = PMIX_RANK_WILDCARD;
    /* set controlling parameters
     * PMIX_OPTIONAL - expect that these keys should be available on startup
     */
    PMIX_INFO_CONSTRUCT (&info[0]);
    PMIX_INFO_LOAD (&info[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);
    rc = PMIx_Get (&proc, PMIX_APPNUM, info, 1, &val);
    if (rc == PMIX_SUCCESS) {
        rc = convert_int (appnum, val);
        PMIX_VALUE_RELEASE (val);
    }
    /* this is optional value, set to 0 */
    else if (rc == PMIX_ERR_NOT_FOUND) {
        *appnum = 0;
        rc = PMIX_SUCCESS;
    }
    PMIX_INFO_DESTRUCT (&info[0]);
    return convert_err (rc);
}

static int pmix_client_get_universe_size (void *impl, int *universe_size)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_info_t info[1];
    bool  val_optional = 1;
    pmix_proc_t proc;

    if (!pmi->init)
        return PMI_FAIL;
    if (!universe_size)
        return PMI_ERR_INVALID_ARG;
    proc = pmi->myproc;
    proc.rank = PMIX_RANK_WILDCARD;
    /* set controlling parameters
     * PMIX_OPTIONAL - expect that these keys should be available on startup
     */
    PMIX_INFO_CONSTRUCT (&info[0]);
    PMIX_INFO_LOAD (&info[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);

    rc = PMIx_Get (&proc, PMIX_UNIV_SIZE, info, 1, &val);
    if (rc == PMIX_SUCCESS) {
        rc = convert_int (universe_size, val);
        PMIX_VALUE_RELEASE (val);
    }

    PMIX_INFO_DESTRUCT (&info[0]);
    return convert_err (rc);
}

static int pmix_client_publish_name (void *impl, const char *service_name,
                                     const char *port)
{
    return PMI_FAIL;
}

static int pmix_client_unpublish_name (void *impl, const char *service_name)
{
    return PMI_FAIL;
}

static int pmix_client_lookup_name (void *impl, const char *service_name,
                                    char *port)
{
    return PMI_FAIL;
}

static int pmix_client_barrier (void *impl)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_info_t buf;
    int ninfo = 0;
    pmix_info_t *info = NULL;
    bool val = 1;

    if (!pmi->init)
        return PMI_FAIL;
    info = &buf;
    PMIX_INFO_CONSTRUCT (info);
    PMIX_INFO_LOAD (info, PMIX_COLLECT_DATA, &val, PMIX_BOOL);
    ninfo = 1;
    rc = PMIx_Fence (NULL, 0, info, ninfo);
    PMIX_INFO_DESTRUCT (info);
    return convert_err (rc);
}

static int pmix_client_abort (void *impl, int exit_code, const char *error_msg)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;

    if (!pmi->init)
        return PMI_FAIL;
    rc = PMIx_Abort (exit_code, error_msg, NULL, 0);
    return convert_err (rc);
}

static int pmix_client_kvs_get_my_name (void *impl, char *kvsname, int length)
{
    struct pmix_client *pmi = impl;

    if (!pmi->init)
        return PMI_FAIL;
    if (!kvsname)
        return PMI_ERR_INVALID_ARG;
    if (length < strlen (pmi->myproc.nspace) + 1)
        return PMI_ERR_INVALID_LENGTH;
    strcmp (kvsname, pmi->myproc.nspace);
    return PMI_SUCCESS;
}

static int pmix_client_kvs_get_name_length_max (void *impl, int *length)
{
    struct pmix_client *pmi = impl;

    if (!pmi->init)
        return PMI_FAIL;
    *length = PMIX_MAX_NSLEN;
    return PMI_SUCCESS;
}

static int pmix_client_kvs_get_key_length_max (void *impl, int *length)
{
    struct pmix_client *pmi = impl;

    if (!pmi->init)
        return PMI_FAIL;
    *length = PMIX_MAX_KEYLEN;
    return PMI_SUCCESS;
}

static int pmix_client_kvs_get_value_length_max (void *impl, int *length)
{
    struct pmix_client *pmi = impl;

    if (!pmi->init)
        return PMI_FAIL;
    *length = KVS_VAL_MAX;
    return PMI_SUCCESS;
}

static int pmix_client_kvs_put (void *impl, const char *kvsname,
                                const char *key, const char *value)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t val;

    if (!pmi->init)
        return PMI_FAIL;
    if (!kvsname || strlen (kvsname) > PMIX_MAX_NSLEN)
        return PMI_ERR_INVALID_LENGTH;
    if (!key || strlen (key) > PMIX_MAX_KEYLEN)
        return PMI_ERR_INVALID_KEY;
    if (!value || strlen (value) > KVS_VAL_MAX)
        return PMI_ERR_INVALID_VAL;
    val.type = PMIX_STRING;
    val.data.string = (char *)value;
    rc = PMIx_Put (PMIX_GLOBAL, key, &val);
    return convert_err (rc);
}

static int pmix_client_kvs_commit (void *impl, const char *kvsname)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;

    if (!pmi->init)
        return PMI_FAIL;
    if (!kvsname || strlen (kvsname) > PMIX_MAX_NSLEN)
        return PMI_ERR_INVALID_LENGTH;
    rc = PMIx_Commit ();
    return convert_err (rc);
}

static int pmix_client_kvs_get (void *impl, const char *kvsname,
                                const char *key, char *value, int len)
{
    struct pmix_client *pmi = impl;
    pmix_status_t rc;
    pmix_value_t *val;
    pmix_proc_t proc;

    if (!pmi->init)
        return PMI_FAIL;
    if (!kvsname || strlen (kvsname) > PMIX_MAX_NSLEN)
        return PMI_ERR_INVALID_LENGTH;
    if (!key || strlen (key) > PMIX_MAX_KEYLEN)
        return PMI_ERR_INVALID_KEY;
    if (!value)
        return PMI_ERR_INVALID_VAL;

    /* PMI-1 expects resource manager to set
     * process mapping in ANL notation. */
    if (!strcmp(key, "PMI_process_mapping")) {
        /* we are looking in the job-data. If there is nothing there
         * we don't want to look in rank's data, thus set rank to widcard */
        proc = pmi->myproc;
        proc.rank = PMIX_RANK_WILDCARD;
        if (PMIx_Get(&proc, PMIX_ANL_MAP, NULL, 0, &val) == PMIX_SUCCESS &&
                                            val && val->type == PMIX_STRING) {
            strncpy (value, val->data.string, len);
            PMIX_VALUE_FREE(val, 1);
            return PMI_SUCCESS;
        }
        else {
            return PMI_FAIL;
        }
    }

    /* retrieve the data from PMIx - since we don't have a rank,
     * we indicate that by passing the UNDEF value */
    (void)strncpy (proc.nspace, kvsname, PMIX_MAX_NSLEN);
    proc.rank = PMIX_RANK_UNDEF;

    rc = PMIx_Get (&proc, key, NULL, 0, &val);
    if (rc == PMIX_SUCCESS && val) {
        if (val->type != PMIX_STRING) {
            rc = PMIX_ERROR;
        } else if (val->data.string) {
            (void)strncpy (value, val->data.string, len);
        }
        PMIX_VALUE_RELEASE (val);
    }
    return convert_err(rc);
}

static int pmix_client_spawn_multiple (void *impl,
                                       int count,
                                       const char *cmds[],
                                       const char **argvs[],
                                       const int maxprocs[],
                                       const int info_keyval_sizesp[],
                                       const PMI_keyval_t *info_keyval_vectors[],
                                       int preput_keyval_size,
                                       const PMI_keyval_t preput_keyval_vector[],
                                       int errors[])
{
    return PMI_FAIL;
}

static void pmix_client_destroy (void *impl)
{
    struct pmix_client *pmi = impl;

    free (pmi);
}

static struct pmi_operations pmix_client_operations = {
    .init                       = pmix_client_init,
    .initialized                = pmix_client_initialized,
    .finalize                   = pmix_client_finalize,
    .get_size                   = pmix_client_get_size,
    .get_rank                   = pmix_client_get_rank,
    .get_appnum                 = pmix_client_get_appnum,
    .get_universe_size          = pmix_client_get_universe_size,
    .publish_name               = pmix_client_publish_name,
    .unpublish_name             = pmix_client_unpublish_name,
    .lookup_name                = pmix_client_lookup_name,
    .barrier                    = pmix_client_barrier,
    .abort                      = pmix_client_abort,
    .kvs_get_my_name            = pmix_client_kvs_get_my_name,
    .kvs_get_name_length_max    = pmix_client_kvs_get_name_length_max,
    .kvs_get_key_length_max     = pmix_client_kvs_get_key_length_max,
    .kvs_get_value_length_max   = pmix_client_kvs_get_value_length_max,
    .kvs_put                    = pmix_client_kvs_put,
    .kvs_commit                 = pmix_client_kvs_commit,
    .kvs_get                    = pmix_client_kvs_get,
    .get_clique_size            = NULL,
    .get_clique_ranks           = NULL,
    .spawn_multiple             = pmix_client_spawn_multiple,
    .destroy                    = pmix_client_destroy,
};

void *pmix_client_create (struct pmi_operations **ops)
{
    struct pmix_client *pmi = calloc (1, sizeof (*pmi));

    if (!pmi)
        return NULL;
    *ops = &pmix_client_operations;
    return pmi;
}


/***   UTILITY FUNCTIONS   ***/
/* internal function */
static pmix_status_t convert_int(int *value, pmix_value_t *kv)
{
    switch (kv->type) {
    case PMIX_INT:
        *value = kv->data.integer;
        break;
    case PMIX_INT8:
        *value = kv->data.int8;
        break;
    case PMIX_INT16:
        *value = kv->data.int16;
        break;
    case PMIX_INT32:
        *value = kv->data.int32;
        break;
    case PMIX_INT64:
        *value = kv->data.int64;
        break;
    case PMIX_UINT:
        *value = kv->data.uint;
        break;
    case PMIX_UINT8:
        *value = kv->data.uint8;
        break;
    case PMIX_UINT16:
        *value = kv->data.uint16;
        break;
    case PMIX_UINT32:
        *value = kv->data.uint32;
        break;
    case PMIX_UINT64:
        *value = kv->data.uint64;
        break;
    case PMIX_BYTE:
        *value = kv->data.byte;
        break;
    case PMIX_SIZE:
        *value = kv->data.size;
        break;
    case PMIX_BOOL:
        *value = kv->data.flag;
        break;
    default:
        /* not an integer type */
        return PMIX_ERR_BAD_PARAM;
    }
    return PMIX_SUCCESS;
}

static int convert_err(pmix_status_t rc)
{
    switch (rc) {
    case PMIX_ERR_INVALID_SIZE:
        return PMI_ERR_INVALID_SIZE;

    case PMIX_ERR_INVALID_KEYVALP:
        return PMI_ERR_INVALID_KEYVALP;

    case PMIX_ERR_INVALID_NUM_PARSED:
        return PMI_ERR_INVALID_NUM_PARSED;

    case PMIX_ERR_INVALID_ARGS:
        return PMI_ERR_INVALID_ARGS;

    case PMIX_ERR_INVALID_NUM_ARGS:
        return PMI_ERR_INVALID_NUM_ARGS;

    case PMIX_ERR_INVALID_LENGTH:
        return PMI_ERR_INVALID_LENGTH;

    case PMIX_ERR_INVALID_VAL_LENGTH:
        return PMI_ERR_INVALID_VAL_LENGTH;

    case PMIX_ERR_INVALID_VAL:
        return PMI_ERR_INVALID_VAL;

    case PMIX_ERR_INVALID_KEY_LENGTH:
        return PMI_ERR_INVALID_KEY_LENGTH;

    case PMIX_ERR_INVALID_KEY:
        return PMI_ERR_INVALID_KEY;

    case PMIX_ERR_INVALID_ARG:
        return PMI_ERR_INVALID_ARG;

    case PMIX_ERR_NOMEM:
        return PMI_ERR_NOMEM;

    case PMIX_ERR_UNPACK_READ_PAST_END_OF_BUFFER:
    case PMIX_ERR_LOST_CONNECTION_TO_SERVER:
    case PMIX_ERR_LOST_PEER_CONNECTION:
    case PMIX_ERR_LOST_CONNECTION_TO_CLIENT:
    case PMIX_ERR_NOT_SUPPORTED:
    case PMIX_ERR_NOT_FOUND:
    case PMIX_ERR_SERVER_NOT_AVAIL:
    case PMIX_ERR_INVALID_NAMESPACE:
    case PMIX_ERR_DATA_VALUE_NOT_FOUND:
    case PMIX_ERR_OUT_OF_RESOURCE:
    case PMIX_ERR_RESOURCE_BUSY:
    case PMIX_ERR_BAD_PARAM:
    case PMIX_ERR_IN_ERRNO:
    case PMIX_ERR_UNREACH:
    case PMIX_ERR_TIMEOUT:
    case PMIX_ERR_NO_PERMISSIONS:
    case PMIX_ERR_PACK_MISMATCH:
    case PMIX_ERR_PACK_FAILURE:
    case PMIX_ERR_UNPACK_FAILURE:
    case PMIX_ERR_UNPACK_INADEQUATE_SPACE:
    case PMIX_ERR_TYPE_MISMATCH:
    case PMIX_ERR_PROC_ENTRY_NOT_FOUND:
    case PMIX_ERR_UNKNOWN_DATA_TYPE:
    case PMIX_ERR_WOULD_BLOCK:
    case PMIX_EXISTS:
    case PMIX_ERROR:
        return PMI_FAIL;

    case PMIX_ERR_INIT:
        return PMI_ERR_INIT;

    case PMIX_SUCCESS:
        return PMI_SUCCESS;
    default:
        return PMI_FAIL;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
