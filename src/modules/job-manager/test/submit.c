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
#include "src/common/libjob/job_hash.h"
#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/submit.h"

void single_job_check (zhashx_t *active_jobs)
{
    zlist_t *newjobs;
    json_t *job1;
    json_t *job2;
    struct job *job;

    ok (zhashx_size (active_jobs) == 0,
        "hash is initially empty");

    if (!(newjobs = zlist_new ()))
        BAIL_OUT ("zlist_new() failed");

    /* good job */
    if (!(job1 = json_pack ("{s:I s:i s:i s:f s:i s:{}}",
                            "id", 1,
                            "urgency", 10,
                            "userid", 42,
                            "t_submit", 1.0,
                            "flags", 0,
                            "jobspec")))
        BAIL_OUT ("json_pack() failed");
    ok (submit_add_one_job (active_jobs, newjobs, job1) == 0,
        "submit_add_one_job works");
    ok (zhashx_size (active_jobs) == 1,
        "hash contains one job");
    ok ((job = zlist_head (newjobs)) != NULL,
        "newjobs contains one job");
    ok (job->id == 1 && job->urgency == 10 && job->userid == 42
        && job->t_submit == 1.0 && job->flags == 0,
        "struct job was properly decoded");

    /* malformed job */
    if (!(job2 = json_pack ("{s:I}", "id", 2)))
        BAIL_OUT ("json_pack() failed");
    errno = 0;
    ok (submit_add_one_job (active_jobs, newjobs, job2) < 0 && errno == EPROTO,
        "submit_add_one job o=(malformed) fails with EPROTO");

    /* resubmit orig job */
    ok (submit_add_one_job (active_jobs, newjobs, job1) == 0,
        "submit_add_one_job o=(dup id) works");
    ok (zhashx_size (active_jobs) == 1,
        "but hash contains one job");
    ok (zlist_size (newjobs) == 1,
        "and newjobs still contains one job");

    /* clean up (batch submit error path) */
    submit_add_jobs_cleanup (active_jobs, newjobs); // destroys newjobs
    ok (zhashx_size (active_jobs) == 0,
        "submit_add_jobs_cleanup removed orig hash entry");

    json_decref (job2);
    json_decref (job1);
}

void multi_job_check (zhashx_t *active_jobs)
{

    zlist_t *newjobs;
    json_t *jobs;

    ok (zhashx_size (active_jobs) == 0,
        "hash is initially empty");
    if (!(jobs = json_pack ("[{s:I s:i s:i s:f s:i s:{}},"
                             "{s:I s:i s:i s:f s:i s:{}}]",
                            "id", 1,
                            "urgency", 10,
                            "userid", 42,
                            "t_submit", 1.0,
                            "flags", 0,
                            "jobspec",
                            "id", 2,
                            "urgency", 11,
                            "userid", 43,
                            "t_submit", 1.1,
                            "flags", 1,
                            "jobspec")))
        BAIL_OUT ("json_pack() failed");

    newjobs = submit_add_jobs (active_jobs, jobs);
    ok (newjobs != NULL,
        "submit_add_jobs works");
    ok (zhashx_size (active_jobs) == 2,
        "hash contains 2 jobs");
    ok (zlist_size (newjobs) == 2,
        "newjobs contains 2 jobs");
    submit_add_jobs_cleanup (active_jobs, newjobs);
    ok (zhashx_size (active_jobs) == 0,
        "submit_add_jobs_cleanup removed hash entries");

    json_decref (jobs);
}

int main (int argc, char *argv[])
{
    zhashx_t *active_jobs;

    plan (NO_PLAN);

    if (!(active_jobs = job_hash_create ()))
        BAIL_OUT ("job_hash_create() failed");

    single_job_check (active_jobs);
    multi_job_check (active_jobs);

    zhashx_destroy (&active_jobs);
    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
