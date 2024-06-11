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
#include "src/modules/job-list/constraint_sql.h"
#include "ccan/str/str.h"

static void constraint2sql_corner_case (const char *str,
                                        const char *msg)
{
    flux_error_t error;
    json_error_t jerror;
    json_t *jc;
    char *query;
    int ret;

    if (!(jc = json_loads (str, 0, &jerror)))
        BAIL_OUT ("json constraint invalid: %s", jerror.text);

    ret = constraint2sql (jc, &query, &error);

    ok (ret < 0, "constraint2sql fails on %s", msg);
    diag ("error: %s", error.text);
    json_decref (jc);
}

static void test_corner_case (void)
{
    constraint2sql_corner_case ("{\"userid\":[1], \"name\":[\"foo\"] }",
                                "object with too many keys");
    constraint2sql_corner_case ("{\"userid\":1}",
                                "object with values not array");
    constraint2sql_corner_case ("{\"foo\":[1]}",
                                "object with invalid operation");
    constraint2sql_corner_case ("{\"userid\":[\"foo\"]}",
                                "userid value not integer");
    constraint2sql_corner_case ("{\"name\":[1]}",
                                "name value not string");
    constraint2sql_corner_case ("{\"queue\":[1]}",
                                "queue value not string");
    constraint2sql_corner_case ("{\"states\":[0.0]}",
                                "states value not integer or string");
    constraint2sql_corner_case ("{\"states\":[\"foo\"]}",
                                "states value not valid string");
    constraint2sql_corner_case ("{\"states\":[8192]}",
                                "states value not valid integer");
    constraint2sql_corner_case ("{\"results\":[0.0]}",
                                "results value not integer or string");
    constraint2sql_corner_case ("{\"results\":[\"foo\"]}",
                                "results value not valid string");
    constraint2sql_corner_case ("{\"results\":[8192]}",
                                "results value not valid integer");
    constraint2sql_corner_case ("{\"t_depend\":[]}",
                                "t_depend value not specified");
    constraint2sql_corner_case ("{\"t_depend\":[1.0]}",
                                "t_depend value in invalid format (int)");
    constraint2sql_corner_case ("{\"t_depend\":[\"0.0\"]}",
                                "t_depend no comparison operator");
    constraint2sql_corner_case ("{\"t_depend\":[\">=foof\"]}",
                                "t_depend value invalid (str)");
    constraint2sql_corner_case ("{\"t_depend\":[\">=-1.0\"]}",
                                "t_depend value < 0.0 (str)");
    constraint2sql_corner_case ("{\"not\":[1]}",
                                "sub constraint not a constraint");
}

void test_constraint2sql (const char *constraint, const char *expected)
{
    flux_error_t error;
    json_error_t jerror;
    json_t *jc = NULL;
    char *query = NULL;
    char *constraint_compact = NULL;
    int ret;

    if (constraint) {
        if (!(jc = json_loads (constraint, 0, &jerror)))
            BAIL_OUT ("json constraint invalid: %s", jerror.text);
        /* b/c tests written below have spacing, alignment,
         * etc. etc. which outputs poorly */
        if (!(constraint_compact = json_dumps (jc, JSON_COMPACT)))
            BAIL_OUT ("json_dumps");
    }

    ret = constraint2sql (jc, &query, &error);

    ok (ret == 0, "constraint2sql success");
    if (ret < 0)
        diag ("error: %s", error.text);
    if (expected) {
        bool pass = query && streq (query, expected);
        ok (pass == true,
            "constraint2sql on \"%s\" success", constraint_compact);
        if (!pass)
            diag ("unexpected result: %s", query);
    }
    else {
        ok (query == NULL,
            "constraint2sql on \"%s\" success", constraint_compact);
    }
    json_decref (jc);
}

static void test_special_cases (void)
{
    test_constraint2sql (NULL, NULL);
}

struct constraint2sql_test {
    const char *constraint;
    const char *expected;
};

/* N.B. These constraints are copied from the tests in match.c */
struct constraint2sql_test tests[] = {
    /*
     * userid tests
     */
    /* matches "all", so no query result */
    {
        "{}",
        NULL,
    },
    /* no sql query possible, return is NULL */
    {
        "{ \"userid\": [ ] }",
        NULL,
    },
    {
        "{ \"userid\": [ 42 ] }",
        "(userid = 42)",
    },
    {
        "{ \"userid\": [ 42, 43 ] }",
        "(userid = 42 OR userid = 43)",
    },
    /* FLUX_USERID_UNKNOWN = 0xFFFFFFFF
     * matches "all", so no query result
     */
    {
        "{ \"userid\": [ -1 ] }",
        NULL,
    },
    /*
     * name tests
     */
    /* no sql query possible, return is NULL */
    {
        "{ \"name\": [ ] }",
        NULL,
    },
    {
        "{ \"name\": [ \"foo\" ] }",
        "(name = 'foo')",
    },
    {
        "{ \"name\": [ \"foo\", \"bar\" ] }",
        "(name = 'foo' OR name = 'bar')",
    },
    /*
     * queue tests
     */
    /* no sql query possible, return is NULL */
    {
        "{ \"queue\": [ ] }",
        NULL,
    },
    {
        "{ \"queue\": [ \"foo\" ] }",
        "(queue = 'foo')",
    },
    {
        "{ \"queue\": [ \"foo\", \"bar\" ] }",
        "(queue = 'foo' OR queue = 'bar')",
    },
    /*
     * states tests
     */
    /* matches "nothing" */
    {
        "{ \"states\": [ ] }",
        "((state & 0) > 0)",
    },
    {
        /* sanity check integer inputs work, we assume FLUX_JOB_STATE_NEW
         * will always be 1, use strings everywhere else
         */
        "{ \"states\": [ 1 ] }",
        "((state & 1) > 0)",
    },
    {
        "{ \"states\": [ \"sched\" ] }",
        "((state & 8) > 0)",
    },
    {
        "{ \"states\": [ \"sched\", \"RUN\" ] }",
        "((state & 24) > 0)",
    },
    /*
     * results tests
     */
    /* matches "nothing" */
    {
        "{ \"results\": [ ] }",
        "((result & 0) > 0)",
    },
    {
        /* sanity check integer inputs work, we assume
         * FLUX_JOB_RESULT_COMPLETED will always be 1, use strings
         * everywhere else
         */
        "{ \"results\": [ 1 ] }",
        "((result & 1) > 0)",
    },
    {
        "{ \"results\": [ \"completed\" ] }",
        "((result & 1) > 0)",
    },
    {
        "{ \"results\": [ \"completed\", \"FAILED\" ] }",
        "((result & 3) > 0)",
    },
    /*
     * hostlist tests
     *
     * N.B. hostlist cannot be converted to SQL query, so all return
     * NULL
     */
    {
        "{ \"hostlist\": [ ] }",
        NULL,
    },
    {
        "{ \"hostlist\": [ \"foo1\" ] }",
        NULL,
    },
    {
        "{ \"hostlist\": [ \"foo[1-2]\" ] }",
        NULL,
    },
    {
        "{ \"hostlist\": [ \"foo1\", \"foo2\", \"foo3\" ] }",
        NULL,
    },
    /*
     * timestamp tests
     */
    {
        "{ \"t_submit\": [ \">=0\" ] }",
        "(t_submit >= 0)",
    },
    {
        "{ \"t_depend\": [ \">=0.0\" ] }",
        "(t_depend >= 0.0)",
    },
    {
        "{ \"t_run\": [ \">=0\" ] }",
        "(t_run >= 0)",
    },
    {
        "{ \"t_cleanup\": [ \">=0.0\" ] }",
        "(t_cleanup >= 0.0)",
    },
    {
        "{ \"t_inactive\": [ \">=0.0\" ] }",
        "(t_inactive >= 0.0)",
    },
    {
        "{ \"t_inactive\": [ \"<100.0\" ] }",
        "(t_inactive < 100.0)",
    },
    {
        "{ \"t_inactive\": [ \"<=100.0\" ] }",
        "(t_inactive <= 100.0)",
    },
    {
        "{ \"t_inactive\": [ \">=100.0\" ] }",
        "(t_inactive >= 100.0)",
    },
    {
        "{ \"or\": [] }",
        NULL,
    },
    {
        "{ \"and\": [] }",
        NULL,
    },
    {
        "{ \"not\": [] }",
        NULL,
    },
    {
        "{ \"not\": [ { \"userid\": [ 42 ] } ] }",
        "(NOT ((userid = 42)))",
    },
    {
        "{ \"or\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        "((userid = 42) OR (name = 'foo'))",
    },
    {
        "{ \"or\": \
           [ \
             { \"not\": [ { \"userid\": [ 42 ] } ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        "((NOT ((userid = 42))) OR (name = 'foo'))",
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
        "(NOT (((userid = 42) OR (name = 'foo'))))",
    },
    {
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        "((userid = 42) AND (name = 'foo'))",
    },
    {
        "{ \"and\": \
           [ \
             { \"not\": [ { \"userid\": [ 42 ] } ] }, \
             { \"name\": [ \"foo\" ] } \
           ] \
        }",
        "((NOT ((userid = 42))) AND (name = 'foo'))",
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
        "(NOT (((userid = 42) AND (name = 'foo'))))",
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
        "(((userid = 42) OR (userid = 43)) AND (name = 'foo'))",
    },
    {
        /* all the jobs in all states for a specific user */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"states\": [ \"pending\", \"running\", \"inactive\" ] } \
           ] \
        }",
        "((userid = 42) AND ((state & 126) > 0))",
    },
    {
        /* all the unsuccessful jobs for a specific user */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"results\": [ \"failed\", \"canceled\", \"timeout\" ] } \
           ] \
        }",
        "((userid = 42) AND ((result & 14) > 0))",
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
        "((userid = 42) AND ((state & 62) > 0) AND (queue = 'batch' OR queue = 'debug'))",
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
        "((userid = 42) AND (queue = 'batch') AND (name = 'foo') AND ((state & 48) > 0))",
    },
    {
        /* all the inactive jobs since a specific time (via t_inactve) */
        "{ \"and\": \
           [ \
             { \"states\": [ \"inactive\" ] }, \
             { \"t_inactive\": [ \">=500.0\" ] } \
           ] \
        }",
        "(((state & 64) > 0) AND (t_inactive >= 500.0))",
    },
    {
        /* jobs for a user that ran on specific hostlist */
        /* N.B. "hostlist" can't be converted into query, so is dropped */
        "{ \"and\": \
           [ \
             { \"userid\": [ 42 ] }, \
             { \"hostlist\": [ \"node1\", \"node2\" ] } \
           ] \
        }",
        "((userid = 42))",
    },
    {
        /* jobs that ran on specific hostlist during a time period
         */
        /* N.B. "hostlist" can't be converted into query, so is dropped */
        "{ \"and\": \
           [ \
             { \"hostlist\": [ \"node1\", \"node2\" ] }, \
             { \"t_run\": [ \">=500.0\" ] }, \
             { \"t_inactive\": [ \"<=5000.0\" ] } \
           ] \
        }",
        "((t_run >= 500.0) AND (t_inactive <= 5000.0))",
    },
    {
        NULL,
        NULL,
    },
};

static void run_constraint2sql_tests (void)
{
    struct constraint2sql_test *ltests = tests;

    while (ltests->constraint) {
        test_constraint2sql (ltests->constraint, ltests->expected);
        ltests++;
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_corner_case ();
    test_special_cases ();
    run_constraint2sql_tests ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
