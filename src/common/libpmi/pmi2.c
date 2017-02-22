/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <string.h>

#include "pmi.h"
#include "pmi2.h"
#include "pmi_strerror.h"

struct pmi2_context {
    int name_length_max;
    int key_length_max;
    int value_length_max;
    char *kvs_name;
    char *value;
    int debug;
    int initialized;
    int rank;
    int appnum;
};
static struct pmi2_context ctx = { .initialized = 0, .rank = -1 };

#define DPRINTF(fmt,...) do { \
        if (ctx.debug) fprintf (stderr, fmt, ##__VA_ARGS__); \
} while (0)

#define DRETURN(rc) do { \
        DPRINTF ("%d: %s rc=%d\n", ctx.rank, __FUNCTION__, (rc)); \
        return (rc); \
} while (0);

struct errmap {
    int pmi_error;
    int pmi2_error;
};

static struct errmap errmap[] = {

    { PMI_SUCCESS,                  PMI2_SUCCESS },
    { PMI_FAIL,                     PMI2_FAIL },
    { PMI_ERR_INIT,                 PMI2_ERR_INIT },
    { PMI_ERR_NOMEM,                PMI2_ERR_NOMEM },
    { PMI_ERR_INVALID_ARG,          PMI2_ERR_INVALID_ARG },
    { PMI_ERR_INVALID_KEY,          PMI2_ERR_INVALID_KEY },
    { PMI_ERR_INVALID_KEY_LENGTH,   PMI2_ERR_INVALID_KEY_LENGTH },
    { PMI_ERR_INVALID_VAL,          PMI2_ERR_INVALID_VAL },
    { PMI_ERR_INVALID_VAL_LENGTH,   PMI2_ERR_INVALID_VAL_LENGTH },
    { PMI_ERR_INVALID_LENGTH,       PMI2_ERR_INVALID_LENGTH },
    { PMI_ERR_INVALID_NUM_ARGS,     PMI2_ERR_INVALID_NUM_ARGS },
    { PMI_ERR_INVALID_ARGS,         PMI2_ERR_INVALID_ARGS },
    { PMI_ERR_INVALID_NUM_PARSED,   PMI2_ERR_INVALID_NUM_PARSED },
    { PMI_ERR_INVALID_KEYVALP,      PMI2_ERR_INVALID_KEYVALP },
    { PMI_ERR_INVALID_SIZE,         PMI2_ERR_INVALID_SIZE },
};

static int map_pmi_error (int errnum)
{
    int i;
    for (i = 0; i < sizeof (errmap) / sizeof (errmap[0]); i++)
        if (errmap[i].pmi_error == errnum)
            return errmap[i].pmi2_error;
    return PMI2_ERR_OTHER;
}

int PMI2_Init (int *spawned, int *size, int *rank, int *appnum)
{
    const char *debug;
    if ((debug = getenv ("FLUX_PMI2_DEBUG")))
        ctx.debug = strtol (debug, NULL, 0);
    else
        ctx.debug = 0;
    if (ctx.initialized)
        return PMI2_FAIL;
    int e;
    if ((e = PMI_Init (spawned)) != PMI_SUCCESS)
        goto done;
    if ((e = PMI_Get_size (size)) != PMI_SUCCESS)
        goto done;
    if ((e = PMI_Get_rank (rank)) != PMI_SUCCESS)
        goto done;
    ctx.rank = *rank;
    if ((e = PMI_Get_appnum (appnum)) != PMI_SUCCESS)
        goto done;
    ctx.appnum = *appnum;
    if ((e = PMI_KVS_Get_name_length_max (&ctx.name_length_max)) != PMI_SUCCESS)
        goto done;
    if ((e = PMI_KVS_Get_key_length_max (&ctx.key_length_max)) != PMI_SUCCESS)
        goto done;
    if ((e = PMI_KVS_Get_value_length_max (&ctx.value_length_max))
                                                            != PMI_SUCCESS)
        goto done;
    if (!(ctx.kvs_name = calloc (1, ctx.name_length_max)))
        return PMI2_ERR_NOMEM;
    if (!(ctx.value = calloc (1, ctx.value_length_max))) {
        free (ctx.kvs_name);
        return PMI2_ERR_NOMEM;
    }
    if ((e = PMI_KVS_Get_my_name (ctx.kvs_name, ctx.name_length_max))
                                                            != PMI_SUCCESS) {
        free (ctx.kvs_name);
        free (ctx.value);
        goto done;
    }
    ctx.initialized = 1;
done:
    DRETURN (map_pmi_error (e));
}

int PMI2_Finalize (void)
{
    if (ctx.initialized) {
        free (ctx.value);
        free (ctx.kvs_name);
        ctx.initialized = 0;
    }
    int e = PMI_Finalize();
    DRETURN (map_pmi_error (e));
}

int PMI2_Initialized (void)
{
    int initialized;
    if (PMI_Initialized (&initialized) != PMI_SUCCESS || !initialized)
        DRETURN (0);
    DRETURN (1);
}

int PMI2_Abort (int flag, const char msg[])
{
    if (flag) { /* global abort */
        int e;
        e = PMI_Abort (1, msg);
        return map_pmi_error (e);
    } else {    /* local abort */
        fprintf (stderr, "PMI2_Abort: %s\n", msg);
        exit (1);
    }
    /*NOTREACHED*/
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
    DRETURN (PMI2_FAIL);
}

int PMI2_Job_GetId (char jobid[], int jobid_size)
{
    int e = PMI_ERR_INIT;
    if (!ctx.initialized)
        goto done;
    snprintf (jobid, jobid_size, "%d", ctx.appnum);
    e = PMI_SUCCESS;
done:
    DRETURN (map_pmi_error (e));
}

int PMI2_Job_GetRank (int* rank)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Job_Connect (const char jobid[], PMI2_Connect_comm_t *conn)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Job_Disconnect (const char jobid[])
{
    DRETURN (PMI2_FAIL);
}

int PMI2_KVS_Put (const char key[], const char value[])
{
    int e = PMI_ERR_INIT;

    if (!ctx.initialized)
        goto done;
    e = PMI_KVS_Put (ctx.kvs_name, key, value);
done:
    DRETURN (map_pmi_error (e));
}

int PMI2_KVS_Get (const char *jobid, int src_pmi_id,
                  const char key[], char value [], int maxvalue, int *vallen)
{
    int e = PMI_ERR_INIT;
    int len;
    if (!ctx.initialized)
        goto done;
    if ((e = PMI_KVS_Get (ctx.kvs_name, key, ctx.value, ctx.value_length_max))
                                                    != PMI_SUCCESS)
        goto done;
    len = strlen (ctx.value) + 1;
    if (len <= maxvalue) {
        *vallen = len;
    } else {
        *vallen = -1*len;
        len = maxvalue;
    }
    memcpy (value, ctx.value, len);
done:
    DRETURN (map_pmi_error (e));
}

int PMI2_KVS_Fence (void)
{
    int e = PMI_ERR_INIT;
    if (!ctx.initialized)
        goto done;
    if ((e = PMI_KVS_Commit (ctx.kvs_name)) != PMI_SUCCESS)
        goto done;
    if ((e = PMI_Barrier ()) != PMI_SUCCESS)
        goto done;
done:
    DRETURN (map_pmi_error (e));
}


int PMI2_Info_GetSize (int* size)
{
    /* FIXME: return #procs on local node */
    DRETURN (PMI2_FAIL);
}

int PMI2_Info_GetNodeAttr (const char name[],
                           char value[], int valuelen, int *found, int waitfor)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Info_GetNodeAttrIntArray (const char name[], int array[],
                                   int arraylen, int *outlen, int *found)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Info_PutNodeAttr (const char name[], const char value[])
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Info_GetJobAttr (const char name[],
                          char value[], int valuelen, int *found)
{
    int e;
    if (strcmp (name, "PMI_process_mapping") != 0) {
        *found = 0;
        e = PMI2_SUCCESS;
        goto done;
    }
    if ((e = PMI_KVS_Get (ctx.kvs_name, name, value, valuelen)) != PMI_SUCCESS)
        goto done;
    *found = 1;
done:
    DRETURN (map_pmi_error (e));
}

int PMI2_Info_GetJobAttrIntArray (const char name[], int array[],
                                  int arraylen, int *outlen, int *found)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Nameserv_publish (const char service_name[],
                           const struct MPID_Info *info_ptr, const char port[])
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Nameserv_lookup (const char service_name[],
                          const struct MPID_Info *info_ptr,
                          char port[], int portLen)
{
    DRETURN (PMI2_FAIL);
}

int PMI2_Nameserv_unpublish (const char service_name[],
                             const struct MPID_Info *info_ptr)
{
    DRETURN (PMI2_FAIL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
