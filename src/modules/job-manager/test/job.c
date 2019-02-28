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

#include "src/common/libtap/tap.h"
#include "src/modules/job-manager/job.h"

void test_create (void)
{
    struct job *job;

    job = job_create (42, 1, FLUX_USERID_UNKNOWN, 2, 3);
    if (job == NULL)
        BAIL_OUT ("job_create failed");
    ok (job->refcount == 1,
        "job_create set refcount to 1");
    ok (job->id == 42 && job->priority == 1 && job->state == FLUX_JOB_NEW
        && job->userid == FLUX_USERID_UNKNOWN && job->t_submit == 2,
        "job_create set id, priority, userid, and t_submit to expected values");
    ok (job->flags == 3,
        "job_create set submit flags to expected value");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create set no internal flags");
    ok (job->aux_queue_handle == NULL && job->queue_handle == NULL,
        "job_Create set queue handles to NULL");
    ok (job_incref (job) == job && job->refcount == 2,
        "job_incref incremented refcount and returned original job pointer");
    job_decref (job);
    ok (job->refcount == 1,
        "job_decref decremented refcount");
    errno = 42;
    job_decref (job);
    ok (errno == 42,
        "job_decref doesn't clobber errno");

    lives_ok({job_incref (NULL);},
        "job_incref on NULL pointer doesn't crash");
    lives_ok({job_decref (NULL);},
        "job_decref on NULL pointer doesn't crash");
}

const char *test_input[] = {
    /* 0 */
    "42.2 submit userid=66 priority=16 flags=42\n",

    /* 1 */
    "42.2 submit userid=66 priority=16 flags=42\n"
    "42.3 priority userid=42 priority=1\n",

    /* 2 */
    "42.2 submit userid=66 priority=16 flags=42\n"
    "42.3 exception type=cancel severity=0 userid=42 free form notes...\n",

    /* 3 */
    "42.2 submit userid=66 priority=16 flags=42\n"
    "42.3 exception type=meep severity=1 userid=42 this one is non-fatal\n",

    /* 4 */
    "42.2 submit userid=66 priority=16 flags=42\n"
    "42.3 alloc\n",

    /* 5 */
    "42.3 alloc\n",

    /* 6 */
    "42.2 submit userid=66 priority=16 flags=42\n"
    "42.3 alloc\n"
    "42.4 free\n",
};

void test_create_from_eventlog (void)
{
    struct job *job;

    /* 0 - submit only */
    job = job_create_from_eventlog (2, test_input[0]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit) failed");
    ok (job->refcount == 1,
        "job_create_from_eventlog log=(submit) set refcount to 1");
    ok (job->id == 2,
        "job_create_from_eventlog log=(submit) set id from param");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit)  set no internal flags");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit) set userid from submit");
    ok (job->flags == 42,
        "job_create_from_eventlog log=(submit) set flags from submit");
    ok (job->priority == 16,
        "job_create_from_eventlog log=(submit) set priority from submit");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit) set t_submit from submit");
    ok (job->state == FLUX_JOB_SCHED,
        "job_create_from_eventlog log=(submit) set state=SCHED");
    job_decref (job);

    /* 1 - submit + priority */
    job = job_create_from_eventlog (3, test_input[1]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+pri) failed");
    ok (job->id == 3,
        "job_create_from_eventlog log=(submit+pri) set id from param");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit+pri) set userid from submit");
    ok (job->priority == 1,
        "job_create_from_eventlog log=(submit+pri) set priority from priority");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit+pri) set t_submit from submit");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit+pri) set no internal flags");
    ok (job->state == FLUX_JOB_SCHED,
        "job_create_from_eventlog log=(submit+pri) set state=SCHED");
    job_decref (job);

    /* 2 - submit + exception severity 0 */
    job = job_create_from_eventlog (3, test_input[2]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex0) failed");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit+ex0) set userid from submit");
    ok (job->priority == 16,
        "job_create_from_eventlog log=(submit+ex0) set priority from submit");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit+ex0) set t_submit from submit");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit+ex0) set no internal flags");
    ok (job->state == FLUX_JOB_CLEANUP,
        "job_create_from_eventlog log=(submit+ex0) set state=CLEANUP");
    job_decref (job);

    /* 3 - submit + exception severity 1 */
    job = job_create_from_eventlog (3, test_input[3]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex1) failed");
    ok (job->state == FLUX_JOB_SCHED,
        "job_create_from_eventlog log=(submit+ex1) set state=SCHED");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit+ex1) set no internal flags");
    job_decref (job);

    /* 4 - submit + alloc */
    job = job_create_from_eventlog (3, test_input[4]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+alloc) failed");
    ok (!job->alloc_pending
        && !job->free_pending
        && job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit+alloc) set has_resources flag");
    ok (job->state == FLUX_JOB_RUN,
        "job_create_from_eventlog log=(submit+alloc) set state=RUN");
    job_decref (job);

    /* 5 - missing submit */
    errno = 0;
    job = job_create_from_eventlog (3, test_input[5]);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(alloc) fails with EINVAL");

    /* 6 - submit + alloc + free */
    job = job_create_from_eventlog (3, test_input[6]);
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+alloc+free) failed");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources
        && !job->exception_pending,
        "job_create_from_eventlog log=(submit+alloc+free) set no internal flags");
    ok (job->state == FLUX_JOB_CLEANUP,
        "job_create_from_eventlog log=(submit+alloc+free) set state=CLEANUP");
    job_decref (job);

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create ();
    test_create_from_eventlog ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
