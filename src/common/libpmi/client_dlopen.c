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

#include "client.h"
#include "client_impl.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

struct dlopen_impl {
    void *dso;
};

void destroy_dlopen (void *impl)
{
    struct dlopen_impl *d = impl;
    if (d) {
        if (d->dso)
            dlclose (d->dso);
        free (d);
    }
}

static int dlopen_init (void *impl, int *spawned)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Init");
    return f ? f (spawned) : PMI_FAIL;
}

static int dlopen_initialized (void *impl, int *initialized)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Initialized");
    return f ? f (initialized) : PMI_FAIL;
}

static int dlopen_finalize (void *impl)
{
    struct dlopen_impl *d = impl;
    int (*f)(void) = dlsym (d->dso, "PMI_Finalize");
    return f ? f () : PMI_FAIL;
}

static int dlopen_get_size (void *impl, int *size)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Get_size");
    return f ? f (size) : PMI_FAIL;
}

static int dlopen_get_rank (void *impl, int *rank)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Get_rank");
    return f ? f (rank) : PMI_FAIL;
}

static int dlopen_get_universe_size (void *impl, int *size)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Get_universe_size");
    return f ? f (size) : PMI_FAIL;
}

static int dlopen_get_appnum (void *impl, int *appnum)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Get_appnum");
    return f ? f (appnum) : PMI_FAIL;
}

static int dlopen_barrier (void *impl)
{
    struct dlopen_impl *d = impl;
    int (*f)(void) = dlsym (d->dso, "PMI_Barrier");
    return f ? f () : PMI_FAIL;
}

static int dlopen_abort (void *impl,
        int exit_code, const char *error_msg)
{
    struct dlopen_impl *d = impl;
    int (*f)(int, const char *) = dlsym (d->dso, "PMI_Abort");
    return f ? f (exit_code, error_msg) : PMI_FAIL;
}

static int dlopen_kvs_get_my_name (void *impl,
        char *kvsname, int length)
{
    struct dlopen_impl *d = impl;
    int (*f)(char *, int) = dlsym (d->dso, "PMI_KVS_Get_my_name");
    return f ? f (kvsname, length) : PMI_FAIL;
}

static int dlopen_kvs_get_name_length_max (void *impl, int *length)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_KVS_Get_name_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int dlopen_kvs_get_key_length_max (void *impl, int *length)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_KVS_Get_key_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int dlopen_kvs_get_value_length_max (void *impl, int *length)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_KVS_Get_value_length_max");
    return f ? f (length) : PMI_FAIL;
}

static int dlopen_kvs_put (void *impl,
        const char *kvsname, const char *key, const char *value)
{
    struct dlopen_impl *d = impl;
    int (*f)(const char *, const char *, const char *) = dlsym (d->dso,
                                                                "PMI_KVS_Put");
    return f ? f (kvsname, key, value) : PMI_FAIL;
}

static int dlopen_kvs_commit (void *impl, const char *kvsname)
{
    struct dlopen_impl *d = impl;
    int (*f)(const char *) = dlsym (d->dso, "PMI_KVS_Commit");
    return f ? f (kvsname) : PMI_FAIL;
}

static int dlopen_kvs_get (void *impl,
        const char *kvsname, const char *key, char *value, int len)
{
    struct dlopen_impl *d = impl;
    int (*f)(const char *, const char *, char *, int) = dlsym (d->dso,
                                                               "PMI_KVS_Get");
    return f ? f (kvsname, key, value, len) : PMI_FAIL;
}

static int dlopen_get_clique_size (void *impl, int *size)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *) = dlsym (d->dso, "PMI_Get_clique_size");
    return f ? f (size) : PMI_FAIL;
}

static int dlopen_get_clique_ranks (void *impl,
        int *ranks, int length)
{
    struct dlopen_impl *d = impl;
    int (*f)(int *, int) = dlsym (d->dso, "PMI_Get_clique_ranks");
    return f ? f (ranks, length) : PMI_FAIL;
}

pmi_t *pmi_create_dlopen (const char *libname)
{
    struct dlopen_impl *d = xzmalloc (sizeof (*d));
    pmi_t *pmi;

    if (!libname)
        libname = "libpmi.so";
    dlerror ();
    /* RTLD_GLOBAL due to issue #432 */
    if (!(d->dso = dlopen (libname, RTLD_NOW | RTLD_GLOBAL))
            || !(pmi = pmi_create (d, destroy_dlopen))) {
        destroy_dlopen (d);
        return NULL;
    }
    pmi->init = dlopen_init;
    pmi->initialized = dlopen_initialized;
    pmi->finalize = dlopen_finalize;
    pmi->get_size = dlopen_get_size;
    pmi->get_rank = dlopen_get_rank;
    pmi->get_universe_size = dlopen_get_universe_size;
    pmi->get_appnum = dlopen_get_appnum;
    pmi->barrier = dlopen_barrier;
    pmi->abort = dlopen_abort;
    pmi->kvs_get_my_name = dlopen_kvs_get_my_name;
    pmi->kvs_get_name_length_max = dlopen_kvs_get_name_length_max;
    pmi->kvs_get_key_length_max = dlopen_kvs_get_key_length_max;
    pmi->kvs_get_value_length_max = dlopen_kvs_get_value_length_max;
    pmi->kvs_put = dlopen_kvs_put;
    pmi->kvs_commit = dlopen_kvs_commit;
    pmi->kvs_get = dlopen_kvs_get;

    pmi->get_clique_size  = dlopen_get_clique_size;
    pmi->get_clique_ranks = dlopen_get_clique_ranks;

    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
