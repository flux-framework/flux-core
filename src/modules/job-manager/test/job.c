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

    job = job_create ();
    if (job == NULL)
        BAIL_OUT ("job_create failed");
    ok (job->refcount == 1,
        "job_create set refcount to 1");
    ok (job->id == 0
        && job->urgency == FLUX_JOB_URGENCY_DEFAULT
        && job->state == FLUX_JOB_STATE_NEW
        && job->userid == FLUX_USERID_UNKNOWN
        && job->t_submit == 0
        && job->flags == 0,
        "job_create set id, urgency, userid, and t_submit to expected values");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create set no internal flags");
    ok (job->handle == NULL,
        "job_create set queue handle to NULL");
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
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n",

    /* 1 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"urgency\","
     "\"context\":{\"userid\":42,\"urgency\":1}}\n",

    /* 2 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":1}}\n",

    /* 3 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"exception\","
     "\"context\":{\"type\":\"cancel\",\"severity\":0,\"userid\":42}}\n",

    /* 4 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"exception\","
     "\"context\":{\"type\":\"meep\",\"severity\":1,\"userid\":42}}\n",

    /* 5 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":100}}\n"
    "{\"timestamp\":42.5,\"name\":\"alloc\"}\n",

    /* 6 */
    "{\"timestamp\":42.3,\"name\":\"alloc\"}\n",

    /* 7 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":100}}\n"
    "{\"timestamp\":42.4,\"name\":\"alloc\"}\n"
    "{\"timestamp\":42.5,\"name\":\"exception\","
     "\"context\":{\"type\":\"gasp\",\"severity\":0,\"userid\":42}}\n"
    "{\"timestamp\":42.6,\"name\":\"free\"}\n",
};

void test_create_from_eventlog (void)
{
    struct job *job;

    /* 0 - submit only */
    job = job_create_from_eventlog (2, test_input[0], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit) failed");
    ok (job->refcount == 1,
        "job_create_from_eventlog log=(submit) set refcount to 1");
    ok (job->id == 2,
        "job_create_from_eventlog log=(submit) set id from param");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit)  set no internal flags");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit) set userid from submit");
    ok (job->flags == 42,
        "job_create_from_eventlog log=(submit) set flags from submit");
    ok (job->urgency == 16,
        "job_create_from_eventlog log=(submit) set urgency from submit");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit) set t_submit from submit");
    ok (job->state == FLUX_JOB_STATE_DEPEND,
        "job_create_from_eventlog log=(submit) set state=DEPEND");
    job_decref (job);

    /* 1 - submit + urgency */
    job = job_create_from_eventlog (3, test_input[1], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+urgency) failed");
    ok (job->id == 3,
        "job_create_from_eventlog log=(submit+urgency) set id from param");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit+urgency) set userid from submit");
    ok (job->urgency == 1,
        "job_create_from_eventlog log=(submit+urgency) set urgency from urgency");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit+urgency) set t_submit from submit");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+urgency) set no internal flags");
    ok (job->state == FLUX_JOB_STATE_DEPEND,
        "job_create_from_eventlog log=(submit+urgency) set state=DEPEND");
    job_decref (job);

    /* 2 - submit + depend + priority */
    job = job_create_from_eventlog (3, test_input[2], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority) failed");
    ok (job->id == 3,
        "job_create_from_eventlog log=(submit+depend+priority) set id from param");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit+depend+priority) set userid from submit");
    ok (job->urgency == 16,
        "job_create_from_eventlog log=(submit+depend+priority) set urgency from submit");
    ok (job->priority == 1,
        "job_create_from_eventlog log=(submit+depend+priority) set priority from priority");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit+depend+priority) set t_submit from submit");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+depend+priority) set no internal flags");
    ok (job->state == FLUX_JOB_STATE_SCHED,
        "job_create_from_eventlog log=(submit+depend+priority) set state=SCHED");
    job_decref (job);

    /* 3 - submit + exception severity 0 */
    job = job_create_from_eventlog (3, test_input[3], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex0) failed");
    ok (job->userid == 66,
        "job_create_from_eventlog log=(submit+ex0) set userid from submit");
    ok (job->urgency == 16,
        "job_create_from_eventlog log=(submit+ex0) set urgency from submit");
    ok (job->t_submit == 42.2,
        "job_create_from_eventlog log=(submit+ex0) set t_submit from submit");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+ex0) set no internal flags");
    ok (job->state == FLUX_JOB_STATE_CLEANUP,
        "job_create_from_eventlog log=(submit+ex0) set state=CLEANUP");
    job_decref (job);

    /* 4 - submit + exception severity 1 */
    job = job_create_from_eventlog (3, test_input[4], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex1) failed");
    ok (job->state == FLUX_JOB_STATE_DEPEND,
        "job_create_from_eventlog log=(submit+ex1) set state=DEPEND");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+ex1) set no internal flags");
    job_decref (job);

    /* 5 - submit + depend + priority + alloc */
    job = job_create_from_eventlog (3, test_input[5], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority+alloc) failed");
    ok (!job->alloc_pending
        && !job->free_pending
        && job->has_resources,
        "job_create_from_eventlog log=(submit+depend+priority+alloc) set has_resources flag");
    ok (job->state == FLUX_JOB_STATE_RUN,
        "job_create_from_eventlog log=(submit+depend+priority+alloc) set state=RUN");
    job_decref (job);

    /* 6 - missing submit */
    errno = 0;
    job = job_create_from_eventlog (3, test_input[6], "{}");
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(alloc) fails with EINVAL");

    /* 7 - submit + depend + priority + alloc + ex0 + free */
    job = job_create_from_eventlog (3, test_input[7], "{}");
    if (job == NULL)
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) failed");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) set no internal flags");
    ok (job->state == FLUX_JOB_STATE_CLEANUP,
        "job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) set state=CLEANUP");
    job_decref (job);

}

void test_create_from_json (void)
{
    json_t *o;
    struct job *job;

    errno = 0;
    ok ((job = job_create_from_json (json_null ())) == NULL && errno == EPROTO,
        "job_create_from_json on malformed object fails with EPROTO");

    if (!(o = json_pack ("{s:I s:i s:i s:f s:i s:{}}",
                         "id", 1LL,
                         "urgency", 10,
                         "userid", 42,
                         "t_submit", 1.0,
                         "flags", 0,
                         "jobspec")))
        BAIL_OUT ("json_pack failed");
    ok ((job = job_create_from_json (o)) != NULL,
        "job_create_from_json works");
    ok (job->id == 1
        && job->urgency == 10
        && job->userid == 42
        && job->t_submit == 1.0
        && job->flags == 0,
        "job json object was properly decoded");
    json_decref (o);
    job_decref (job);
}

static void test_subscribe (void)
{
    flux_plugin_t *p = flux_plugin_create ();
    flux_plugin_t *p2 = flux_plugin_create ();
    struct job * job = job_create ();
    struct job * job2 = job_create ();
    if (!job || !job2 || !p || !p2)
        BAIL_OUT ("failed to create jobs and/or plugins");

    ok (job->subscribers == NULL && job2->subscribers == NULL,
        "job->subscribers is NULL with no subscribers");
    ok (job_events_subscribe (job, p) == 0,
        "job_events_subscribe works");
    ok (job->subscribers && zlistx_size (job->subscribers) == 1,
        "job now has one subscription");
    ok (zlistx_head (job->subscribers) == p,
        "plugin is first subscriber on list");

    ok (job_events_subscribe (job, p2) == 0,
        "2nd job_events_subscribe works");
    ok (job->subscribers && zlistx_size (job->subscribers) == 2,
        "job now has two subscribers");

    ok (job_events_subscribe (job2, p2) == 0,
        "subscribe plugin 2 to a second job");
    ok (job2->subscribers && zlistx_size (job2->subscribers) == 1,
        "job2 now has one subscriber");
    ok (zlistx_head (job2->subscribers) == p2,
        "plugin 2 is first subscriber on job2 subscriber list");

    flux_plugin_destroy (p);
    pass ("destroy first plugin");

    ok (job->subscribers && zlistx_size (job->subscribers) == 1,
        "after plugin destruction, job has 1 subscriber");
    ok (zlistx_head (job->subscribers) == p2,
        "plugin 2 is now first subscriber on list");

    /*  Now destroy job before plugin
     */
    job_decref (job);
    pass ("destroy job before plugin");
    job_decref (job2);
    pass ("destroy job2 before plugin");
    flux_plugin_destroy (p2);
    pass ("destroy 2nd plugin after all jobs");
}

static void test_event_id_cache (void)
{
    struct job *job = job_create ();
    ok (job_event_id_set (job, 1024) < 0 && errno == ENOSPC,
        "job_event_id_set 1024 returns ENOSPC");
    ok (job_event_id_set (job, -1) < 0 && errno == EINVAL,
        "job_event_id_set -1 returns EINVAL");

    ok (job_event_id_test (job, 1024) < 0 && errno == EINVAL,
        "job_event_id_test 1024 returns EINVAL");
    ok (job_event_id_test (job, -1) < 0 && errno == EINVAL,
        "job_event_id_test -1 returns EINVAL");

    ok (job_event_id_test (job, 0) == 0,
        "job_event_id_test 0 returns 0");
    ok (job_event_id_test (job, 63) == 0,
        "job_event_id_test 63 returns 0");

    ok (job_event_id_set (job, 0) == 0,
        "job_event_id_set works");
    ok (job_event_id_test (job, 0) == 1,
        "job_event_id_test 0 now returns 1");
    ok (job_event_id_set (job, 3) == 0,
        "job_event_id_set works");
    ok (job_event_id_test (job, 3) == 1,
        "job_event_id_test 3 now returns 1");
    ok (job_event_id_set (job, 63) == 0,
        "job_event_id_set works");
    ok (job_event_id_test (job, 63) == 1,
        "job_event_id_test 63 now returns 1");

    ok (job_event_id_set (job, 3) == 0,
        "job_event_id_set of the same event works");
    ok (job_event_id_test (job, 3) == 1,
        "job_event_id_test of multiple set event works");

    job_decref (job);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create ();
    test_create_from_eventlog ();
    test_create_from_json ();
    test_subscribe ();
    test_event_id_cache ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
