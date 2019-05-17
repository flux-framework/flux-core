/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/queue.h"

/* Create job with only params that affect queue order.
 */
struct job *job_create_test (flux_jobid_t id, int priority)
{
    struct job *j;
    if (!(j = job_create ()))
        BAIL_OUT ("job_create failed");
    j->id = id;
    j->priority = priority;
    return j;
}

int main (int argc, char *argv[])
{
    struct queue *q;
    struct job *job[3];
    struct job *njob[2];
    struct job *j, *j_prev;

    plan (NO_PLAN);

    q = queue_create (true);
    if (!q)
        BAIL_OUT ("could not create queue");
    ok (queue_size (q) == 0, "queue_size returns 0");

    /* insert 1,2,3 */

    job[0] = job_create_test (1, FLUX_JOB_PRIORITY_DEFAULT);
    job[1] = job_create_test (2, FLUX_JOB_PRIORITY_DEFAULT);
    job[2] = job_create_test (3, FLUX_JOB_PRIORITY_DEFAULT);
    ok (queue_insert (q, job[0], &job[0]->queue_handle) == 0,
        "queue_insert 1 pri=def");
    ok (queue_insert (q, job[1], &job[1]->queue_handle) == 0,
        "queue_insert 2 pri=def");
    ok (queue_insert (q, job[2], &job[2]->queue_handle) == 0,
        "queue_insert 3 pri=def");

    errno = 0;
    ok (queue_insert (q, job[2], &job[2]->queue_handle) < 0 && errno == EEXIST,
        "queue_insert 3 again fails with EEXIST");

    /* queue size, refcounts */

    ok (queue_size (q) == 3, "queue_size returns 3");
    ok (job[0]->refcount == 2 && job[1]->refcount == 2 && job[2]->refcount == 2,
        "queue took reference on inserted jobs");

    /* iterators */

    ok (queue_first (q) == job[0] && queue_next (q) == job[1]
            && queue_next (q) == job[2] && queue_next (q) == NULL,
        "queue iterators return job 1,2,3,NULL");

    /* lookup_by_id */

    ok (queue_lookup_by_id (q, 1) == job[0]
            && queue_lookup_by_id (q, 2) == job[1]
            && queue_lookup_by_id (q, 3) == job[2],
        "queue_lookup_by_id works for all three jobs");
    errno = 0;
    ok (queue_lookup_by_id (q, 42) == NULL && errno == ENOENT,
        "queue_lookupby_id 42 fails with ENOENT");

    /* insert high priority */

    njob[0] = job_create_test (100, FLUX_JOB_PRIORITY_MAX);
    ok (queue_insert (q, njob[0], &njob[0]->queue_handle) == 0,
        "queue_insert 100 pri=max");
    ok (queue_first (q) == njob[0], "queue_first returns high priority job");

    /* insert low priority */

    njob[1] = job_create_test (101, FLUX_JOB_PRIORITY_MIN);
    ok (queue_insert (q, njob[1], &njob[1]->queue_handle) == 0,
        "queue_insert 101 pri=min");

    j_prev = NULL;
    j = queue_first (q);
    while (j) {
        j_prev = j;
        j = queue_next (q);
    }
    ok (j_prev == njob[1], "iterators find low priority job last");

    /* set high priority and reorder
     *   review: queue contains 100,1,2,3,101
     */
    job[2]->priority = FLUX_JOB_PRIORITY_MAX;  // job 3
    queue_reorder (q, job[2], job[2]->queue_handle);
    ok (queue_first (q) == job[2],
        "reorder job 3 pri=max moves that job first");

    /* queue_delete */

    queue_delete (q, njob[0], njob[0]->queue_handle);
    queue_delete (q, njob[1], njob[1]->queue_handle);

    ok (njob[0]->refcount == 1 && njob[1]->refcount == 1,
        "queue_delete dropped reference on jobs");

    errno = 0;
    ok (queue_lookup_by_id (q, 100) == NULL && errno == ENOENT,
        "queue_lookup_by_id on deleted job fails with ENOENT");

    /* destroy */

    queue_destroy (q);
    ok (job[0]->refcount == 1 && job[1]->refcount == 1 && job[2]->refcount == 1,
        "queue dropped reference on jobs at destruction");

    job_decref (job[0]);
    job_decref (job[1]);
    job_decref (job[2]);

    job_decref (njob[0]);
    job_decref (njob[1]);

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
