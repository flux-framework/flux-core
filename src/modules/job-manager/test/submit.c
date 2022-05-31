/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/submit.h"

void single_job_check (zhashx_t *active_jobs)
{
    zlistx_t *newjobs;
    zlistx_t *newjobs_saved;
    json_t *job1;
    json_t *job2;
    struct job *job;

    ok (zhashx_size (active_jobs) == 0,
        "hash is initially empty");

    /* good job */
    if (!(job1 = json_pack ("[{s:I s:i s:i s:f s:i s:{}}]",
                            "id", 1LL,
                            "urgency", 10,
                            "userid", 42,
                            "t_submit", 1.0,
                            "flags", 0,
                            "jobspec")))
        BAIL_OUT ("json_pack() failed");

    newjobs = submit_jobs_to_list (job1);
    ok (newjobs != NULL,
        "submit_jobs_to_list works");
    ok (zlistx_size (newjobs) == 1,
        "newjobs contains one job");
    if (!(job = zlistx_first (newjobs)))
        BAIL_OUT ("submit_jobs_to_list failed");

    ok (job->id == 1 && job->urgency == 10 && job->userid == 42
        && job->t_submit == 1.0 && job->flags == 0,
        "struct job was properly decoded");

    ok (submit_hash_jobs (active_jobs, newjobs) == 0,
        "submit_hash_jobs works");
    ok (zhashx_size (active_jobs) == 1,
        "hash contains one job");

    /* malformed job */
    if (!(job2 = json_pack ("[{s:I}]", "id", 2)))
        BAIL_OUT ("json_pack() failed");

    ok (submit_jobs_to_list (job2) == NULL && errno == EPROTO,
        "submit_jobs_to_list fails with EPROTO on invalid job");

    zlistx_set_duplicator (newjobs, job_duplicator);
    newjobs_saved = zlistx_dup (newjobs);

    /* resubmit orig job */
    errno = 0;
    ok (submit_hash_jobs (active_jobs, newjobs) < 0 && errno == EEXIST,
        "submit_hash_jobs with duplicate fails");
    ok (zhashx_size (active_jobs) == 1,
        "the hash still contains one job");

    /* clean up (batch submit error path) */
    submit_add_jobs_cleanup (active_jobs, newjobs_saved); // destroys newjobs
    ok (zhashx_size (active_jobs) == 0,
        "submit_add_jobs_cleanup removed orig hash entry");

    zlistx_destroy (&newjobs);
    json_decref (job2);
    json_decref (job1);
}

void multi_job_check (zhashx_t *active_jobs)
{

    zlistx_t *newjobs;
    json_t *jobs;

    ok (zhashx_size (active_jobs) == 0,
        "hash is initially empty");
    if (!(jobs = json_pack ("[{s:I s:i s:i s:f s:i s:{}},"
                             "{s:I s:i s:i s:f s:i s:{}}]",
                            "id", 1LL,
                            "urgency", 10,
                            "userid", 42,
                            "t_submit", 1.0,
                            "flags", 0,
                            "jobspec",
                            "id", 2LL,
                            "urgency", 11,
                            "userid", 43,
                            "t_submit", 1.1,
                            "flags", 1,
                            "jobspec")))
        BAIL_OUT ("json_pack() failed");

    newjobs = submit_jobs_to_list (jobs);
    ok (newjobs != NULL,
        "submit_jobs_to_list works");
    ok (zlistx_size (newjobs) == 2,
        "submit_jobs_to_list returned correct number of jobs");

    ok (submit_hash_jobs (active_jobs, newjobs) == 0,
        "submit_hash_jobs works");
    ok (zhashx_size (active_jobs) == 2,
        "hash contains 2 jobs");
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
