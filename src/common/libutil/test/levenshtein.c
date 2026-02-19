/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/levenshtein.h"

void test_corner_cases (void)
{
    /* Test NULL inputs */
    ok (levenshtein_distance (NULL, "abc") == -1,
        "distance(NULL, \"abc\") returns -1");
    ok (levenshtein_distance ("abc", NULL) == -1,
        "distance(\"abc\", NULL) returns -1");
    ok (levenshtein_distance (NULL, NULL) == -1,
        "distance(NULL, NULL) returns -1");
}

void test_basics (void)
{
    /* Test empty strings */
    ok (levenshtein_distance ("", "") == 0,
        "distance(\"\", \"\") == 0");
    ok (levenshtein_distance ("", "a") == 1,
        "distance(\"\", \"a\") == 1");
    ok (levenshtein_distance ("a", "") == 1,
        "distance(\"a\", \"\") == 1");

    /* Test identical strings */
    ok (levenshtein_distance ("a", "a") == 0,
        "distance(\"a\", \"a\") == 0");
    ok (levenshtein_distance ("abc", "abc") == 0,
        "distance(\"abc\", \"abc\") == 0");

    /* Test insertions */
    ok (levenshtein_distance ("a", "ab") == 1,
        "distance(\"a\", \"ab\") == 1");
    ok (levenshtein_distance ("b", "ab") == 1,
        "distance(\"b\", \"ab\") == 1");

    /* Test deletions */
    ok (levenshtein_distance ("ab", "a") == 1,
        "distance(\"ab\", \"a\") == 1");
    ok (levenshtein_distance ("ab", "b") == 1,
        "distance(\"ab\", \"b\") == 1");

    /* Test substitutions */
    ok (levenshtein_distance ("a", "b") == 1,
        "distance(\"a\", \"b\") == 1");
    ok (levenshtein_distance ("abc", "abd") == 1,
        "distance(\"abc\", \"abd\") == 1");

    /* Test multiple operations */
    ok (levenshtein_distance ("kitten", "sitting") == 3,
        "distance(\"kitten\", \"sitting\") == 3");
    ok (levenshtein_distance ("saturday", "sunday") == 3,
        "distance(\"saturday\", \"sunday\") == 3");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    test_corner_cases ();
    test_basics ();
    done_testing ();
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
