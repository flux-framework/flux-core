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
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <jansson.h>
#include <argz.h>

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
    if (job && status != NULL && strlen (status) < sizeof (job->state)) {
        strcpy (job->state, status);
        clock_gettime (CLOCK_MONOTONIC, &job->mtime);
    }
}

const char *wreck_job_get_state (struct wreck_job *job)
{
    return job ? job->state : "";
}

int wreck_job_insert (struct wreck_job *job, zhash_t *hash)
{
    hashkey_t key;
    if (!job || !hash) {
        errno = EINVAL;
        return -1;
    }
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

    if (id <= 0 || !hash) {
        errno = EINVAL;
        return NULL;
    }
    if (!(job = zhash_lookup (hash, idkey (key, id)))) {
        errno = ENOENT;
        return NULL;
    }
    return job;
}

void wreck_job_delete (int64_t id, zhash_t *hash)
{
    hashkey_t key;
    if (id > 0 && hash != NULL)
        zhash_delete (hash, idkey (key, id));
}

void wreck_job_set_aux (struct wreck_job *job, void *item, flux_free_f destroy)
{
    if (job != NULL) {
        if (job->aux_destroy)
            job->aux_destroy (job->aux);
        job->aux = item;
        job->aux_destroy = destroy;
    }
}

void *wreck_job_get_aux (struct wreck_job *job)
{
    return job ? job->aux : NULL;
}

/* Sort jobs in reverse mtime order.
 * This has the zlist_compare_fn footprint and can be used with zlist_sort().
 */
static int compare_job_mtime (struct wreck_job *j1, struct wreck_job *j2)
{
    if (j1->mtime.tv_sec < j2->mtime.tv_sec)
        return 1;
    if (j1->mtime.tv_sec > j2->mtime.tv_sec)
        return -1;
    if (j1->mtime.tv_nsec < j2->mtime.tv_nsec)
        return 1;
    if (j1->mtime.tv_nsec > j2->mtime.tv_nsec)
        return -1;
    return 0;
}

/* If 'key' is one of the strings in an argz vector, return true.
 */
static bool findz (const char *argz, size_t argz_len, const char *key)
{
    const char *entry = NULL;
    if (argz && key) {
        while ((entry = argz_next (argz, argz_len, entry)) != NULL)
            if (!strcmp (key, entry))
                return true;
    }
    return false;
}

/* If 'key' survives black/white list filtering, return true.
 */
static bool bw_test (const char *key, const char *b, size_t b_len,
                                      const char *w, size_t w_len)
{
    if (b && findz (b, b_len, key))
        return false;
    if (w && !findz (w, w_len, key))
        return false;
    return true;
}

char *wreck_job_list (zhash_t *hash, int max, const char *include_states,
                                              const char *exclude_states)
{
    json_t *array = NULL;
    json_t *obj = NULL;
    zlist_t *joblist = NULL;
    char *json_str;
    struct wreck_job *job;
    char *white = NULL;
    size_t white_len = 0;
    char *black = NULL;
    size_t black_len = 0;
    int saved_errno;

    if (!hash) {
        errno = EINVAL;
        goto error;
    }
    if (include_states) {
        if (argz_create_sep (include_states, ',', &white, &white_len) != 0)
            goto nomem;
    }
    if (exclude_states) {
        if (argz_create_sep (exclude_states, ',', &black, &black_len) != 0)
            goto nomem;
    }
    if (!(joblist = zlist_new ()))
        goto nomem;
    if (!(array = json_array ()))
        goto nomem;
    job = zhash_first (hash);
    while (job != NULL) {
        if (bw_test (job->state, black, black_len, white, white_len)) {
            if (zlist_append (joblist, job) < 0)
                goto nomem;
        }
        job = zhash_next (hash);
    }
    zlist_sort (joblist, (zlist_compare_fn *)compare_job_mtime);
    job = zlist_first (joblist);
    while (job != NULL) {
        json_t *entry;
        if (!(entry = json_pack ("{s:I s:s s:s}",
                            "jobid", job->id,
                            "kvs_path", job->kvs_path ? job->kvs_path : "",
                            "state", job->state)))
            goto nomem;
        if (json_array_append_new (array, entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        if (max > 0 && json_array_size (array) == max)
            break;
        job = zlist_next (joblist);
    }
    if (!(obj = json_pack ("{s:O}", "jobs", array)))
        goto nomem;
    if (!(json_str = json_dumps (obj, JSON_COMPACT)))
        goto error;
    zlist_destroy (&joblist);
    json_decref (array);
    json_decref (obj);
    free (black);
    free (white);
    return json_str;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    zlist_destroy (&joblist);
    json_decref (array);
    json_decref (obj);
    free (black);
    free (white);
    errno = saved_errno;
    return NULL;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
