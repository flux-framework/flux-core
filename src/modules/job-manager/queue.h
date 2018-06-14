#ifndef _FLUX_JOB_MANAGER_QUEUE_H
#define _FLUX_JOB_MANAGER_QUEUE_H

#include "src/common/libjob/job.h"
#include "job.h"

struct queue *queue_create (void);
void queue_destroy (struct queue *queue);

/* Insert job into queue.  The queue takes a reference on job,
 * so the caller retains its reference.
 * Returns 0 on success, -1 on failure with errno set.
 */
int queue_insert (struct queue *queue, struct job *job);

/* Find new position in queue for job (e.g. after priority change).
 * Returns 0 on success, -1 on failure with errno set.
 */
void queue_reorder (struct queue *queue, struct job *job);

/* Find a job by jobid.
 * Returns job on success, NULL on failure wtih errno set.
 */
struct job *queue_lookup_by_id (struct queue *queue, flux_jobid_t id);

/* Delete queue entry associated with 'job' (dropping its refence on job).
 */
void queue_delete (struct queue *queue, struct job *job);

/* deletion-safe iterator
 * Returns first/next job, or NULL on empty list or at end of list.
 */
struct job *queue_first (struct queue *queue);
struct job *queue_next (struct queue *queue);

/* Return the number of jobs in the queue
 */
int queue_size (struct queue *queue);

#endif /* _FLUX_JOB_MANAGER_QUEUE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

