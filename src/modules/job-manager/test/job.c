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
#include "src/common/libeventlog/eventlog.h"
#include "src/modules/job-manager/job.h"
#include "ccan/str/str.h"

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
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.3,\"name\":\"validate\"}\n",


    /* 1 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"urgency\","
     "\"context\":{\"userid\":42,\"urgency\":1}}\n",

    /* 2 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":1}}\n",

    /* 3 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"exception\","
     "\"context\":{\"type\":\"cancel\",\"severity\":0,\"userid\":42}}\n",

    /* 4 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"exception\","
     "\"context\":{\"type\":\"meep\",\"severity\":1,\"userid\":42}}\n",

    /* 5 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":100}}\n"
    "{\"timestamp\":42.5,\"name\":\"alloc\"}\n",

    /* 6 */
    "{\"timestamp\":42.3,\"name\":\"alloc\"}\n",

    /* 7 */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n"
    "{\"timestamp\":42.4,\"name\":\"priority\","
     "\"context\":{\"priority\":100}}\n"
    "{\"timestamp\":42.4,\"name\":\"alloc\"}\n"
    "{\"timestamp\":42.5,\"name\":\"exception\","
     "\"context\":{\"type\":\"gasp\",\"severity\":0,\"userid\":42}}\n"
    "{\"timestamp\":42.6,\"name\":\"free\"}\n",

    /* 8 - no version attribute */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42}}\n"
    "{\"timestamp\":42.3,\"name\":\"depend\"}\n",

    /* 9 - version=0 (invalid) */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":0}}\n",

    /* 10 - submit+validate+submit should cause event_job_update to fail */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n"
    "{\"timestamp\":42.25,\"name\":\"validate\"}\n"
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n",

    /* 11 - submit leaves state NEW which is invalid */
    "{\"timestamp\":42.2,\"name\":\"submit\","
     "\"context\":{\"userid\":66,\"urgency\":16,\"flags\":42,\"version\":1}}\n",
};

void test_create_from_eventlog (void)
{
    struct job *job;
    flux_error_t error;

    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (2, "xyz", "{}", NULL, &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog on bad eventlog fails with EINVAL");
    like (error.text, "failed to decode eventlog",
          "and error.text is set");

    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (2,
                                    test_input[0],
                                    "}badjson}",
                                    NULL,
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog on bad jobspec fails with EINVAL");
    like (error.text, "failed to decode jobspec",
          "and error.text is set");

    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (2,
                                    test_input[0],
                                    "{}",
                                    "}badjson}",
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog on bad R fails with EINVAL");
    like (error.text, "failed to decode R",
          "and error.text is set");

    /* 0 - submit only */
    job = job_create_from_eventlog (2,
                                    test_input[0],
                                    "{}",
                                    NULL,
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit) failed: %s",
                  error.text);
    }
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
    job = job_create_from_eventlog (3,
                                    test_input[1],
                                    "{}",
                                    NULL,
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+urgency) failed: %s",
                  error.text);
    }
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
    job = job_create_from_eventlog (3,
                                    test_input[2],
                                    "{}",
                                    NULL,
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority) failed: %s",
                  error.text);
    }
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
    job = job_create_from_eventlog (3,
                                    test_input[3],
                                    "{}",
                                    NULL,
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex0) failed: %s",
                  error.text);
    }
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
    job = job_create_from_eventlog (3,
                                    test_input[4],
                                    "{}",
                                    NULL,
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+ex1) failed: %s",
                  error.text);
    }
    ok (job->state == FLUX_JOB_STATE_DEPEND,
        "job_create_from_eventlog log=(submit+ex1) set state=DEPEND");
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+ex1) set no internal flags");
    job_decref (job);

    /* 5 - submit + depend + priority + alloc */
    job = job_create_from_eventlog (3,
                                    test_input[5],
                                    "{}",
                                    "{}",
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority+alloc) failed: %s",
                  error.text);
    }
    ok (!job->alloc_pending
        && !job->free_pending
        && job->has_resources,
        "job_create_from_eventlog log=(submit+depend+priority+alloc) set has_resources flag");
    ok (job->R_redacted != NULL,
        "and R is set");
    ok (job->state == FLUX_JOB_STATE_RUN,
        "job_create_from_eventlog log=(submit+depend+priority+alloc) set state=RUN");
    job_decref (job);

    /* 6 - missing submit */
    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (3,
                                    test_input[6],
                                    "{}",
                                    "{}",
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(alloc) fails with EINVAL");
    ok (strlen (error.text) > 0,
        "and error.text is set");

    /* 7 - submit + depend + priority + alloc + ex0 + free */
    job = job_create_from_eventlog (3,
                                    test_input[7],
                                    "{}",
                                    "{}",
                                    &error);
    if (job == NULL) {
        BAIL_OUT ("job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) failed: %s",
                  error.text);
    }
    ok (!job->alloc_pending
        && !job->free_pending
        && !job->has_resources,
        "job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) set no internal flags");
    ok (job->state == FLUX_JOB_STATE_CLEANUP,
        "job_create_from_eventlog log=(submit+depend+priority+alloc+ex0+free) set state=CLEANUP");
    job_decref (job);

    /* 8 - no version (has no validate event) */
    job = job_create_from_eventlog (3,
                                    test_input[8],
                                    "{}",
                                    NULL,
                                    &error);
    ok (job != NULL,
        "job_create_from_eventlog version log=(submit.v0+depend) works");
    ok (job->state == FLUX_JOB_STATE_PRIORITY,
        "job_create_from_eventlog version log=(submit.v0+depend) state=PRIORITY");
    job_decref (job);

    /* 9 - invalid version */
    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (3,
                                    test_input[9],
                                    "{}",
                                    NULL,
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(submit.v0) fails with EINVAL");
    like (error.text, "eventlog v.* is unsupported",
          "and error.text is set");

    /* 10 - two submits */
    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (3,
                                    test_input[10],
                                    "{}",
                                    NULL,
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(submit,validate,submit) fails with EINVAL");
    like (error.text, "could not apply",
          "and error.text is set");

    /* 11 - one submit */
    errno = 0;
    error.text[0] = '\0';
    job = job_create_from_eventlog (3,
                                    test_input[11],
                                    "{}",
                                    NULL,
                                    &error);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_eventlog log=(submit) fails with EINVAL");
    like (error.text, "job state .* is invalid after replay",
          "and error.text is set");
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
        && job->queue == NULL
        && job->flags == 0,
        "job json object was properly decoded");
    json_decref (o);
    job_decref (job);

    if (!(o = json_pack ("{s:I s:i s:i s:f s:i s:{s:{s:{s:s}}}}",
                         "id", 1LL,
                         "urgency", 10,
                         "userid", 42,
                         "t_submit", 1.0,
                         "flags", 0,
                         "jobspec",
                         "attributes", "system", "queue", "foo")))
        BAIL_OUT ("json_pack failed");
    ok ((job = job_create_from_json (o)) != NULL,
        "job_create_from_json works");
    ok (job->id == 1
        && job->urgency == 10
        && job->userid == 42
        && job->t_submit == 1.0
        && job->queue && streq (job->queue, "foo")
        && job->flags == 0,
        "job json object was properly decoded w/ queue");
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

static void test_event_queue (void)
{
    struct job *job = job_create ();
    int flags;
    json_t *entry;
    const char *name;
    json_t *context;
    int i;
    char dbuf[128];

    if (!job)
        BAIL_OUT ("job_create failed");

    if (json_array_append (job->event_queue, json_null ()) < 0)
        BAIL_OUT ("could not enqueue bad event entry");
    errno = 0;
    ok (job_event_peek (job, &flags, &entry) < 0 && errno == EPROTO,
        "job_event_peek fails with EPROTO on badly wrapped eventlog entry");
    errno = 0;
    ok (job_event_dequeue (job, &flags, &entry) < 0 && errno == EPROTO,
        "job_event_dequeue fails with EPROTO on badly wrapped eventlog entry");
    json_array_remove (job->event_queue, 0);

    errno = 0;
    ok (job_event_peek (job, &flags, &entry) < 0 && errno == ENOENT,
        "job_event_peek fails with ENOENT when there are no events");
    ok (job_event_is_queued (job, "foo") == false,
        "job_event_is_queued foo returns false");
    /* Post two test events
     */
    entry = eventlog_entry_pack (0.,
                                 "foo",
                                 "{s:i}",
                                 "bar", 42);
    if (!entry)
        BAIL_OUT ("eventlog_entry_pack failed");
    ok (job_event_enqueue (job, 42, entry) == 0,
        "job_event_enqueue works");
    json_decref (entry);
    diag ("queue: %s", job_event_queue_print (job, dbuf, sizeof (dbuf)));
    ok (json_array_size (job->event_queue) == 1,
        "queue size is 1");
    ok (job_event_is_queued (job, "foo") == true,
        "job_event_is_queued foo returns true");
    entry = eventlog_entry_pack (0.,
                                 "bar",
                                 "{s:i}",
                                 "baz", 43);
    if (!entry)
        BAIL_OUT ("eventlog_entry_pack failed");
    ok (job_event_enqueue (job, 43, entry) == 0,
        "job_event_enqueue works");
    json_decref (entry);
    diag ("queue: %s", job_event_queue_print (job, dbuf, sizeof (dbuf)));
    ok (json_array_size (job->event_queue) == 2,
        "queue size is 2");
    ok (job_event_is_queued (job, "bar") == true,
        "job_event_is_queued bar returns true");
    /* Check the first event
     */
    flags = 0;
    entry = NULL;
    ok (job_event_peek (job, &flags, &entry) == 0,
        "job_event_peek works");
    ok (eventlog_entry_parse (entry, NULL, &name, &context) == 0
        && streq (name, "foo")
        && json_unpack (context, "{s:i}", "bar", &i) == 0
        && i == 42,
        "eventlog entry is correct");
    ok (flags == 42,
        "flags are correct");
    ok (json_array_size (job->event_queue) == 2,
        "queue size is still 2");
    flags = 0;
    entry = NULL;
    ok (job_event_dequeue (job, &flags, &entry) == 0,
        "job_event_dequeue with NULL args works");
    diag ("queue: %s", job_event_queue_print (job, dbuf, sizeof (dbuf)));
    ok (json_array_size (job->event_queue) == 1,
        "queue size is now 1");
    ok (eventlog_entry_parse (entry, NULL, &name, &context) == 0
        && streq (name, "foo")
        && json_unpack (context, "{s:i}", "bar", &i) == 0
        && i == 42,
        "eventlog entry is correct");
    ok (flags == 42,
        "flags are correct");
    json_decref (entry);
    ok (json_array_size (job->event_queue) == 1,
        "queue size is now 1");
    /* Check the second event
     */
    flags = 0;
    entry = NULL;
    ok (job_event_peek (job, &flags, &entry) == 0,
        "job_event_peek works");
    ok (eventlog_entry_parse (entry, NULL, &name, &context) == 0
        && streq (name, "bar")
        && json_unpack (context, "{s:i}", "baz", &i) == 0
        && i == 43,
        "eventlog entry is correct");
    ok (flags == 43,
        "flags are correct");
    ok (json_array_size (job->event_queue) == 1,
        "queue size is still 1");
    ok (job_event_dequeue (job, NULL, NULL) == 0,
        "job_event_dequeue with NULL args works");
    ok (json_array_size (job->event_queue) == 0,
        "queue size is now 0");

    job_decref (job);
}

static void test_jobspec_update (void)
{
    struct job *job;
    flux_error_t error;
    const char *command;
    const char *queue;
    json_t *o;
    json_t *cpy;
    int ret;

    /* corner cases */

    if (!(o = json_pack ("{s:s}", "dummy", "dummy")))
        BAIL_OUT ("failed to create update");

    ok (validate_jobspec_updates (o) == false,
        "validate_jobspec_updates fails on bad update keys");

    json_decref (o);

    if (!(job = job_create()))
        BAIL_OUT ("failed to create empty job");

    ret = job_apply_jobspec_updates (job, NULL);
    ok (ret == -1 && errno == EINVAL,
        "job_apply_jobspec_updates fail on job with no jobspec");

    cpy = job_jobspec_with_updates (job, NULL);
    ok (cpy == NULL && errno == EAGAIN,
        "job_jobspec_with_updates fail on job with no jobspec");

    job_decref (job);

    /* functional tests */

    if (!(job = job_create_from_eventlog (1234,
                                          test_input[0],
                                          "{}",
                                          NULL,
                                          &error)))
        BAIL_OUT ("failed to create job w/ empty jobspec");

    if (!(o = json_pack ("{s:[{s:[s]}] s:s}",
                         "tasks",
                           "command", "hostname",
                         "attributes.system.queue", "foo")))
        BAIL_OUT ("failed to create update");

    ok (validate_jobspec_updates (o) == true,
        "validate_jobspec_updates success update keys");

    ok (job->queue == NULL, "job->queue NULL before update");

    ret = job_apply_jobspec_updates (job, o);
    ok (ret == 0, "job_apply_jobspec_updates success");
    json_decref (o);

    ret = json_unpack (job->jobspec_redacted,
                       "{s:[{s:[s]}]}",
                       "tasks",
                         "command", &command);
    ok (ret == 0, "parsed jobspec command");
    ok (command && streq (command, "hostname"),
        "jobspec command updated correctly");

    ret = json_unpack (job->jobspec_redacted,
                       "{s:{s:{s:s}}}",
                       "attributes",
                         "system",
                           "queue", &queue);
    ok (ret == 0, "parsed jobspec queue");
    ok (queue && streq (queue, "foo"),
        "jobspec queue updated correctly");

    ok (job->queue && streq (job->queue, "foo"), "job->queue=foo after update");

    if (!(o = json_pack ("{s:s}", "attributes.system.queue", "bar")))
        BAIL_OUT ("failed to create update");

    cpy = job_jobspec_with_updates (job, o);
    ok (cpy != NULL, "job_jobspec_with_updates success");
    json_decref (o);

    ret = json_unpack (cpy,
                       "{s:{s:{s:s}}}",
                       "attributes",
                         "system",
                           "queue", &queue);
    ok (ret == 0, "parsed jobspec queue in cpy");
    ok (queue && streq (queue, "bar"), "jobspec cpy has updated queue");
    json_decref (cpy);

    ret = json_unpack (job->jobspec_redacted,
                       "{s:{s:{s:s}}}",
                       "attributes",
                         "system",
                           "queue", &queue);
    ok (ret == 0, "parsed jobspec queue in original");
    ok (queue && streq (queue, "foo"), "job jobspec not modified");

    job_decref (job);
}

static void test_resource_update ()
{
    struct job *job;
    double expiration;
    json_t *update;
    int rc;


    if (!(job = job_create()))
        BAIL_OUT ("failed to create empty job");
    if (!(update = json_pack ("{s:f}", "expiration", 100.)))
        BAIL_OUT ("failed to create update");
    ok (update != NULL, "create valid resource-update context");

    rc = job_apply_resource_updates (job, update);
    ok (rc == -1 && errno == EAGAIN,
        "job_apply_resource_updates fails on job without R_redacted");
    json_decref (update);

    job->R_redacted = json_pack ("{s:i s:{s:f s:f}}",
                                 "version", 1,
                                 "execution",
                                 "starttime", 1.,
                                 "expiration", 2.);
    if (!job->R_redacted)
        BAIL_OUT ("Failed to create fake R_redacted");
    ok (job->R_redacted != NULL,
        "Create fake job->R_redacted");

    if (!(update = json_pack ("{s:f s:s}",
                              "expiration", 100.,
                              "dummy", "test")))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ multiple updates");

    rc = job_apply_resource_updates (job, update);
    ok (rc == -1 && errno == EINVAL,
        "job_apply_resource_updates fails with multiple updates");
    json_decref (update);

    if (!(update = json_pack ("{s:s}", "dummy", "test")))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ invalid update key");

    rc = job_apply_resource_updates (job, update);
    ok (rc == -1 && errno == EINVAL,
        "job_apply_resource_updates fails with invalid update key");
    json_decref (update);

    if (!(update = json_pack ("{s:s}", "expiration", "test")))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ invalid value");

    rc = job_apply_resource_updates (job, update);
    ok (rc == -1 && errno == EINVAL,
        "job_apply_resource_updates fails with invalid update value");
    json_decref (update);

    if (!(update = json_pack ("{s:f}", "expiration", -1.0)))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ negative expiration");

    rc = job_apply_resource_updates (job, update);
    ok (rc == -1 && errno == EINVAL,
        "job_apply_resource_updates fails with negative expiration");
    json_decref (update);

    if (!(update = json_pack ("{s:f}", "expiration", 100.0)))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ valid expiration");

    rc = job_apply_resource_updates (job, update);
    ok (rc ==0,
        "job_apply_resource_updates() works with valid expiration");
    json_decref (update);

    if (json_unpack (job->R_redacted,
                     "{s:{s:F}}",
                     "execution",
                      "expiration", &expiration) < 0)
        BAIL_OUT ("Failed to unpack new expiration from R_redacted");
    ok (expiration == 100.,
        "expiration was updated in R_redacted");

    if (!(update = json_pack ("{s:f}", "expiration", 0.0)))
        BAIL_OUT ("failed to create update");
    ok (update != NULL,
        "create resource-update context w/ 0.0 expiration");

    rc = job_apply_resource_updates (job, update);
    ok (rc ==0,
        "job_apply_resource_updates() works with 0.0 expiration");
    json_decref (update);

    if (json_unpack (job->R_redacted,
                     "{s:{s:F}}",
                     "execution",
                      "expiration", &expiration) < 0)
        BAIL_OUT ("Failed to unpack new expiration from R_redacted");
    ok (expiration == 0.0,
        "expiration was updated in R_redacted");

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
    test_event_queue ();
    test_jobspec_update ();
    test_resource_update ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
