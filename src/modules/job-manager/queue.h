/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_QUEUE_H
#    define _FLUX_JOB_MANAGER_QUEUE_H

#    include <stdbool.h>
#    include "src/common/libjob/job.h"
#    include "job.h"

struct queue;

typedef void (*queue_notify_f) (struct queue *queue, void *arg);

/* Create a job queue, sorted by priority, then t_submit.
 * If lookup_hash=true, create a hash to speed up queue_lookup_by_id().
 */
struct queue *queue_create (bool lookup_hash);
void queue_destroy (struct queue *queue);

/* Insert job into queue.  The queue takes a reference on job,
 * so the caller retains its reference.
 * Returns 0 on success, -1 on failure with errno set.
 */
int queue_insert (struct queue *queue, struct job *job, void **handle);

/* Find new position in queue for job (e.g. after priority change).
 * Returns 0 on success, -1 on failure with errno set.
 */
void queue_reorder (struct queue *queue, struct job *job, void *handle);

/* Find a job by jobid.
 * Returns job on success, NULL on failure with errno set.
 */
struct job *queue_lookup_by_id (struct queue *queue, flux_jobid_t id);

/* Delete queue entry associated with 'job' (dropping its reference on job).
 */
void queue_delete (struct queue *queue, struct job *job, void *handle);

/* deletion-safe iterator
 * Returns first/next job, or NULL on empty list or at end of list.
 */
struct job *queue_first (struct queue *queue);
struct job *queue_next (struct queue *queue);

/* Return the number of jobs in the queue
 */
int queue_size (struct queue *queue);

/* Arrange to be notified when queue size decreases to zero.
 */
void queue_set_notify_empty (struct queue *queue, queue_notify_f cb, void *arg);

#endif /* _FLUX_JOB_MANAGER_QUEUE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
