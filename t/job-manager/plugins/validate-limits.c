/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* validate-limits.c - check that the jobtap API validates plugin data
 *
 * Each test posts its result as a memo so that a sharness test can check
 * the outcome of every case with a single query. A value of 1 means the
 * API behaved as expected.
 *
 * See flux-framework/flux-core#7815.
 */

#include <string.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>

/*  Build an object nested 'depth' levels deep in the shape that caused
 *  #7815, i.e. constraints repeatedly wrapped in {"and": [...]}
 */
static json_t *wrapped_constraints (int depth)
{
    json_t *o;

    if (!(o = json_pack ("{s:[{s:[s]}]}",
                         "and",
                           "hostlist", "foo")))
        return NULL;
    for (int i = 0; i < depth; i++) {
        json_t *n;
        if (!(n = json_pack ("{s:[O]}", "and", o))) {
            json_decref (o);
            return NULL;
        }
        json_decref (o);
        o = n;
    }
    return o;
}

static void memo (flux_plugin_t *p, const char *name, int ok)
{
    flux_jobtap_event_post_pack (p,
                                 FLUX_JOBTAP_CURRENT_JOB,
                                 "memo",
                                 "{s:i}",
                                 name,
                                 ok ? 1 : 0);
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    json_t *o;

    /*  A deeply nested jobspec update must be rejected
     */
    if ((o = wrapped_constraints (200))) {
        int rc = flux_jobtap_jobspec_update_pack (p,
                                                  "{s:O}",
                                                  "attributes.system.constraints",
                                                  o);
        memo (p, "deep_update_rejected", rc < 0);
        json_decref (o);
    }

    /*  ... but a normally nested one must still be accepted
     */
    if ((o = wrapped_constraints (2))) {
        int rc = flux_jobtap_jobspec_update_pack (p,
                                                  "{s:O}",
                                                  "attributes.system.constraints",
                                                  o);
        memo (p, "shallow_update_accepted", rc == 0);
        json_decref (o);
    }

    /*  An exception type containing whitespace must be rejected, as it is
     *  when raised via the job-manager.raise RPC.
     */
    memo (p,
          "bad_exception_type_rejected",
          flux_jobtap_raise_exception (p,
                                       FLUX_JOBTAP_CURRENT_JOB,
                                       "bad type",
                                       7,
                                       "test") < 0);

    /*  As must an out of range severity
     */
    memo (p,
          "bad_exception_severity_rejected",
          flux_jobtap_raise_exception (p,
                                       FLUX_JOBTAP_CURRENT_JOB,
                                       "test",
                                       99,
                                       "test") < 0);

    /*  An event name containing whitespace must be rejected
     */
    memo (p,
          "bad_event_name_rejected",
          flux_jobtap_event_post_pack (p,
                                       FLUX_JOBTAP_CURRENT_JOB,
                                       "bad name",
                                       "{}") < 0);

    /*  As must an event with a deeply nested context
     */
    if ((o = wrapped_constraints (200))) {
        int rc = flux_jobtap_event_post_pack (p,
                                              FLUX_JOBTAP_CURRENT_JOB,
                                              "test-event",
                                              "{s:O}",
                                              "context",
                                              o);
        memo (p, "deep_event_context_rejected", rc < 0);
        json_decref (o);
    }

    /*  A normal event must still be accepted
     */
    memo (p,
          "normal_event_accepted",
          flux_jobtap_event_post_pack (p,
                                       FLUX_JOBTAP_CURRENT_JOB,
                                       "test-event",
                                       "{s:i}",
                                       "value",
                                       1) == 0);

    /*  An empty or overlong dependency description must be rejected
     */
    memo (p,
          "empty_dependency_rejected",
          flux_jobtap_dependency_add (p, FLUX_JOBTAP_CURRENT_JOB, "") < 0);
    {
        char buf[512];
        memset (buf, 'x', sizeof (buf) - 1);
        buf[sizeof (buf) - 1] = '\0';
        memo (p,
              "long_dependency_rejected",
              flux_jobtap_dependency_add (p,
                                          FLUX_JOBTAP_CURRENT_JOB,
                                          buf) < 0);
    }

    return 0;
}

/*  Return deeply nested annotations and a deeply nested R, both of which
 *  must be refused without disrupting the job.
 */
static int sched_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    json_t *o;

    if ((o = wrapped_constraints (200))) {
        (void) flux_plugin_arg_pack (args,
                                     FLUX_PLUGIN_ARG_OUT,
                                     "{s:{s:O} s:O}",
                                     "annotations",
                                       "deep", o,
                                     "R", o);
        json_decref (o);
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.depend", depend_cb, NULL },
    { "job.state.sched", sched_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_register (p, "validate-limits", tab);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
