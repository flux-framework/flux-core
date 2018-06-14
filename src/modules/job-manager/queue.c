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

/* queue - maintain job queue
 *
 * The queue is kept sorted first by priority, then by submission time.
 * Submission time is appriximated by integer FLUID jobid.
 *
 * The list entries point to jobs stored in a hash, keyed by jobid.
 * Upon insertion into the hash, the job reference count is incremented;
 * upon deletion from the hash, the job reference count is decremented.
 * Invariant: job are either in both the hash and the list, or neither.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <stdlib.h>

#include "src/common/libjob/job.h"
#include "job.h"
#include "queue.h"

struct queue {
    zhashx_t *active_jobs;
    zlistx_t *active_jobs_list;
};

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))


/* Hash numerical jobid in 'key' for 'active_jobs'.
 * N.B. zhashx_hash_fn signature
 */
static size_t job_hasher (const void *key)
{
    const flux_jobid_t *id = key;
    return *id;
}

/* Compare keys for sorting 'active_jobs'.
 * N.B. zhashx_comparator_fn signature
 */
static int job_hash_key_cmp (const void *key1, const void *key2)
{
    const flux_jobid_t *id1 = key1;
    const flux_jobid_t *id2 = key2;

    return NUMCMP (*id1, *id2);
}

/* Destroy job entry in 'active_jobs'.
 * N.B. zhashx_destructor_fn signature.
 */
static void job_destructor (void **item)
{
    job_decref (*(struct job **)item);
    *item = NULL;
}

/* Compare items for sorting 'active_jobs_list'.
 * N.B. zlistx_comparator_fn signature
 */
static int job_list_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

/* zlistx_insert() and zlistx_reorder() take a 'low_value' parameter
 * which indicates which end of the list to search from.
 * false=search begins at tail (lowest priority, youngest)
 * true=search begins at head (highest priority, oldest)
 * Attempt to minimize search distance based on job priority.
 */
static bool search_direction (struct job *job)
{
    if (job->priority > FLUX_JOB_PRIORITY_DEFAULT)
        return true;
    else
        return false;
}

/* Insert 'job' into active_jobs hash and active_jobs_list (pri, id order).
 */
int queue_insert (struct queue *queue, struct job *job)
{
    if (zhashx_insert (queue->active_jobs, &job->id, job) < 0) {
        errno = EEXIST;
        return -1;
    }
    job_incref (job);
    if (!(job->list_handle = zlistx_insert (queue->active_jobs_list,
                                            job, search_direction (job)))) {
        zhashx_delete (queue->active_jobs, &job->id); // implicit job_decref
        errno = ENOMEM; // presumed
        return -1;
    }
    return 0;
}

void queue_reorder (struct queue *queue, struct job *job)
{
    zlistx_reorder (queue->active_jobs_list, job->list_handle,
                    search_direction (job));
}

struct job *queue_lookup_by_id  (struct queue *queue, flux_jobid_t id)
{
    struct job *job;

    if (!(job = zhashx_lookup (queue->active_jobs, &id))) {
        errno = ENOENT;
        return NULL;
    }
    return job;
}

void queue_delete (struct queue *queue, struct job *job)
{
    if (job->list_handle)
        (void)zlistx_delete (queue->active_jobs_list, job->list_handle);
    zhashx_delete (queue->active_jobs, &job->id); // implicit job_decref
}

int queue_size (struct queue *queue)
{
    return zlistx_size (queue->active_jobs_list);
}

struct job *queue_first (struct queue *queue)
{
    return zlistx_first (queue->active_jobs_list); // deletion-safe
}

struct job *queue_next (struct queue *queue)
{
    return zlistx_next (queue->active_jobs_list); // deletion-safe
}

void queue_destroy (struct queue *queue)
{
    if (queue) {
        int saved_errno = errno;
        if (queue->active_jobs_list)
            zlistx_destroy (&queue->active_jobs_list);
        if (queue->active_jobs)
            zhashx_destroy (&queue->active_jobs);
        free (queue);
        errno = saved_errno;
    }
}

struct queue *queue_create (void)
{
    struct queue *queue;

    if (!(queue = calloc (1, sizeof (*queue))))
        return NULL;
    if (!(queue->active_jobs = zhashx_new ()))
        goto error;
    if (!(queue->active_jobs_list = zlistx_new ()))
        goto error;
    zhashx_set_key_hasher (queue->active_jobs, job_hasher);
    zhashx_set_key_comparator (queue->active_jobs, job_hash_key_cmp);
    zhashx_set_key_duplicator (queue->active_jobs, NULL);
    zhashx_set_key_destructor (queue->active_jobs, NULL);
    zhashx_set_destructor (queue->active_jobs, job_destructor);
    zlistx_set_comparator (queue->active_jobs_list, job_list_cmp);
    return queue;
error:
    queue_destroy (queue);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

