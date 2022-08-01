/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/strstrip.h"

struct str_test {
    const char *input;
    const char *printable;
    const char *expected;
};

struct str_test tests[] = {
    { "",               "",                 ""  },
    { "   ",            "",                 ""  },
    { "\t",             "",                 ""  },
    { "a",              "a",                "a" },
    { "no thing",       "no thing",         "no thing" },
    { "   no thing",    "   no thing",      "no thing" },
    { "   no thing\n",  "   no thing\\n",   "no thing" },
    { "   no thing  \n","   no thing \\n",  "no thing" },
    { "a     ",         "     a",           "a" },
    { "\na   ",         "\\n  a",           "a" },
    { NULL, NULL, NULL },
};

int main(int argc, char** argv)
{
    struct str_test *st = tests;

    plan (NO_PLAN);

    ok (strstrip (NULL) == NULL && errno == EINVAL,
        "strstrip (NULL) returns EINVAL");
    ok (strstrip_copy (NULL) == NULL && errno == EINVAL,
        "strstrip_copy (NULL) returns EINVAL");

    while (st->input != NULL) {
        char *result;
        char *s = strdup (st->input);
        if (!s)
            BAIL_OUT ("Failed to copy input string %s", st->input);

        /* strstrip() */
        ok ((result = strstrip (s)) != NULL,
            "strstrip (\"%s\")",
            st->printable);
        is (result, st->expected,
           "got expected result");
        free (s);

        /* strstrip_copy() */
        if (!(s = strdup (st->input)))
            BAIL_OUT ("Failed to copy input string %s", st->input);
        ok ((result = strstrip_copy (s)) != NULL,
            "strstrip_copy (\"%s\")",
            st->printable);
        is (result, st->expected,
           "got expected result");
        is (s, st->input,
           "original string unmodified");
        free (s);
        free (result);

        st++;
    }

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
