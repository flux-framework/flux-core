/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include "src/modules/job-list/job_data.h"
#include "src/modules/job-list/match.h"
#include "ccan/str/str.h"

static void list_constraint_create_corner_case (const char *str,
                                                const char *fmt,
                                                ...)
{
    struct list_constraint *c;
    char buf[1024];
    flux_error_t error;
    json_error_t jerror;
    json_t *jc;
    va_list ap;

    if (!(jc = json_loads (str, 0, &jerror)))
        BAIL_OUT ("json constraint invalid: %s", jerror.text);

    va_start (ap, fmt);
    vsnprintf(buf, sizeof (buf), fmt, ap);
    va_end (ap);

    c = list_constraint_create (jc, &error);

    ok (c == NULL, "list_constraint_create fails on %s", buf);
    diag ("error: %s", error.text);
    json_decref (jc);
}

static void test_corner_case (void)
{
    ok (job_match (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "job_match returns EINVAL on NULL inputs");

    list_constraint_create_corner_case ("{\"userid\":[1], \"name\":[\"foo\"] }",
                                        "object with too many keys");
    list_constraint_create_corner_case ("{\"userid\":1}",
                                        "object with values not array");
    list_constraint_create_corner_case ("{\"foo\":[1]}",
                                        "object with invalid operation");
    list_constraint_create_corner_case ("{\"userid\":[\"foo\"]}",
                                        "userid value not integer");
    list_constraint_create_corner_case ("{\"name\":[1]}",
                                        "name value not string");
    list_constraint_create_corner_case ("{\"queue\":[1]}",
                                        "queue value not string");
    list_constraint_create_corner_case ("{\"states\":[0.0]}",
                                        "states value not integer or string");
    list_constraint_create_corner_case ("{\"states\":[\"foo\"]}",
                                        "states value not valid string");
    list_constraint_create_corner_case ("{\"states\":[8192]}",
                                        "states value not valid integer");
    list_constraint_create_corner_case ("{\"results\":[0.0]}",
                                        "results value not integer or string");
    list_constraint_create_corner_case ("{\"results\":[\"foo\"]}",
                                        "results value not valid string");
    list_constraint_create_corner_case ("{\"results\":[8192]}",
                                        "results value not valid integer");
    list_constraint_create_corner_case ("{\"t_depend\":[]}",
                                        "t_depend value not specified");
    list_constraint_create_corner_case ("{\"t_depend\":[1.0]}",
                                        "t_depend value in invalid format (int)");
    list_constraint_create_corner_case ("{\"t_depend\":[\"0.0\"]}",
                                        "t_depend no comparison operator");
    list_constraint_create_corner_case ("{\"t_depend\":[\">=foof\"]}",
                                        "t_depend value invalid (str)");
    list_constraint_create_corner_case ("{\"t_depend\":[\">=-1.0\"]}",
                                        "t_depend value < 0.0 (str)");
    list_constraint_create_corner_case ("{\"not\":[1]}",
                                        "sub constraint not a constraint");
}

static struct job *setup_job (uint32_t userid,
                              const char *name,
                              const char *queue,
                              flux_job_state_t state,
                              flux_job_result_t result,
                              double t_submit,
                              double t_depend,
                              double t_run,
                              double t_cleanup,
                              double t_inactive)
{
    struct job *job;
    int bitmask = 0x1;
    if (!(job = job_create (NULL, FLUX_JOBID_ANY)))
        BAIL_OUT ("failed to create job");
    job->userid = userid;
    if (name)
        job->name = name;
    if (queue)
        job->queue = queue;
    job->state = state;
    if (state) {
        /* Assume all jobs run, we don't skip any states, so add bitmask
         * for all states lower than configured one
         */
        job->states_mask = job->state;
        while (!(job->states_mask & bitmask)) {
            job->states_mask |= bitmask;
            bitmask <<= 1;
        }
    }
    job->result = result;
    job->t_submit = t_submit;
    job->t_depend = t_depend;
    job->t_run = t_run;
    job->t_cleanup = t_cleanup;
    job->t_inactive = t_inactive;
    /* assume for all tests */
    job->submit_version = 1;
    return job;
}

static struct list_constraint *create_list_constraint (const char *constraint)
{
    struct list_constraint *c;
    flux_error_t error;
    json_error_t jerror;
    json_t *jc = NULL;

    if (constraint) {
        if (!(jc = json_loads (constraint, 0, &jerror)))
            BAIL_OUT ("json constraint invalid: %s", jerror.text);
    }

    if (!(c = list_constraint_create (jc, &error)))
        BAIL_OUT ("list constraint create fail: %s", error.text);

    json_decref (jc);
    return c;
}

static void test_basic_special_cases (void)
{
    struct job *job = setup_job (0, NULL, NULL, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0);
    struct list_constraint *c;
    flux_error_t error;
    int rv;

    c = create_list_constraint ("{}");
    rv = job_match (job, c, &error);
    ok (rv == true, "empty object works as expected");
    list_constraint_destroy (c);

    c = create_list_constraint (NULL);
    rv = job_match (job, c, &error);
    ok (rv == true, "NULL constraint works as expected");
    list_constraint_destroy (c);

    job_destroy (job);
}

struct basic_userid_test {
    uint32_t userid;
    int expected;
};

struct basic_userid_constraint_test {
    const char *constraint;
    struct basic_userid_test tests[4];
} basic_userid_tests[] = {
    {
        "{ \"userid\": [ ] }",
        {
            { 42, false, },
            {  0, false, },
        },
    },
    {
        "{ \"userid\": [ 42 ] }",
        {
            { 42, true,  },
            { 43, false, },
            {  0, false, },
        },
    },
    {
        "{ \"userid\": [ 42, 43 ] }",
        {
            { 42, true,  },
            { 43, true,  },
            { 44, false, },
            {  0, false, },
        },
    },
    /* FLUX_USERID_UNKNOWN = 0xFFFFFFFF */
    {
        "{ \"userid\": [ -1 ] }",
        {
            { 42, true,  },
            { 43, true,  },
            {  0, false, },
        },
    },
    {
        NULL,
        {
            { 0, false, },
        },
    },
};

static void test_basic_userid (void)
{
    struct basic_userid_constraint_test *ctests = basic_userid_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_userid_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (tests->userid) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (tests->userid,
                             NULL,
                             NULL,
                             0,
                             0,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic userid job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_name_test {
    const char *name;
    int expected;
    bool end;                   /* name can be NULL */
};

struct basic_name_constraint_test {
    const char *constraint;
    struct basic_name_test tests[5];
} basic_name_tests[] = {
    {
        "{ \"name\": [ ] }",
        {
            /* N.B. name can potentially be NULL */
            {  NULL, false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        "{ \"name\": [ \"foo\" ] }",
        {
            /* N.B. name can potentially be NULL */
            {  NULL, false, false, },
            { "foo",  true, false, },
            { "bar", false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        "{ \"name\": [ \"foo\", \"bar\" ] }",
        {
            /* N.B. name can potentially be NULL */
            {  NULL, false, false, },
            { "foo",  true, false, },
            { "bar",  true, false, },
            { "baz", false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        NULL,
        {
            { NULL, false,  true, },
        },
    },
};

static void test_basic_name (void)
{
    struct basic_name_constraint_test *ctests = basic_name_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_name_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (!tests->end) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (0,
                             tests->name,
                             NULL,
                             0,
                             0,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic name job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_queue_test {
    const char *queue;
    int expected;
    bool end;                   /* queue can be NULL */
};

struct basic_queue_constraint_test {
    const char *constraint;
    struct basic_queue_test tests[5];
} basic_queue_tests[] = {
    {
        "{ \"queue\": [ ] }",
        {
            /* N.B. queue can potentially be NULL */
            {  NULL, false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        "{ \"queue\": [ \"foo\" ] }",
        {
            /* N.B. queue can potentially be NULL */
            {  NULL, false, false, },
            { "foo",  true, false, },
            { "bar", false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        "{ \"queue\": [ \"foo\", \"bar\" ] }",
        {
            /* N.B. queue can potentially be NULL */
            {  NULL, false, false, },
            { "foo",  true, false, },
            { "bar",  true, false, },
            { "baz", false, false, },
            {  NULL, false,  true, },
        },
    },
    {
        NULL,
        {
            { NULL, false,  true, },
        },
    },
};

static void test_basic_queue (void)
{
    struct basic_queue_constraint_test *ctests = basic_queue_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_queue_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (!tests->end) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (0,
                             NULL,
                             tests->queue,
                             0,
                             0,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic queue job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_states_test {
    flux_job_state_t state;
    int expected;
};

struct basic_states_constraint_test {
    const char *constraint;
    struct basic_states_test tests[4];
} basic_states_tests[] = {
    {
        "{ \"states\": [ ] }",
        {
            { FLUX_JOB_STATE_NEW, false,  },
            {                  0, false, },
        },
    },
    {
        /* sanity check integer inputs work, we assume FLUX_JOB_STATE_NEW
         * will always be 1, use strings everywhere else
         */
        "{ \"states\": [ 1 ] }",
        {
            { FLUX_JOB_STATE_NEW, true,  },
            {                  0, false, },
        },
    },
    {
        "{ \"states\": [ \"sched\" ] }",
        {
            { FLUX_JOB_STATE_SCHED,  true, },
            {   FLUX_JOB_STATE_RUN, false, },
            {                    0, false, },
        },
    },
    {
        "{ \"states\": [ \"sched\", \"RUN\" ] }",
        {
            {    FLUX_JOB_STATE_SCHED, true,  },
            {      FLUX_JOB_STATE_RUN, true,  },
            { FLUX_JOB_STATE_INACTIVE, false, },
            {                       0, false, },
        },
    },
    {
        NULL,
        {
            { 0, false, },
        },
    },
};

static void test_basic_states (void)
{
    struct basic_states_constraint_test *ctests = basic_states_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_states_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (tests->state) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (0,
                             NULL,
                             NULL,
                             tests->state,
                             0,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic states job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_results_test {
    flux_job_state_t state;
    flux_job_result_t result;
    int expected;
};

struct basic_results_constraint_test {
    const char *constraint;
    struct basic_results_test tests[4];
} basic_results_tests[] = {
    {
        "{ \"results\": [ ] }",
        {
            { FLUX_JOB_STATE_NEW, FLUX_JOB_RESULT_COMPLETED, false, },
            {                  0,                         0, false, },
        },
    },
    {
        /* sanity check integer inputs work, we assume
         * FLUX_JOB_RESULT_COMPLETED will always be 1, use strings
         * everywhere else
         */
        "{ \"results\": [ 1 ] }",
        {
            { FLUX_JOB_STATE_INACTIVE, FLUX_JOB_RESULT_COMPLETED,  true, },
            {                       0,                         0, false, },
        },
    },
    {
        "{ \"results\": [ \"completed\" ] }",
        {
            { FLUX_JOB_STATE_RUN,                              0, false, },
            { FLUX_JOB_STATE_INACTIVE, FLUX_JOB_RESULT_COMPLETED,  true, },
            { FLUX_JOB_STATE_INACTIVE,    FLUX_JOB_RESULT_FAILED, false, },
            {                       0,                         0, false, },
        },
    },
    {
        "{ \"results\": [ \"completed\", \"FAILED\" ] }",
        {
            { FLUX_JOB_STATE_INACTIVE, FLUX_JOB_RESULT_COMPLETED,  true, },
            { FLUX_JOB_STATE_INACTIVE,    FLUX_JOB_RESULT_FAILED,  true, },
            { FLUX_JOB_STATE_INACTIVE,  FLUX_JOB_RESULT_CANCELED, false, },
            {                       0,                         0, false, },
        },
    },
    {
        NULL,
        {
            { 0, 0, false, },
        },
    },
};

static void test_basic_results (void)
{
    struct basic_results_constraint_test *ctests = basic_results_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_results_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (tests->state) {  /* result can be 0, iterate on state > 0 */
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (0,
                             NULL,
                             NULL,
                             tests->state,
                             tests->result,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic results job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_timestamp_test {
    flux_job_state_t state;
    int submit_version;
    double t_submit;
    double t_depend;
    double t_run;
    double t_cleanup;
    double t_inactive;
    int expected;
    bool end;                   /* timestamps can be 0 */
};

struct basic_timestamp_constraint_test {
    const char *constraint;
    struct basic_timestamp_test tests[7];
} basic_timestamp_tests[] = {
    {
        "{ \"t_submit\": [ \">=0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, true, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, true, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, true, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, true, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, true, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_depend\": [ \">=0.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, true,  false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    /* N.B. t_run >= 0 is false if state RUN not yet reached */
    {
        "{ \"t_run\": [ \">=0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, true,  false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    /* N.B. t_cleanup >= 0 is false if state CLEANUP not yet reached */
    {
        "{ \"t_cleanup\": [ \">=0.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, true,  false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, true,  false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    /* N.B. t_inactive >= 0 is false if state INACTIVE not yet reached */
    {
        "{ \"t_inactive\": [ \">=0.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, true,  false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<100.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<=100.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<50.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<=50.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<25.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \"<=25.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">100.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">=100.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">50.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">=50.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">25.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_inactive\": [ \">=25.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    /*
     * Need to test special legacy case, submit_version == 0 where
     * `t_depend` means `t_submit`.  So all tests fail for <15.0 when
     * submit version == 1, but should all pass for submit version == 0.
     */
    {
        "{ \"t_depend\": [ \"<15.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_PRIORITY, 1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_SCHED,    1, 10.0, 20.0,  0.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_RUN,      1, 10.0, 20.0, 30.0,  0.0,  0.0, false, false, },
            { FLUX_JOB_STATE_CLEANUP,  1, 10.0, 20.0, 30.0, 40.0,  0.0, false, false, },
            { FLUX_JOB_STATE_INACTIVE, 1, 10.0, 20.0, 30.0, 40.0, 50.0, false, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        "{ \"t_depend\": [ \"<15.0\" ] }",
        {
            { FLUX_JOB_STATE_DEPEND,   0, 10.0, 20.0,  0.0,  0.0,  0.0,  true, false, },
            { FLUX_JOB_STATE_PRIORITY, 0, 10.0, 20.0,  0.0,  0.0,  0.0,  true, false, },
            { FLUX_JOB_STATE_SCHED,    0, 10.0, 20.0,  0.0,  0.0,  0.0,  true, false, },
            { FLUX_JOB_STATE_RUN,      0, 10.0, 20.0, 30.0,  0.0,  0.0,  true, false, },
            { FLUX_JOB_STATE_CLEANUP,  0, 10.0, 20.0, 30.0, 40.0,  0.0,  true, false, },
            { FLUX_JOB_STATE_INACTIVE, 0, 10.0, 20.0, 30.0, 40.0, 50.0,  true, false, },
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
    {
        NULL,
        {
            { 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, false,  true, },
        },
    },
};

static void test_basic_timestamp (void)
{
    struct basic_timestamp_constraint_test *ctests = basic_timestamp_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_timestamp_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (!tests->end) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (0,
                             NULL,
                             NULL,
                             tests->state,
                             0,
                             tests->t_submit,
                             tests->t_depend,
                             tests->t_run,
                             tests->t_cleanup,
                             tests->t_inactive);
            /* special for legacy corner case */
            job->submit_version = tests->submit_version;
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic timestamp job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

struct basic_conditionals_test {
    uint32_t userid;
    const char *name;
    int expected;
};

struct basic_conditionals_constraint_test {
    const char *constraint;
    struct basic_conditionals_test tests[5];
} basic_conditionals_tests[] = {
    {
        "{ \"or\": [] }",
        {
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"and\": [] }",
        {
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"not\": [] }",
        {
            { 42, "foo", false, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"not\": [ { \"userid\": [ 42 ] } ] }",
        {
            { 42, "foo", false, },
            { 43, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"or\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            { 43, "bar", false, },
            { 42, "bar",  true, },
            { 43, "foo",  true, },
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"or\": \
           [ \
             { \"not\": [ { \"userid\": [ 42 ] } ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            { 43, "bar",  true, },
            { 42, "bar", false, },
            { 43, "foo",  true, },
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"not\": \
           [ \
             { \"or\": \
               [ \
                 { \"userid\": [ 42 ] }, \
                 { \"name\": [ \"foo\" ] } \
               ] \
             } \
           ] \
        }",
        {
            { 43, "bar",  true, },
            { 42, "bar", false, },
            { 43, "foo", false, },
            { 42, "foo", false, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            { 43, "bar", false, },
            { 42, "bar", false, },
            { 43, "foo", false, },
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"not\": [ { \"userid\": [ 42 ] } ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            { 43, "bar", false, },
            { 42, "bar", false, },
            { 43, "foo",  true, },
            { 42, "foo", false, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"not\": \
           [ \
             { \"and\": \
               [ \
                 { \"userid\": [ 42 ] }, \
                 { \"name\": [ \"foo\" ] } \
               ] \
             } \
           ] \
        }",
        {
            { 43, "bar",  true, },
            { 42, "bar",  true, },
            { 43, "foo",  true, },
            { 42, "foo", false, },
            {  0,  NULL, false, },
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"or\": \
               [ \
                 { \"userid\": [ 42 ] }, \
                 { \"userid\": [ 43 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            { 43, "bar", false, },
            { 42, "bar", false, },
            { 43, "foo",  true, },
            { 42, "foo",  true, },
            {  0,  NULL, false, },
        },
    },
    {
        NULL,
        {
            {  0,  NULL, false, },
        },
    },
};

static void test_basic_conditionals (void)
{
    struct basic_conditionals_constraint_test *ctests = basic_conditionals_tests;
    int index = 0;

    while (ctests->constraint) {
        struct basic_conditionals_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (tests->userid) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (tests->userid,
                             tests->name,
                             NULL,
                             0,
                             0,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             0.0);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "basic conditionals job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

/* following tests emulate some "realworld"-ish matching */
struct realworld_test {
    uint32_t userid;
    const char *name;
    const char *queue;
    flux_job_state_t state;
    flux_job_result_t result;
    double t_inactive;
    int expected;
};

struct realworld_constraint_test {
    const char *constraint;
    struct realworld_test tests[8];
} realworld_tests[] = {
    {
        /* all the jobs in all states for a specific user */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"states\": [ \"pending\", \"running\", \"inactive\" ] } \
           ] \
        }",
        {
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                true,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                true,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                2000.0,
                true,
            },
            {
                43,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                2000.0,
                false,
            },
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
    {
        /* all the unsuccessful jobs for a specific user */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"results\": [ \"failed\", \"canceled\", \"timeout\" ] } \
           ] \
        }",
        {
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_FAILED,
                2000.0,
                true,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_CANCELED,
                2000.0,
                true,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_TIMEOUT,
                2000.0,
                true,
            },
            {
                43,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_FAILED,
                2000.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                false,
            },
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
    {
        /* all the pending and running jobs for a user, in two specific queues */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"states\" : [ \"pending\", \"running\" ] }, \
             { \"queue\": [ \"batch\", \"debug\" ] } \
           ] \
        }",
        {
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                true,
            },
            {
                42,
                "foo",
                "debug",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                true,
            },
            {
                42,
                "foo",
                "debug",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                true,
            },
            {
                43,
                "foo",
                "batch",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                2000.0,
                false,
            },
            {
                42,
                "foo",
                "gpu",
                FLUX_JOB_STATE_DEPEND,
                0,
                0.0,
                false,
            },
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
    {
        /* jobs for a user, in queue batch, with specific job name, are running */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"queue\": [ \"batch\" ] }, \
             { \"name\": [ \"foo\" ] }, \
             { \"states\": [ \"running\" ] } \
           ] \
        }",
        {
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                true,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_CLEANUP,
                0,
                0.0,
                true,
            },
            {
                43,
                "foo",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "debug",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                false,
            },
            {
                42,
                "bar",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                2000.0,
                false,
            },
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
    {
        /* all the inactive jobs since a specific time (via t_inactve) */
        "{ \"and\": \
           [ \
             { \"states\": [ \"inactive\" ] }, \
             { \"t_inactive\": [ \">=500.0\" ] } \
           ] \
        }",
        {
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_SCHED,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_RUN,
                0,
                0.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                100.0,
                false,
            },
            {
                42,
                "foo",
                "batch",
                FLUX_JOB_STATE_INACTIVE,
                FLUX_JOB_RESULT_COMPLETED,
                1000.0,
                true,
            },
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
    {
        NULL,
        {
            {
                0,
                NULL,
                NULL,
                0,
                0,
                0.0,
                false
            },
        },
    },
};

static void test_realworld (void)
{
    struct realworld_constraint_test *ctests = realworld_tests;
    int index = 0;

    while (ctests->constraint) {
        struct realworld_test *tests = ctests->tests;
        struct list_constraint *c;
        int index2 = 0;

        c = create_list_constraint (ctests->constraint);
        while (tests->userid) {
            struct job *job;
            flux_error_t error;
            int rv;
            job = setup_job (tests->userid,
                             tests->name,
                             tests->queue,
                             tests->state,
                             tests->result,
                             0.0,
                             0.0,
                             0.0,
                             0.0,
                             tests->t_inactive);
            rv = job_match (job, c, &error);
            ok (rv == tests->expected,
                "realworld job match test #%d/#%d",
                index, index2);
            job_destroy (job);
            index2++;
            tests++;
        }

        index++;
        list_constraint_destroy (c);
        ctests++;
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_corner_case ();
    test_basic_special_cases ();
    test_basic_userid ();
    test_basic_name ();
    test_basic_queue ();
    test_basic_states ();
    test_basic_results ();
    test_basic_timestamp ();
    test_basic_conditionals ();
    test_realworld ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
