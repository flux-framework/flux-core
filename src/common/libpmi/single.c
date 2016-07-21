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
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <czmq.h>

#include "single.h"

#define KVS_KEY_MAX         64
#define KVS_VAL_MAX         512
#define KVS_NAME_MAX        64

struct pmi_single {
    int rank;
    int size;
    int spawned;
    int initialized;
    zhash_t *kvs;
    char kvsname[KVS_NAME_MAX];
};

int pmi_single_init (struct pmi_single *pmi, int *spawned)
{
    pmi->initialized = 1;
    *spawned = pmi->spawned;
    return PMI_SUCCESS;
}

int pmi_single_initialized (struct pmi_single *pmi, int *initialized)
{
    *initialized = pmi->initialized;
    return PMI_SUCCESS;
}

int pmi_single_finalize (struct pmi_single *pmi)
{
    pmi->initialized = 0;
    return PMI_SUCCESS;
}

int pmi_single_get_size (struct pmi_single *pmi, int *size)
{
    *size = pmi->size;
    return PMI_SUCCESS;
}

int pmi_single_get_rank (struct pmi_single *pmi, int *rank)
{
    *rank = pmi->rank;
    return PMI_SUCCESS;
}

int pmi_single_get_appnum (struct pmi_single *pmi, int *appnum)
{
    *appnum = -1;
    return PMI_SUCCESS;
}

int pmi_single_get_universe_size (struct pmi_single *pmi, int *universe_size)
{
    *universe_size = pmi->size;
    return PMI_SUCCESS;
}

int pmi_single_publish_name (struct pmi_single *pmi,
                             const char *service_name, const char *port)
{
    return PMI_FAIL;
}

int pmi_single_unpublish_name (struct pmi_single *pmi,
                               const char *service_name)
{
    return PMI_FAIL;
}

int pmi_single_lookup_name (struct pmi_single *pmi,
                            const char *service_name, char *port)
{
    return PMI_FAIL;
}

int pmi_single_barrier (struct pmi_single *pmi)
{
    return PMI_SUCCESS;
}

int pmi_single_abort (struct pmi_single *pmi,
                      int exit_code, const char *error_msg)
{
    fprintf (stderr, "PMI_Abort: %s\n", error_msg);
    exit (exit_code);
    /*NOTREACHED*/
    return PMI_SUCCESS;
}

int pmi_single_kvs_get_my_name (struct pmi_single *pmi,
                                char *kvsname, int length)
{
    if (length < strlen (pmi->kvsname) + 1)
        return PMI_ERR_INVALID_LENGTH;
    strcpy (kvsname, pmi->kvsname);
    return PMI_SUCCESS;
}

int pmi_single_kvs_get_name_length_max (struct pmi_single *pmi, int *length)
{
    *length = KVS_NAME_MAX;
    return PMI_SUCCESS;
}

int pmi_single_kvs_get_key_length_max (struct pmi_single *pmi, int *length)
{
    *length = KVS_KEY_MAX;
    return PMI_SUCCESS;
}

int pmi_single_kvs_get_value_length_max (struct pmi_single *pmi, int *length)
{
    *length = KVS_VAL_MAX;
    return PMI_SUCCESS;
}

int pmi_single_kvs_put (struct pmi_single *pmi, const char *kvsname,
                        const char *key, const char *value)
{
    char *cpy;

    if (strcmp (kvsname, pmi->kvsname))
        return PMI_ERR_INVALID_ARG;
    if (!(cpy = strdup (value)))
        return PMI_ERR_NOMEM;
    if (zhash_insert (pmi->kvs, key, cpy) < 0) {
        free (cpy);
        return PMI_ERR_INVALID_KEY;
    }
    zhash_freefn (pmi->kvs, key, free);
    return PMI_SUCCESS;
}

int pmi_single_kvs_commit (struct pmi_single *pmi, const char *kvsname)
{
    return PMI_SUCCESS; /* a no-op here */
}

int pmi_single_kvs_get (struct pmi_single *pmi, const char *kvsname,
                        const char *key, char *value, int len)
{
    char *val = zhash_lookup (pmi->kvs, key);
    if (!val)
        return PMI_ERR_INVALID_KEY;
    if (len< strlen (val) + 1)
        return PMI_ERR_INVALID_VAL_LENGTH;
    strcpy (value, val);
    return PMI_SUCCESS;
}

int pmi_single_spawn_multiple (struct pmi_single *pmi,
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

void pmi_single_destroy (struct pmi_single *pmi)
{
    if (pmi) {
        zhash_destroy (&pmi->kvs);
        free (pmi);
    }
}

struct pmi_single *pmi_single_create (void)
{
    struct pmi_single *pmi = calloc (1, sizeof (*pmi));

    if (!pmi || !(pmi->kvs = zhash_new ()))
        goto error;
    pmi->rank = 0;
    pmi->size = 1;
    pmi->spawned = 0;
    pmi->initialized = 0;
    snprintf (pmi->kvsname, sizeof (pmi->kvsname), "single-%d", getpid ());
    if (pmi_single_kvs_put (pmi, pmi->kvsname,
                            "PMI_process_mapping", "") != PMI_SUCCESS)
        goto error;
    return pmi;
error:
    pmi_single_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
