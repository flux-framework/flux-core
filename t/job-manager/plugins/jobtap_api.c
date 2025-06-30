/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <errno.h>
#include <flux/core.h>
#include <flux/jobtap.h>
#include "ccan/str/str.h"

static int test_prolog_start_finish (flux_plugin_t *p,
                                     const char *topic,
                                     flux_plugin_arg_t *args)
{
    errno = 0;
    if (flux_jobtap_prolog_start (NULL, NULL) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_start (NULL NULL)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_prolog_start (p, NULL) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_start (p, NULL)",
                                     errno,
                                     EINVAL);

    errno = 0;
    if (streq (topic, "job.state.cleanup")) {
        if (flux_jobtap_prolog_start (p, "test") == 0
            || errno != EINVAL)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "test", 0,
                                         "%s: %s: errno=%d != %d",
                                         topic,
                                         "flux_jobtap_prolog_start ",
                                         "after start request should fail",
                                         errno,
                                         EINVAL);


    }
    errno = 0;
    if (flux_jobtap_prolog_finish (NULL, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_finish (NULL, ...)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_prolog_finish (p, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_finish (p, NULL...)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_prolog_finish (NULL, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_finish (p, 1)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_prolog_finish (p, 1, "test", 0) == 0
        || errno != ENOENT)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s (%s): errno=%d != %d",
                                     topic,
                                     "flux_jobtap_prolog_finish",
                                     "p, 1, \"test\", 0",
                                     errno,
                                     EINVAL);


    return 0;
}


static int test_epilog_start_finish (flux_plugin_t *p,
                                     const char *topic,
                                     flux_plugin_arg_t *args)
{
    errno = 0;
    if (flux_jobtap_epilog_start (NULL, NULL) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_start (NULL NULL)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_epilog_start (p, NULL) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_start (p, NULL)",
                                     errno,
                                     EINVAL);

    errno = 0;
    if (streq (topic, "job.state.run")) {
        if (flux_jobtap_epilog_start (p, "test") == 0
            || errno != EINVAL)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "test", 0,
                                         "%s: %s: errno=%d != %d",
                                         topic,
                                         "flux_jobtap_epilog_start ",
                                         "after start request should fail",
                                         errno,
                                         EINVAL);


    }
    errno = 0;
    if (flux_jobtap_epilog_finish (NULL, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_finish (NULL, ...)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_epilog_finish (p, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_finish (p, NULL...)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_epilog_finish (NULL, FLUX_JOBTAP_CURRENT_JOB, NULL, 0) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s: errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_finish (p, 1)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_epilog_finish (p, 1, "test", 0) == 0
        || errno != ENOENT)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s (%s): errno=%d != %d",
                                     topic,
                                     "flux_jobtap_epilog_finish",
                                     "p, 1, \"test\", 0",
                                     errno,
                                     EINVAL);


    return 0;
}


static int test_event_post_pack (flux_plugin_t *p,
                                 const char *topic,
                                 flux_plugin_arg_t *args)
{
    const char *event = NULL;

    errno = 0;
    if (flux_jobtap_event_post_pack (NULL, 0, NULL, NULL) == 0
        || errno != EINVAL)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s (%s): errno=%d != %d",
                                     topic,
                                     "flux_jobtap_event_post_pack",
                                     " (NULL, ...)",
                                     errno,
                                     EINVAL);
    errno = 0;
    if (flux_jobtap_event_post_pack (p, 0, "foo", NULL) == 0
        || errno != ENOENT)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s (%s): errno=%d != %d",
                                     topic,
                                     "flux_jobtap_event_post_pack",
                                     " (NULL, ...)",
                                     errno,
                                     ENOENT);

    const char *state;
    if (strstarts (topic, "job.state."))
        state = topic+10;
    else
        state = topic+4;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:{s:{s?{s?{s?s}}}}}",
                                "jobspec",
                                 "attributes",
                                  "system",
                                   state,
                                    "post-event", &event) < 0)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "%s: %s: unpack_args: %s",
                                            topic,
                                            "test_event_post",
                                            flux_plugin_arg_strerror (args));
    if (event != NULL) {
        if (flux_jobtap_event_post_pack (p,
                                         FLUX_JOBTAP_CURRENT_JOB,
                                         event,
                                         "{s:s}",
                                         "test_context", "yes") < 0)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "test", 0,
                                         "%s: %s (event=%s): %s",
                                         topic,
                                         "flux_jobtap_event_post_pack",
                                         event,
                                         strerror (errno));
    }

    return 0;
}

static void set_flag_expect_error (const char *topic,
                                   flux_plugin_t *p,
                                   flux_jobid_t id,
                                   char *flag,
                                   char *msg,
                                   int expected_errno)
{
    errno = 0;
    int rc = flux_jobtap_job_set_flag (p, id, flag);
    if (rc == 0 || errno != expected_errno)
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "test", 0,
                                     "%s: %s (%s): errno=%d != %d",
                                     topic,
                                     "flux_jobtap_job_set_flag",
                                     msg,
                                     errno,
                                     expected_errno);
}


static int test_job_flags (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args)
{
    const char *flag = NULL;

    set_flag_expect_error (topic, NULL, 0, NULL,    "NULL, 0, NULL", EINVAL);
    set_flag_expect_error (topic, p,    0, NULL,    "p, 0, NULL",    EINVAL);
    set_flag_expect_error (topic, p,    0, "debug", "p, 0, debug",   ENOENT);

    set_flag_expect_error (topic, p,
                           FLUX_JOBTAP_CURRENT_JOB,
                           "foo",
                           "p, FLUX_JOBTAP_CURRENT_JOB, foo",
                           EINVAL);
    const char *state;
    if (strstarts (topic, "job.state."))
        state = topic+10;
    else
        state = topic+4;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:{s:{s?{s?{s?s}}}}}",
                                "jobspec",
                                "attributes",
                                "system",
                                state,
                                "set_flag", &flag) < 0)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "%s: %s: unpack_args: %s",
                                            topic,
                                            "test_job_flags",
                                            flux_plugin_arg_strerror (args));
    if (flag != NULL) {
        if (flux_jobtap_job_set_flag (p, FLUX_JOBTAP_CURRENT_JOB, flag) < 0)
            flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                         "test", 0,
                                         "%s: %s (flag=%s): %s",
                                         topic,
                                         "flux_jobtap_job_set_flag",
                                         flag,
                                         strerror (errno));
    }
    return 0;
}

static int test_job_lookup (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args)
{
    flux_jobid_t id;
    flux_jobid_t lookupid = FLUX_JOBID_ANY;
    flux_plugin_arg_t *oarg;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s?{s?I}}}}",
                                "id", &id,
                                "jobspec",
                                  "attributes",
                                    "system",
                                      "lookup-id", &lookupid) < 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "%s: failed to unpack lookupid: %s",
                                            topic,
                                            flux_plugin_arg_strerror (args));

    errno = 0;
    oarg = flux_jobtap_job_lookup (NULL, FLUX_JOBID_ANY);
    if (oarg != NULL || errno != EINVAL)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected errno=%d got %d",
                                           topic,
                                           "flux_jobtap_job_lookup",
                                           EINVAL,
                                           errno);

    errno = 0;
    oarg = flux_jobtap_job_lookup (p, 1234);
    if (oarg != NULL || errno != ENOENT)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected errno=%d got %d",
                                           topic,
                                           "flux_jobtap_job_lookup",
                                           ENOENT,
                                           errno);

    /*  lookup current job works */
    oarg = flux_jobtap_job_lookup (p, FLUX_JOBTAP_CURRENT_JOB);
    if (oarg == NULL)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: on current job failed: %s",
                                           topic,
                                           "flux_jobtap_job_lookup",
                                           strerror (errno));
    flux_plugin_arg_destroy (oarg);

    /*  Skip final test if lookupid not set in jobspec */
    if (lookupid == FLUX_JOBID_ANY)
        return 0;

    /*  lookup other job works */
    oarg = flux_jobtap_job_lookup (p, lookupid);
    if (oarg == NULL)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: on %ju failed: %s",
                                           topic,
                                           "flux_jobtap_job_lookup",
                                           (uintmax_t) lookupid,
                                           strerror (errno));
    flux_plugin_arg_destroy (oarg);
    return 0;
}

static int test_job_result (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args)
{
    int rc;
    flux_jobid_t id;
    flux_job_result_t result;
    flux_job_result_t expected_result = FLUX_JOB_RESULT_COMPLETED;
    const char *s = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s?{s?s}}}}",
                                "id", &id,
                                "jobspec",
                                  "attributes",
                                    "system",
                                      "expected-result", &s) < 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "%s: failed to unpack result: %s",
                                            topic,
                                            flux_plugin_arg_strerror (args));

    if (s != NULL && flux_job_strtoresult (s, &expected_result) < 0)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                            "test", 0,
                                            "%s: flux_job_strtoresult: %s",
                                            topic,
                                            strerror (errno));

    /*  Test flux_jobtap_get_job_result(3) ENOENT */
    errno = 0;
    rc = flux_jobtap_get_job_result (p, 1234, &result);
    if (rc == 0 || errno != ENOENT)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected errno=%d got %d",
                                           topic,
                                           "flux_jobtap_get_job_result",
                                           ENOENT,
                                           errno);

    /*  Test flux_jobtap_get_job_result(3) EINVAL */
    errno = 0;
    rc = flux_jobtap_get_job_result (NULL, 1234, &result);
    if (rc == 0 || errno != EINVAL)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected errno=%d got %d",
                                           topic,
                                           "flux_jobtap_get_job_result",
                                           EINVAL,
                                           errno);


    rc = flux_jobtap_get_job_result (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     &result);
    if (rc < 0 || expected_result != result)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected result=%d got %d",
                                           topic,
                                           "flux_jobtap_get_job_result",
                                           expected_result,
                                           result);

    return 0;
}

static int test_jobtap_call_einval (flux_plugin_t *p, flux_plugin_arg_t *args)
{
    int rc;

    errno = 0;
    rc = flux_jobtap_call (p, 0, "foo", args);
    if (rc > 0 || errno != ENOENT)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "flux_jobtap_call() %s: %s %d",
                                            "invalid id",
                                            "expected ENOENT got",
                                            errno);
    errno = 0;
    rc = flux_jobtap_call (NULL, FLUX_JOBTAP_CURRENT_JOB, "foo", args);
    if (rc > 0 || errno != EINVAL)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "flux_jobtap_call() %s: %s %d",
                                            "p=NULL",
                                            "expected EINVAL got",
                                            errno);
    errno = 0;
    rc = flux_jobtap_call (p, FLUX_JOBTAP_CURRENT_JOB, "foo", NULL);
    if (rc > 0 || errno != EINVAL)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "flux_jobtap_call() %s: %s %d",
                                            "args=NULL",
                                            "expected EINVAL got",
                                            errno);
    rc = flux_jobtap_call (p, FLUX_JOBTAP_CURRENT_JOB, "job.foo", args);
    if (rc > 0 || errno != EINVAL)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "flux_jobtap_call() %s: %s %d",
                                            "topic=job.foo",
                                            "expected EINVAL got",
                                            errno);
    return 0;
}

static int inactive_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    return test_job_result (p, topic, args);
}

static int cleanup_cb (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *arg)
{
    test_event_post_pack (p, topic, args);
    test_prolog_start_finish (p, topic, args);
    test_epilog_start_finish (p, topic, args);
    return test_job_result (p, topic, args);
}

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    int rc;
    flux_job_result_t result;

    test_job_flags (p, topic, args);

    /*  Test flux_jobtap_get_job_result(3) returns EINVAL here */
    errno = 0;
    rc = flux_jobtap_get_job_result (p, FLUX_JOBTAP_CURRENT_JOB, &result);
    if (rc == 0 || errno != EINVAL)
        return flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                           "test", 0,
                                           "%s: %s: expected errno=%d got %d",
                                           topic,
                                           "flux_jobtap_get_job_result",
                                           EINVAL,
                                           errno);
    test_event_post_pack (p, topic, args);
    test_prolog_start_finish (p, topic, args);
    test_epilog_start_finish (p, topic, args);
    return 0;
}

static int sched_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    test_job_flags (p, topic, args);
    test_event_post_pack (p, topic, args);
    return 0;
}

static int priority_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    test_job_flags (p, topic, args);
    test_event_post_pack (p, topic, args);
    test_jobtap_call_einval (p, args);
    return 0;
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    test_job_flags (p, topic, args);
    test_event_post_pack (p, topic, args);
    test_jobtap_call_einval (p, args);
    return test_job_lookup (p, topic, args);
}


static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    test_event_post_pack (p, topic, args);
    return test_job_lookup (p, topic, args);
}

static int new_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    test_event_post_pack (p, topic, args);
    return test_job_flags (p, topic, args);
}

static const struct flux_plugin_handler tab[] = {
    { "job.new",            new_cb,      NULL },
    { "job.validate",       validate_cb, NULL },
    { "job.state.priority", priority_cb, NULL },
    { "job.state.depend",   depend_cb,   NULL },
    { "job.state.sched",    sched_cb,    NULL },
    { "job.state.run",      run_cb,      NULL },
    { "job.state.cleanup",  cleanup_cb,  NULL },
    { "job.state.inactive", inactive_cb, NULL },
    { 0 }
};

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_register (p, "api-test", tab);
}

// vi:ts=4 sw=4 expandtab
