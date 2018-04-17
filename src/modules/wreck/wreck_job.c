/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "wreck_job.h"

typedef char hashkey_t[16];

static char *idkey (hashkey_t key, int64_t id)
{
    size_t keysz = sizeof (hashkey_t);
    int n = snprintf (key, keysz, "%lld", (long long)id);
    assert (n < keysz);
    return key;
}

void wreck_job_destroy (struct wreck_job *job)
{
    if (job) {
        int saved_errno = errno;
        if (job->aux_destroy)
            job->aux_destroy (job->aux);
        free (job->kvs_path);
        free (job);
        errno = saved_errno;
    }
}

struct wreck_job *wreck_job_create (void)
{
    struct wreck_job *job;
    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    return job;
}

void wreck_job_set_state (struct wreck_job *job, const char *status)
{
    assert (strlen (status) < sizeof (job->state));
    strcpy (job->state, status);
}

const char *wreck_job_get_state (struct wreck_job *job)
{
    return job->state;
}

int wreck_job_insert (struct wreck_job *job, zhash_t *hash)
{
    hashkey_t key;
    if (zhash_lookup (hash, idkey (key, job->id)) != NULL) {
        errno = EEXIST;
        return -1;
    }
    zhash_update (hash, key, job);
    zhash_freefn (hash, key, (zhash_free_fn *)wreck_job_destroy);
    return 0;
}

struct wreck_job *wreck_job_lookup (int64_t id, zhash_t *hash)
{
    hashkey_t key;
    struct wreck_job *job;

    if (!(job = zhash_lookup (hash, idkey (key, id)))) {
        errno = ENOENT;
        return NULL;
    }
    return job;
}

void wreck_job_delete (int64_t id, zhash_t *hash)
{
    hashkey_t key;
    zhash_delete (hash, idkey (key, id));
}

void wreck_job_set_aux (struct wreck_job *job, void *item, flux_free_f destroy)
{
    if (job->aux_destroy)
        job->aux_destroy (job->aux);
    job->aux = item;
    job->aux_destroy = destroy;
}

void *wreck_job_get_aux (struct wreck_job *job)
{
    return job->aux;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
