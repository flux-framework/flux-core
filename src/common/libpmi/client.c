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

#include "pmi_strerror.h"
#include "client.h"
#include "client_impl.h"
#include "simple_client.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

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
    if (getenv ("PMI_FD"))
        return pmi_create_simple ();
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

pmi_t *pmi_create_simple (void)
{
    pmi_t *pmi;
    struct pmi_simple_client *cli = pmi_simple_client_create ();

    if (!cli)
        return NULL;
    if (!(pmi = pmi_create (cli, (pmi_free_f)(pmi_simple_client_destroy)))) {
        pmi_simple_client_destroy (cli);
        return NULL;
    }
    pmi->init = (pmi_init_f)pmi_simple_client_init;
    pmi->initialized = (pmi_get_int_f)pmi_simple_client_initialized;
    pmi->finalize = (pmi_finalize_f)pmi_simple_client_finalize;
    pmi->get_size = (pmi_get_int_f)pmi_simple_client_get_size;
    pmi->get_rank = (pmi_get_int_f)pmi_simple_client_get_rank;
    pmi->get_universe_size = (pmi_get_int_f)pmi_simple_client_get_universe_size;
    pmi->get_appnum = (pmi_get_int_f)pmi_simple_client_get_appnum;
    pmi->barrier = (pmi_barrier_f)pmi_simple_client_barrier;
    pmi->abort = (pmi_abort_f)pmi_simple_client_abort;
    pmi->kvs_get_my_name = (pmi_kvs_get_my_name_f)pmi_simple_client_kvs_get_my_name;
    pmi->kvs_get_name_length_max = (pmi_get_int_f)pmi_simple_client_kvs_get_name_length_max;
    pmi->kvs_get_key_length_max = (pmi_get_int_f)pmi_simple_client_kvs_get_key_length_max;
    pmi->kvs_get_value_length_max = (pmi_get_int_f)pmi_simple_client_kvs_get_value_length_max;
    pmi->kvs_put = (pmi_kvs_put_f)pmi_simple_client_kvs_put;
    pmi->kvs_commit = (pmi_kvs_commit_f)pmi_simple_client_kvs_commit;
    pmi->kvs_get = (pmi_kvs_get_f)pmi_simple_client_kvs_get;
    pmi->get_clique_size  = NULL; // FIXME
    pmi->get_clique_ranks = NULL; // FIXME

    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
