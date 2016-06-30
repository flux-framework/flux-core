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
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <stdarg.h>

#include "client.h"
#include "client_impl.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

typedef struct {
    int errnum;
    const char *errstr;
} etab_t;

static etab_t pmi_errors[] = {
    { PMI_SUCCESS,              "operation completed successfully" },
    { PMI_FAIL,                 "operation failed" },
    { PMI_ERR_NOMEM,            "input buffer not large enough" },
    { PMI_ERR_INIT,             "PMI not initialized" },
    { PMI_ERR_INVALID_ARG,      "invalid argument" },
    { PMI_ERR_INVALID_KEY,      "invalid key argument" },
    { PMI_ERR_INVALID_KEY_LENGTH,"invalid key length argument" },
    { PMI_ERR_INVALID_VAL,      "invalid val argument" },
    { PMI_ERR_INVALID_VAL_LENGTH,"invalid val length argument" },
    { PMI_ERR_INVALID_LENGTH,   "invalid length argument" },
    { PMI_ERR_INVALID_NUM_ARGS, "invalid number of arguments" },
    { PMI_ERR_INVALID_ARGS,     "invalid args argument" },
    { PMI_ERR_INVALID_NUM_PARSED, "invalid num_parsed length argument" },
    { PMI_ERR_INVALID_KEYVALP,  "invalid keyvalp argument" },
    { PMI_ERR_INVALID_SIZE,     "invalid size argument" },
};
static const int pmi_errors_len = sizeof (pmi_errors) / sizeof (pmi_errors[0]);

const char *pmi_strerror (int rc)
{
    static char unknown[] = "pmi error XXXXXXXXX";
    int i;

    for (i = 0; i < pmi_errors_len; i++) {
        if (pmi_errors[i].errnum == rc)
            return pmi_errors[i].errstr;
    }
    snprintf (unknown, sizeof (unknown), "pmi error %d", rc);
    return unknown;
}

void pmi_destroy (pmi_t *pmi)
{
    if (pmi) {
        if (pmi->impl_destroy)
            pmi->impl_destroy (pmi->impl);
        free (pmi);
    }
}

pmi_t *pmi_create (void *impl, pmi_free_f destroy)
{
    pmi_t *pmi = xzmalloc (sizeof (*pmi));
    pmi->impl = impl;
    pmi->impl_destroy = destroy;
    return pmi;
}

pmi_t *pmi_create_guess (void)
{
    const char *fd_str = getenv ("PMI_FD");
    const char *rank_str = getenv ("PMI_RANK");
    const char *size_str = getenv ("PMI_SIZE");

    if (fd_str && rank_str && size_str)
        return pmi_create_simple (strtol (fd_str, NULL, 10),
                                  strtol (rank_str, NULL, 10),
                                  strtol (size_str, NULL, 10));
    else if (getenv ("PMIX_SERVER_URI"))
        return pmi_create_dlopen ("libpmix.so");
    else
        return pmi_create_dlopen (NULL);
}

int pmi_init (pmi_t *pmi, int *spawned)
{
    if (!pmi->init)
        return PMI_FAIL;
    return pmi->init (pmi->impl, spawned);
}

int pmi_initialized (pmi_t *pmi, int *initialized)
{
    if (!pmi->initialized)
        return PMI_FAIL;
    return pmi->initialized (pmi->impl, initialized);
}

int pmi_finalize (pmi_t *pmi)
{
    if (!pmi->finalize)
        return PMI_FAIL;
    return pmi->finalize (pmi->impl);
}

int pmi_get_size (pmi_t *pmi, int *size)
{
    if (!pmi->get_size)
        return PMI_FAIL;
    return pmi->get_size (pmi->impl, size);
}

int pmi_get_rank (pmi_t *pmi, int *rank)
{
    if (!pmi->get_rank)
        return PMI_FAIL;
    return pmi->get_rank (pmi->impl, rank);
}

int pmi_get_universe_size (pmi_t *pmi, int *size)
{
    if (!pmi->get_universe_size)
        return PMI_FAIL;
    return pmi->get_universe_size (pmi->impl, size);
}

int pmi_get_appnum (pmi_t *pmi, int *appnum)
{
    if (!pmi->get_appnum)
        return PMI_FAIL;
    return pmi->get_appnum (pmi->impl, appnum);
}

int pmi_publish_name (pmi_t *pmi, const char *service_name, const char *port)
{
    if (!pmi->publish_name)
        return PMI_FAIL;
    return pmi->publish_name (pmi->impl, service_name, port);
}

int pmi_unpublish_name (pmi_t *pmi, const char *service_name)
{
    if (!pmi->publish_name)
        return PMI_FAIL;
    return pmi->unpublish_name (pmi->impl, service_name);
}

int pmi_lookup_name (pmi_t *pmi, const char *service_name, char *port)
{
    if (!pmi->lookup_name)
        return PMI_FAIL;
    return pmi->lookup_name (pmi->impl, service_name, port);
}

int pmi_barrier (pmi_t *pmi)
{
    if (!pmi->barrier)
        return PMI_FAIL;
    return pmi->barrier (pmi->impl);
}

int pmi_abort (pmi_t *pmi, int exit_code, const char *error_msg)
{
    if (!pmi->abort)
        return PMI_FAIL;
    return pmi->abort (pmi->impl, exit_code, error_msg);
}

int pmi_kvs_get_my_name (pmi_t *pmi, char *kvsname, int length)
{
    if (!pmi->kvs_get_my_name)
        return PMI_FAIL;
    return pmi->kvs_get_my_name (pmi->impl, kvsname, length);
}

int pmi_kvs_get_name_length_max (pmi_t *pmi, int *length)
{
    if (!pmi->kvs_get_name_length_max)
        return PMI_FAIL;
    return pmi->kvs_get_name_length_max (pmi->impl, length);
}

int pmi_kvs_get_key_length_max (pmi_t *pmi, int *length)
{
    if (!pmi->kvs_get_key_length_max)
        return PMI_FAIL;
    return pmi->kvs_get_key_length_max (pmi->impl, length);
}

int pmi_kvs_get_value_length_max (pmi_t *pmi, int *length)
{
    if (!pmi->kvs_get_value_length_max)
        return PMI_FAIL;
    return pmi->kvs_get_value_length_max (pmi->impl, length);
}

int pmi_kvs_put (pmi_t *pmi,
        const char *kvsname, const char *key, const char *value)
{
    if (!pmi->kvs_put)
        return PMI_FAIL;
    return pmi->kvs_put (pmi->impl, kvsname, key, value);
}


int pmi_kvs_commit (pmi_t *pmi, const char *kvsname)
{
    if (!pmi->kvs_commit)
        return PMI_FAIL;
    return pmi->kvs_commit (pmi->impl, kvsname);
}

int pmi_kvs_get (pmi_t *pmi,
        const char *kvsname, const char *key, char *value, int len)
{
    if (!pmi->kvs_get)
        return PMI_FAIL;
    return pmi->kvs_get (pmi->impl, kvsname, key, value, len);
}

int pmi_spawn_multiple (pmi_t *pmi,
        int count,
        const char *cmds[],
        const char **argvs[],
        const int maxprocs[],
        const int info_keyval_sizesp[],
        const pmi_keyval_t *info_keyval_vectors[],
        int preput_keyval_size,
        const pmi_keyval_t preput_keyval_vector[],
        int errors[])
{
    if (!pmi->spawn_multiple)
        return PMI_FAIL;
    return pmi->spawn_multiple (pmi->impl, count, cmds, argvs, maxprocs,
            info_keyval_sizesp, info_keyval_vectors, preput_keyval_size,
            preput_keyval_vector, errors);
}

int pmi_get_id (pmi_t *pmi, char *id_str, int length)
{
    if (!pmi->get_id)
        return PMI_FAIL;
    return pmi->get_id (pmi->impl, id_str, length);
}

int pmi_get_kvs_domain_id (pmi_t *pmi, char *id_str, int length)
{
    if (!pmi->get_kvs_domain_id)
        return PMI_FAIL;
    return pmi->get_kvs_domain_id (pmi->impl, id_str, length);
}

int pmi_get_id_length_max (pmi_t *pmi, int *length)
{
    if (!pmi->get_id_length_max)
        return PMI_FAIL;
    return pmi->get_id_length_max (pmi->impl, length);
}

int pmi_get_clique_size (pmi_t *pmi, int *size)
{
    if (!pmi->get_clique_size)
        return PMI_FAIL;
    return pmi->get_clique_size (pmi->impl, size);
}

int pmi_get_clique_ranks (pmi_t *pmi, int *ranks, int length)
{
    if (!pmi->get_clique_ranks)
        return PMI_FAIL;
    return pmi->get_clique_ranks (pmi->impl, ranks, length);
}

int pmi_kvs_create (pmi_t *pmi, char *kvsname, int length)
{
    if (!pmi->kvs_create)
        return PMI_FAIL;
    return pmi->kvs_create (pmi->impl, kvsname, length);
}

int pmi_kvs_destroy (pmi_t *pmi, const char *kvsname)
{
    if (!pmi->kvs_destroy)
        return PMI_FAIL;
    return pmi->kvs_destroy (pmi->impl, kvsname);
}

int pmi_kvs_iter_first (pmi_t *pmi, const char *kvsname,
        char *key, int key_len, char *val, int val_len)
{
    if (!pmi->kvs_iter_first)
        return PMI_FAIL;
    return pmi->kvs_iter_first (pmi->impl, kvsname, key, key_len, val, val_len);
}

int pmi_kvs_iter_next (pmi_t *pmi, const char *kvsname,
        char *key, int key_len, char *val, int val_len)
{
    if (!pmi->kvs_iter_next)
        return PMI_FAIL;
    return pmi->kvs_iter_next (pmi->impl, kvsname, key, key_len, val, val_len);
}

int pmi_parse_option (pmi_t *pmi, int num_args, char *args[],
        int *num_parsed, pmi_keyval_t **keyvalp, int *size)
{
    if (!pmi->parse_option)
        return PMI_FAIL;
    return pmi->parse_option (pmi->impl, num_args, args, num_parsed,
            keyvalp, size);
}

int pmi_args_to_keyval (pmi_t *pmi, int *argcp, char *((*argvp)[]),
        pmi_keyval_t **keyvalp, int *size)
{
    if (!pmi->args_to_keyval)
        return PMI_FAIL;
    return pmi->args_to_keyval (pmi->impl, argcp, argvp, keyvalp, size);
}

int pmi_free_keyvals (pmi_t *pmi, pmi_keyval_t keyvalp[], int size)
{
    if (!pmi->free_keyvals)
        return PMI_FAIL;
    return pmi->free_keyvals (pmi->impl, keyvalp, size);
}

int pmi_get_options (pmi_t *pmi, char *str, int length)
{
    if (!pmi->get_options)
        return PMI_FAIL;
    return pmi->get_options (pmi->impl, str, length);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
