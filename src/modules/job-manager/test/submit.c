/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include "src/modules/job-manager/submit.h"
#include "src/common/libtap/tap.h"

void single_job_check (struct queue *queue)
{
    zlist_t *newjobs;
    json_t *job1;
    json_t *job2;
    struct job *job;

    ok (queue_size (queue) == 0, "queue is initially empty");

    if (!(newjobs = zlist_new ()))
        BAIL_OUT ("zlist_new() failed");

    /* good job */
    if (!(job1 = json_pack ("{s:I s:i s:i s:f s:i}",
                            "id",
                            1,
                            "priority",
                            10,
                            "userid",
                            42,
                            "t_submit",
                            1.0,
                            "flags",
                            0)))
        BAIL_OUT ("json_pack() failed");
    ok (submit_enqueue_one_job (queue, newjobs, job1) == 0,
        "submit_enqueue_one_job works");
    ok (queue_size (queue) == 1, "queue contains one job");
    ok ((job = zlist_head (newjobs)) != NULL, "newjobs contains one job");
    ok (job->id == 1 && job->priority == 10 && job->userid == 42
            && job->t_submit == 1.0 && job->flags == 0,
        "struct job was properly decoded");

    /* malformed job */
    if (!(job2 = json_pack ("{s:I}", "id", 2)))
        BAIL_OUT ("json_pack() failed");
    errno = 0;
    ok (submit_enqueue_one_job (queue, newjobs, job2) < 0 && errno == EPROTO,
        "submit_enqueue_one job o=(malformed) fails with EPROTO");

    /* resubmit orig job */
    ok (submit_enqueue_one_job (queue, newjobs, job1) == 0,
        "submit_enqueue_one_job o=(dup id) works");
    ok (queue_size (queue) == 1, "but queue contains one job");
    ok (zlist_size (newjobs) == 1, "and newjobs still contains one job");

    /* clean up (batch submit error path) */
    submit_enqueue_jobs_cleanup (queue, newjobs);  // destroys newjobs
    ok (queue_size (queue) == 0,
        "submit_enqueue_jobs_cleanup removed orig queue entry");

    json_decref (job2);
    json_decref (job1);
}

void multi_job_check (struct queue *queue)
{
    zlist_t *newjobs;
    json_t *jobs;

    ok (queue_size (queue) == 0, "queue is initially empty");
    if (!(jobs = json_pack ("[{s:I s:i s:i s:f s:i},"
                            "{s:I s:i s:i s:f s:i}]",
                            "id",
                            1,
                            "priority",
                            10,
                            "userid",
                            42,
                            "t_submit",
                            1.0,
                            "flags",
                            0,
                            "id",
                            2,
                            "priority",
                            11,
                            "userid",
                            43,
                            "t_submit",
                            1.1,
                            "flags",
                            1)))
        BAIL_OUT ("json_pack() failed");

    newjobs = submit_enqueue_jobs (queue, jobs);
    ok (newjobs != NULL, "submit_enqueue_jobs works");
    ok (queue_size (queue) == 2, "queue contains 2 jobs");
    ok (zlist_size (newjobs) == 2, "newjobs contains 2 jobs");
    submit_enqueue_jobs_cleanup (queue, newjobs);
    ok (queue_size (queue) == 0,
        "submit_enqueue_jobs_cleanup removed queue entries");

    json_decref (jobs);
}

int main (int argc, char *argv[])
{
    struct queue *queue;

    plan (NO_PLAN);

    if (!(queue = queue_create (true)))
        BAIL_OUT ("queue_create() failed");

    single_job_check (queue);
    multi_job_check (queue);

    queue_destroy (queue);
    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
