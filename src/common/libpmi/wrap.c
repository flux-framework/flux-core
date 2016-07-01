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

#include "pmi.h"
#include "wrap.h"

struct pmi_wrap {
    void *dso;
};

int pmi_wrap_init (struct pmi_wrap *pmi, int *spawned)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Init");
    return f ? f (spawned) : PMI_FAIL;
}

int pmi_wrap_initialized (struct pmi_wrap *pmi, int *initialized)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Initialized");
    return f ? f (initialized) : PMI_FAIL;
}

int pmi_wrap_finalize (struct pmi_wrap *pmi)
{
    int (*f)(void) = dlsym (pmi->dso, "PMI_Finalize");
    return f ? f () : PMI_FAIL;
}

int pmi_wrap_get_size (struct pmi_wrap *pmi, int *size)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_size");
    return f ? f (size) : PMI_FAIL;
}

int pmi_wrap_get_rank (struct pmi_wrap *pmi, int *rank)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_rank");
    return f ? f (rank) : PMI_FAIL;
}

int pmi_wrap_get_universe_size (struct pmi_wrap *pmi, int *size)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_universe_size");
    return f ? f (size) : PMI_FAIL;
}

int pmi_wrap_get_appnum (struct pmi_wrap *pmi, int *appnum)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_appnum");
    return f ? f (appnum) : PMI_FAIL;
}

int pmi_wrap_barrier (struct pmi_wrap *pmi)
{
    int (*f)(void) = dlsym (pmi->dso, "PMI_Barrier");
    return f ? f () : PMI_FAIL;
}

int pmi_wrap_abort (struct pmi_wrap *pmi, int exit_code, const char *error_msg)
{
    int (*f)(int, const char *) = dlsym (pmi->dso, "PMI_Abort");
    return f ? f (exit_code, error_msg) : PMI_FAIL;
}

int pmi_wrap_kvs_get_my_name (struct pmi_wrap *pmi, char *kvsname, int length)
{
    int (*f)(char *, int) = dlsym (pmi->dso, "PMI_KVS_Get_my_name");
    return f ? f (kvsname, length) : PMI_FAIL;
}

int pmi_wrap_kvs_get_name_length_max (struct pmi_wrap *pmi, int *length)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_name_length_max");
    return f ? f (length) : PMI_FAIL;
}

int pmi_wrap_kvs_get_key_length_max (struct pmi_wrap *pmi, int *length)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_key_length_max");
    return f ? f (length) : PMI_FAIL;
}

int pmi_wrap_kvs_get_value_length_max (struct pmi_wrap *pmi, int *length)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_KVS_Get_value_length_max");
    return f ? f (length) : PMI_FAIL;
}

int pmi_wrap_kvs_put (struct pmi_wrap *pmi, const char *kvsname,
                      const char *key, const char *value)
{
    int (*f)(const char *, const char *, const char *) = dlsym (pmi->dso,
                                                                "PMI_KVS_Put");
    return f ? f (kvsname, key, value) : PMI_FAIL;
}

int pmi_wrap_kvs_commit (struct pmi_wrap *pmi, const char *kvsname)
{
    int (*f)(const char *) = dlsym (pmi->dso, "PMI_KVS_Commit");
    return f ? f (kvsname) : PMI_FAIL;
}

int pmi_wrap_kvs_get (struct pmi_wrap *pmi, const char *kvsname,
                      const char *key, char *value, int len)
{
    int (*f)(const char *, const char *, char *, int) = dlsym (pmi->dso,
                                                               "PMI_KVS_Get");
    return f ? f (kvsname, key, value, len) : PMI_FAIL;
}

int pmi_wrap_get_clique_size (struct pmi_wrap *pmi, int *size)
{
    int (*f)(int *) = dlsym (pmi->dso, "PMI_Get_clique_size");
    return f ? f (size) : PMI_FAIL;
}

int pmi_wrap_get_clique_ranks (struct pmi_wrap *pmi, int *ranks, int length)
{
    int (*f)(int *, int) = dlsym (pmi->dso, "PMI_Get_clique_ranks");
    return f ? f (ranks, length) : PMI_FAIL;
}

int pmi_wrap_publish_name (struct pmi_wrap *pmi,
                           const char *service_name, const char *port)
{
    int (*f)(const char *, const char *) = dlsym (pmi->dso, "PMI_Publish_name");
    return f ? f (service_name, port) : PMI_FAIL;
}

int pmi_wrap_unpublish_name (struct pmi_wrap *pmi, const char *service_name)
{
    int (*f)(const char *) = dlsym (pmi->dso, "PMI_Unpublish_name");
    return f ? f (service_name) : PMI_FAIL;
}

int pmi_wrap_lookup_name (struct pmi_wrap *pmi,
                          const char *service_name, char *port)
{
    int (*f)(const char *, const char *) = dlsym (pmi->dso, "PMI_Lookup_name");
    return f ? f (service_name, port) : PMI_FAIL;
}

int pmi_wrap_spawn_multiple (struct pmi_wrap *pmi,
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
    int (*f)(int, const char **, const char ***, const int *, const int *,
             const PMI_keyval_t **, int, const PMI_keyval_t *, int *);
    if ((f = dlsym (pmi->dso, "PMI_Lookup_name")))
        return f (count, cmds, argvs, maxprocs, info_keyval_sizesp,
                  info_keyval_vectors, preput_keyval_size, preput_keyval_vector,
                  errors);
    return PMI_FAIL;
}

void pmi_wrap_destroy (struct pmi_wrap *pmi)
{
    if (pmi) {
        if (pmi->dso)
            dlclose (pmi->dso);
        free (pmi);
    }
}

struct pmi_wrap *pmi_wrap_create (const char *libname)
{
    struct pmi_wrap *pmi = calloc (1, sizeof (*pmi));

    if (!pmi)
        return NULL;
    dlerror ();
    /* RTLD_GLOBAL due to issue #432 */
    if (!(pmi->dso = dlopen (libname, RTLD_NOW | RTLD_GLOBAL))) {
        pmi_wrap_destroy (pmi);
        return NULL;
    }
    /* avoid opening self */
    if (dlsym (pmi->dso, "flux_pmi_library")) {
        pmi_wrap_destroy (pmi);
        return NULL;
    }
    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
