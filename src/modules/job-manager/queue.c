/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queue - list of jobs sorted by priority, then t_submit order
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <stdlib.h>
#include <stdbool.h>

#include "src/common/libjob/job.h"
#include "job.h"
#include "queue.h"

struct queue {
    zhashx_t *h;
    zlistx_t *l;
    queue_notify_f empty_cb;
    void *empty_arg;
};

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))


/* Hash numerical jobid in 'key'.
 * N.B. zhashx_hash_fn signature
 */
static size_t job_hasher (const void *key)
{
    const flux_jobid_t *id = key;
    return *id;
}

/* Compare hash keys.
 * N.B. zhashx_comparator_fn signature
 */
static int job_hash_key_cmp (const void *key1, const void *key2)
{
    const flux_jobid_t *id1 = key1;
    const flux_jobid_t *id2 = key2;

    return NUMCMP (*id1, *id2);
}

/* Destroy list entry.
 * N.B. zlistx_destructor_fn signature.
 */
static void job_destructor (void **item)
{
    job_decref (*(struct job **)item);
    *item = NULL;
}

/* Duplicate list entry
 * N.B. zlistx_duplicator_fn signature.
 */
static void *job_duplicator (const void *item)
{
    return job_incref ((void *)item);
}

/* Compare items for sorting in list.
 * N.B. zlistx_comparator_fn signature
 */
static int job_list_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->t_submit, j2->t_submit);
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

/* Insert 'job' into queue.
 * If hash is defined, insert into hash also.
 */
int queue_insert (struct queue *queue, struct job *job, void **handle)
{
    void *tmp;

    if (queue->h) {
        if (zhashx_insert (queue->h, &job->id, job) < 0) {
            errno = EEXIST;
            return -1;
        }
    }
    if (!(tmp = zlistx_insert (queue->l, job, search_direction (job)))) {
        if (queue->h)
            zhashx_delete (queue->h, &job->id);
        errno = ENOMEM; // presumed
        return -1;
    }
    *handle = tmp;
    return 0;
}

void queue_reorder (struct queue *queue, struct job *job, void *handle)
{
    zlistx_reorder (queue->l, handle, search_direction (job));
}

struct job *queue_lookup_by_id  (struct queue *queue, flux_jobid_t id)
{
    struct job *job;

    if (queue->h) {
        if ((job = zhashx_lookup (queue->h, &id)))
            return job;
    }
    else {
        job = zlistx_first (queue->l);
        while (job) {
            if (job->id == id)
                return job;
            job = zlistx_next (queue->l);
        }
    }
    errno = ENOENT;
    return NULL;
}

void queue_delete (struct queue *queue, struct job *job, void *handle)
{
    int rc;

    if (queue->h)
        zhashx_delete (queue->h, &job->id);
    rc = zlistx_delete (queue->l, handle); // calls job_decref ()
    assert (rc == 0);
    if (queue->empty_cb && zlistx_size (queue->l) == 0)
        queue->empty_cb (queue, queue->empty_arg);
}

int queue_size (struct queue *queue)
{
    return zlistx_size (queue->l);
}

void queue_set_notify_empty (struct queue *queue,
                             queue_notify_f cb, void *arg)
{
    queue->empty_cb = cb;
    queue->empty_arg = arg;
    if (queue->empty_cb && zlistx_size (queue->l) == 0)
        queue->empty_cb (queue, queue->empty_arg);
}

struct job *queue_first (struct queue *queue)
{
    return zlistx_first (queue->l); // deletion-safe
}

struct job *queue_next (struct queue *queue)
{
    return zlistx_next (queue->l); // deletion-safe
}

void queue_destroy (struct queue *queue)
{
    if (queue) {
        int saved_errno = errno;
        zlistx_destroy (&queue->l);
        zhashx_destroy (&queue->h);
        free (queue);
        errno = saved_errno;
    }
}

struct queue *queue_create (bool lookup_hash)
{
    struct queue *queue;

    if (!(queue = calloc (1, sizeof (*queue))))
        return NULL;
    if (!(queue->l = zlistx_new ()))
        goto error;
    zlistx_set_destructor (queue->l, job_destructor);
    zlistx_set_comparator (queue->l, job_list_cmp);
    zlistx_set_duplicator (queue->l, job_duplicator);
    if (lookup_hash) {
        if (!(queue->h = zhashx_new ()))
            goto error;
        zhashx_set_key_hasher (queue->h, job_hasher);
        zhashx_set_key_comparator (queue->h, job_hash_key_cmp);
        zhashx_set_key_duplicator (queue->h, NULL);
        zhashx_set_key_destructor (queue->h, NULL);
    }
    return queue;
error:
    queue_destroy (queue);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

