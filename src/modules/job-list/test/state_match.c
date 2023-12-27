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
#include "src/modules/job-list/state_match.h"
#include "ccan/str/str.h"

static void state_constraint_create_corner_case (const char *str,
                                                 const char *fmt,
                                                 ...)
{
    struct state_constraint *c;
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

    c = state_constraint_create (jc, &error);
    ok (c == NULL, "state_constraint_create fails on %s", buf);
    diag ("error: %s", error.text);
    json_decref (jc);
}

static void test_corner_case (void)
{
    ok (state_match (0, NULL) == false,
        "state_match returns false on NULL inputs");

    state_constraint_create_corner_case ("{\"userid\":[1], \"name\":[\"foo\"] }",
                                         "object with too many keys");
    state_constraint_create_corner_case ("{\"userid\":1}",
                                         "object with values not array");
    state_constraint_create_corner_case ("{\"foo\":[1]}",
                                         "object with invalid operation");
    state_constraint_create_corner_case ("{\"not\":[1]}",
                                         "sub constraint not a constraint");
}

/* expected array - expected values for
 * FLUX_JOB_STATE_DEPEND
 * FLUX_JOB_STATE_PRIORITY
 * FLUX_JOB_STATE_SCHED
 * FLUX_JOB_STATE_RUN
 * FLUX_JOB_STATE_CLEANUP
 * FLUX_JOB_STATE_INACTIVE
 * FLUX_JOB_STATE_PENDING
 * FLUX_JOB_STATE_RUNNING
 * FLUX_JOB_STATE_ACTIVE
 */
struct state_match_constraint_test {
    const char *constraint;
    bool expected[9];
} state_match_tests[] = {
    /*
     * Empty values tests
     */
    {
        "{ \"states\": [ ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"and\": [ ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"or\": [ ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"not\": [ ] }",
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        }
    },
    /*
     * Simple states tests
     */
    {
        "{ \"states\": [ \"pending\" ] }",
        {
            true,
            true,
            true,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    {
        "{ \"and\": [ { \"states\": [ \"pending\" ] } ] }",
        {
            true,
            true,
            true,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    {
        "{ \"or\": [ { \"states\": [ \"pending\" ] } ] }",
        {
            true,
            true,
            true,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    {
        "{ \"not\": [ { \"states\": [ \"pending\" ] } ] }",
        {
            false,
            false,
            false,
            true,
            true,
            true,
            false,
            true,
            true,
        }
    },
    /*
     * Simple results tests
     */
    /* N.B. "results" assumes job state == INACTIVE */
    {
        "{ \"results\": [ \"completed\" ] }",
        {
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            false,
            false,
        }
    },
    /* N.B. Returning 'true' for FLUX_JOB_STATE_INACTIVE may be
     * surprising here.  If the job state is FLUX_JOB_STATE_INACTIVE,
     * the result of "results=COMPLETED" is "maybe true", b/c it
     * depends on the actual result.  So the "not" of a "maybe true"
     * is still "maybe true".
     */
    {
        "{ \"not\": [ { \"results\": [ \"completed\" ] } ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    /*
     * Simple timestamp tests
     */
    {
        "{ \"t_submit\": [ 100.0 ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"t_run\": [ \">100.0\" ] }",
        {
            false,
            false,
            false,
            true,
            true,
            true,
            false,
            true,
            true,
        }
    },
    /* N.B. For state depend, priority, sched, is always false, so not
     * makes it always true.  For states run, cleanup, and inactive is
     * maybe true, so not maybe true = true.  So all would return
     * true.
     */
    {
        "{ \"not\": [ { \"t_run\": [ \"<=500\" ] } ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    /*
     * AND tests w/ states
     */
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"states\": [ \"priority\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"and\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"states\": [ \"priority\" ] } \
               ] \
             } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"and\": \
           [ \
             { \"not\": [ { \"states\": [ \"depend\" ] } ] }, \
             { \"states\": [ \"priority\" ] } \
           ] \
        }",
        {
            false,
            true,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    /*
     * OR tests w/ states
     */
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"states\": [ \"priority\" ] } \
           ] \
        }",
        {
            true,
            true,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"or\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"states\": [ \"priority\" ] } \
               ] \
             } \
           ] \
        }",
        {
            false,
            false,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"or\": \
           [ \
             { \"not\": [ { \"states\": [ \"depend\" ] } ] }, \
             { \"states\": [ \"priority\" ] } \
           ] \
        }",
        {
            false,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    /*
     * AND tests w/ states & results
     */
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"results\": [ \"completed\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"and\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"results\": [ \"completed\" ] } \
               ] \
             } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"not\": [ { \"results\": [ \"completed\" ] } ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    /*
     * OR tests w/ states & results
     */
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"results\": [ \"completed\" ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            true,
            true,
            false,
            true,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"or\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"results\": [ \"completed\" ] } \
               ] \
             } \
           ] \
        }",
        {
            false,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"not\": [ { \"results\": [ \"completed\" ] } ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    /*
     * AND tests w/ states & t_inactive
     */
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"t_inactive\": [ \">=100.0\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"and\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"t_inactive\": [ \">=100.0\" ] } \
               ] \
             } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"not\": [ { \"t_inactive\": [ \">=100.0\" ] } ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        }
    },
    /*
     * OR tests w/ states & t_inactive
     */
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"t_inactive\": [ \">=100.0\" ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            true,
            true,
            false,
            true,
        }
    },
    {
        "{ \"not\": \
           [ \
             { \"or\": \
               [ \
                 { \"states\": [ \"depend\" ] }, \
                 { \"t_inactive\": [ \">=100.0\" ] } \
               ] \
             } \
           ] \
        }",
        {
            false,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"not\": [ { \"t_inactive\": [ \">=100.0\" ] } ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        }
    },
    /*
     * Simple non-states tests
     */
    {
        "{ \"userid\": [ 42 ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"not\": [ { \"userid\": [ 42 ] } ] }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    /*
     * non-states AND tests
     */
    {
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
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
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    /*
     * non-states OR tests
     */
    {
        "{ \"or\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
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
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    /*
     * states and non-states AND tests
     */
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"running\" ] }, \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            true,
            true,
            false,
            false,
            true,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"not\": [ { \"states\": [ \"running\" ] } ] }, \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            false,
            false,
            true,
            true,
            false,
            true,
        },
    },
    /* N.B. All returning true may be difficult to understand here.
     * The states check is effectively irrelevant.  The userid or name
     * could could be false, leading to the "and" constraint
     * potentially being false for any job state.  So the full
     * constraint could be true for any job state.
     */
    {
        "{ \"not\": \
           [ \
             { \"and\": \
               [ \
                 { \"states\": [ \"running\" ] }, \
                 { \"userid\": [ 42 ] }, \
                 { \"name\": [ \"foo\" ] } \
               ] \
             } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    /*
     * states and non-states OR tests
     */
    /* N.B. All states return true here, b/c the states check is sort
     * of irrelevant, the userid or name checks could always return
     * true, leading to the or statement to be true that any state
     * could be matched with this constraint.
     */
    {
        "{ \"or\": \
           [ \
             { \"states\": [ \"running\" ] }, \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"or\": \
           [ \
             { \"not\": [ { \"states\": [ \"running\" ] } ] }, \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"not\": \
           [ \
             { \"or\": \
               [ \
                 { \"states\": [ \"running\" ] }, \
                 { \"userid\": [ 42 ] }, \
                 { \"name\": [ \"foo\" ] } \
               ] \
             } \
           ] \
        }",
        {
            true,
            true,
            true,
            false,
            false,
            true,
            true,
            false,
            true,
        },
    },
    /*
     * complex tests, conditionals inside conditionals
     */
    {
        "{ \"and\": \
           [ \
             { \"and\": \
               [ \
                 { \"states\": [ \"priority\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            false,
            true,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"or\": \
               [ \
                 { \"states\": [ \"priority\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"and\": \
               [ \
                 { \"results\": [ \"completed\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            false,
            false,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"or\": \
               [ \
                 { \"results\": [ \"completed\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"or\": \
               [ \
                 { \"states\": [ \"priority\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"not\": [ { \"states\": [ \"depend\" ] } ] }, \
             { \"or\": \
               [ \
                 { \"states\": [ \"priority\" ] }, \
                 { \"userid\": [ 42 ] } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            false,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
            true,
        },
    },
    {
        "{ \"and\": \
           [ \
             { \"states\": [ \"depend\" ] }, \
             { \"not\": \
               [ \
                 { \"or\": \
                   [ \
                     { \"states\": [ \"priority\" ] }, \
                     { \"userid\": [ 42 ] } \
                   ] \
                 } \
               ] \
             }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        {
            true,
            false,
            false,
            false,
            false,
            false,
            true,
            false,
            true,
        },
    },
    /* cover every constraint operator
     * - every test here should fail as we AND several impossible things
     */
    {
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] }, \
             { \"queue\": [ \"foo\" ] }, \
             { \"hostlist\": [ \"bar\" ] }, \
             { \"states\": [ \"running\" ] }, \
             { \"results\": [ \"completed\" ] }, \
             { \"t_submit\": [ \">=500.0\" ] }, \
             { \"t_depend\": [ \">=100.0\" ] }, \
             { \"t_run\": [ \"<=100.0\" ] }, \
             { \"t_cleanup\": [ \">=100.0\" ] }, \
             { \"t_inactive\": [ \"<=100.0\" ] } \
           ] \
        }",
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        },
    },
    {
        NULL,
        {
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
            false,
        },
    },
};

static struct state_constraint *create_state_constraint (const char *constraint)
{
    struct state_constraint *c;
    flux_error_t error;
    json_error_t jerror;
    json_t *jc = NULL;

    if (constraint) {
        if (!(jc = json_loads (constraint, 0, &jerror)))
            BAIL_OUT ("json constraint invalid: %s", jerror.text);
    }

    if (!(c = state_constraint_create (jc, &error)))
        BAIL_OUT ("constraint create fail: %s", error.text);

    json_decref (jc);
    return c;
}

static void test_state_match (void)
{
    struct state_match_constraint_test *ctests = state_match_tests;
    struct state_constraint *c;
    bool rv;
    int index = 0;

    /* First test special case */
    c = create_state_constraint (NULL);
    rv = state_match (FLUX_JOB_STATE_DEPEND, c);
    ok (rv == ctests->expected[0], "state match test NULL DEPEND");
    rv = state_match (FLUX_JOB_STATE_PRIORITY, c);
    ok (rv == ctests->expected[1], "state match test NULL PRIORITY");
    rv = state_match (FLUX_JOB_STATE_SCHED, c);
    ok (rv == ctests->expected[2], "state match test NULL SCHED");
    rv = state_match (FLUX_JOB_STATE_RUN, c);
    ok (rv == ctests->expected[3], "state match test NULL RUN");
    rv = state_match (FLUX_JOB_STATE_CLEANUP, c);
    ok (rv == ctests->expected[4], "state match test NULL CLEANUP");
    rv = state_match (FLUX_JOB_STATE_INACTIVE, c);
    ok (rv == ctests->expected[5], "state match test NULL INACTIVE");
    rv = state_match (FLUX_JOB_STATE_PENDING, c);
    ok (rv == ctests->expected[6], "state match test NULL PENDING");
    rv = state_match (FLUX_JOB_STATE_RUNNING, c);
    ok (rv == ctests->expected[7], "state match test NULL RUNNING");
    rv = state_match (FLUX_JOB_STATE_ACTIVE, c);
    ok (rv == ctests->expected[8], "state match test NULL ACTIVE");

    while (ctests->constraint) {
        c = create_state_constraint (ctests->constraint);
        rv = state_match (FLUX_JOB_STATE_DEPEND, c);
        ok (rv == ctests->expected[0], "state match test #%d DEPEND", index);
        rv = state_match (FLUX_JOB_STATE_PRIORITY, c);
        ok (rv == ctests->expected[1], "state match test #%d PRIORITY", index);
        rv = state_match (FLUX_JOB_STATE_SCHED, c);
        ok (rv == ctests->expected[2], "state match test #%d SCHED", index);
        rv = state_match (FLUX_JOB_STATE_RUN, c);
        ok (rv == ctests->expected[3], "state match test #%d RUN", index);
        rv = state_match (FLUX_JOB_STATE_CLEANUP, c);
        ok (rv == ctests->expected[4], "state match test #%d CLEANUP", index);
        rv = state_match (FLUX_JOB_STATE_INACTIVE, c);
        ok (rv == ctests->expected[5], "state match test #%d INACTIVE", index);
        rv = state_match (FLUX_JOB_STATE_PENDING, c);
        ok (rv == ctests->expected[6], "state match test #%d PENDING", index);
        rv = state_match (FLUX_JOB_STATE_RUNNING, c);
        ok (rv == ctests->expected[7], "state match test #%d RUNNING", index);
        rv = state_match (FLUX_JOB_STATE_ACTIVE, c);
        ok (rv == ctests->expected[8], "state match test #%d ACTIVE", index);

        index++;
        state_constraint_destroy (c);
        ctests++;
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_corner_case ();
    test_state_match ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
