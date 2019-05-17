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
#    include "config.h"
#endif
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <czmq.h>

#include "single.h"

#define KVS_KEY_MAX 64
#define KVS_VAL_MAX 512
#define KVS_NAME_MAX 64

struct pmi_single {
    int rank;
    int size;
    int spawned;
    int initialized;
    zhash_t *kvs;
    char kvsname[KVS_NAME_MAX];
};

static int pmi_single_init (void *impl, int *spawned)
{
    struct pmi_single *pmi = impl;
    pmi->initialized = 1;
    *spawned = pmi->spawned;
    return PMI_SUCCESS;
}

static int pmi_single_initialized (void *impl, int *initialized)
{
    struct pmi_single *pmi = impl;
    *initialized = pmi->initialized;
    return PMI_SUCCESS;
}

static int pmi_single_finalize (void *impl)
{
    struct pmi_single *pmi = impl;
    pmi->initialized = 0;
    return PMI_SUCCESS;
}

static int pmi_single_get_size (void *impl, int *size)
{
    struct pmi_single *pmi = impl;
    *size = pmi->size;
    return PMI_SUCCESS;
}

static int pmi_single_get_rank (void *impl, int *rank)
{
    struct pmi_single *pmi = impl;
    *rank = pmi->rank;
    return PMI_SUCCESS;
}

static int pmi_single_get_appnum (void *impl, int *appnum)
{
    *appnum = getpid ();
    return PMI_SUCCESS;
}

static int pmi_single_get_universe_size (void *impl, int *universe_size)
{
    struct pmi_single *pmi = impl;
    *universe_size = pmi->size;
    return PMI_SUCCESS;
}

static int pmi_single_publish_name (void *impl,
                                    const char *service_name,
                                    const char *port)
{
    return PMI_FAIL;
}

static int pmi_single_unpublish_name (void *impl, const char *service_name)
{
    return PMI_FAIL;
}

static int pmi_single_lookup_name (void *impl,
                                   const char *service_name,
                                   char *port)
{
    return PMI_FAIL;
}

static int pmi_single_barrier (void *impl)
{
    return PMI_SUCCESS;
}

static int pmi_single_abort (void *impl, int exit_code, const char *error_msg)
{
    fprintf (stderr, "PMI_Abort: %s\n", error_msg);
    exit (exit_code);
    /*NOTREACHED*/
    return PMI_SUCCESS;
}

static int pmi_single_kvs_get_my_name (void *impl, char *kvsname, int length)
{
    struct pmi_single *pmi = impl;
    if (length < strlen (pmi->kvsname) + 1)
        return PMI_ERR_INVALID_LENGTH;
    strcpy (kvsname, pmi->kvsname);
    return PMI_SUCCESS;
}

static int pmi_single_kvs_get_name_length_max (void *impl, int *length)
{
    *length = KVS_NAME_MAX;
    return PMI_SUCCESS;
}

static int pmi_single_kvs_get_key_length_max (void *impl, int *length)
{
    *length = KVS_KEY_MAX;
    return PMI_SUCCESS;
}

static int pmi_single_kvs_get_value_length_max (void *impl, int *length)
{
    *length = KVS_VAL_MAX;
    return PMI_SUCCESS;
}

static int pmi_single_kvs_put (void *impl,
                               const char *kvsname,
                               const char *key,
                               const char *value)
{
    struct pmi_single *pmi = impl;
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

static int pmi_single_kvs_commit (void *impl, const char *kvsname)
{
    return PMI_SUCCESS; /* a no-op here */
}

static int pmi_single_kvs_get (void *impl,
                               const char *kvsname,
                               const char *key,
                               char *value,
                               int len)
{
    struct pmi_single *pmi = impl;
    char *val = zhash_lookup (pmi->kvs, key);
    if (!val)
        return PMI_ERR_INVALID_KEY;
    if (len < strlen (val) + 1)
        return PMI_ERR_INVALID_VAL_LENGTH;
    strcpy (value, val);
    return PMI_SUCCESS;
}

static int pmi_single_spawn_multiple (void *impl,
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

static void pmi_single_destroy (void *impl)
{
    struct pmi_single *pmi = impl;
    if (pmi) {
        zhash_destroy (&pmi->kvs);
        free (pmi);
    }
}

static struct pmi_operations pmi_single_operations = {
    .init = pmi_single_init,
    .initialized = pmi_single_initialized,
    .finalize = pmi_single_finalize,
    .get_size = pmi_single_get_size,
    .get_rank = pmi_single_get_rank,
    .get_appnum = pmi_single_get_appnum,
    .get_universe_size = pmi_single_get_universe_size,
    .publish_name = pmi_single_publish_name,
    .unpublish_name = pmi_single_unpublish_name,
    .lookup_name = pmi_single_lookup_name,
    .barrier = pmi_single_barrier,
    .abort = pmi_single_abort,
    .kvs_get_my_name = pmi_single_kvs_get_my_name,
    .kvs_get_name_length_max = pmi_single_kvs_get_name_length_max,
    .kvs_get_key_length_max = pmi_single_kvs_get_key_length_max,
    .kvs_get_value_length_max = pmi_single_kvs_get_value_length_max,
    .kvs_put = pmi_single_kvs_put,
    .kvs_commit = pmi_single_kvs_commit,
    .kvs_get = pmi_single_kvs_get,
    .get_clique_size = NULL,
    .get_clique_ranks = NULL,
    .spawn_multiple = pmi_single_spawn_multiple,
    .destroy = pmi_single_destroy,
};

void *pmi_single_create (struct pmi_operations **ops)
{
    struct pmi_single *pmi = calloc (1, sizeof (*pmi));

    if (!pmi || !(pmi->kvs = zhash_new ()))
        goto error;
    pmi->rank = 0;
    pmi->size = 1;
    pmi->spawned = 0;
    pmi->initialized = 0;
    snprintf (pmi->kvsname, sizeof (pmi->kvsname), "single-%d", getpid ());
    if (pmi_single_kvs_put (pmi, pmi->kvsname, "PMI_process_mapping", "")
        != PMI_SUCCESS)
        goto error;
    *ops = &pmi_single_operations;
    return pmi;
error:
    pmi_single_destroy (pmi);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
