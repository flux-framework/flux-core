/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsdprocess/strv.h"

static void test_corner_case (void)
{
    char **strv;
    char **strvcpy;
    char *tmp[] = { "a", "b", NULL };
    int ret;

    strv = strv_create ("a string", NULL);
    ok (strv == NULL && errno == EINVAL,
        "strv_create fails with EINVAL on NULL delim");

    ret = strv_copy (tmp, NULL);
    ok (ret < 0 && errno == EINVAL,
        "strv_copy fails with EINVAL on NULL copy pointer");

    ret = strv_copy (NULL, &strvcpy);
    ok (ret < 0 && errno == EINVAL,
        "strv_copy fails with EINVAL on NULL strv");

    /* strv_destroy() won't segfault on NULL pointer */
    strv_destroy (NULL);
}

static void test_strv_len (char **strv, int expected_len)
{
    int len = 0;
    char **ptr = strv;
    assert (strv);
    while (*ptr) {
        len++;
        ptr++;
    }
    ok (expected_len == len,
        "strv length is expected length %d", expected_len);
}

static void test_strv_values (char **strv, char **expected_strv)
{
    char **strv_ptr = strv;
    char **expected_strv_ptr = expected_strv;
    int index = 0;
    while (*(expected_strv_ptr)) {
        int ret = strcmp ((*strv_ptr), (*expected_strv_ptr));
        ok (ret == 0,
            "strv[%d] matches expected value %s", index, (*expected_strv_ptr));
        strv_ptr++;
        expected_strv_ptr++;
        index++;
    }
    /* test NULL delim */
    ok ((*strv_ptr) == NULL, "strv[%d]: last value in strv is NULL", index);
}

static void strv_create_test (char *str,
                              char **expected_strv,
                              int expected_len)
{
    char **strv;
    diag("strv_create test %s", str);
    strv = strv_create (str, " ");
    ok (strv != NULL, "strv_create success");

    test_strv_len (strv, expected_len);
    test_strv_values (strv, expected_strv);
    strv_destroy (strv);
}

struct strv_test_data {
    char *test_str;
    char *expected_strv[5];
    int expected_len;
} strv_tests[] = {
    { "",            { NULL },                      0},
    { "foo",         { "foo", NULL },               1},
    { "foo bar",     { "foo", "bar", NULL },        2},
    { "foo bar baz", { "foo", "bar", "baz", NULL }, 3},
    { NULL,          { NULL },                      0},
};

static void test_strv_create (void)
{
    struct strv_test_data *ptr = &strv_tests[0];
    while (ptr->test_str) {
        strv_create_test (ptr->test_str,
                          ptr->expected_strv,
                          ptr->expected_len);
        ptr++;
    }
}

static void strv_copy_test (char *str,
                            char **expected_strv,
                            int expected_len)
{
    char **strv = NULL;
    char **strvcpy = NULL;
    int ret;

    diag("strv_copy test %s", str);
    strv = strv_create (str, " ");
    ok (strv != NULL, "strv_create success");

    ret = strv_copy (strv, &strvcpy);
    ok (ret == 0, "strv_copy success on %s", str);

    test_strv_len (strvcpy, expected_len);
    test_strv_values (strvcpy, expected_strv);
    strv_destroy (strv);
    strv_destroy (strvcpy);
}

static void test_strv_copy (void)
{
    struct strv_test_data *ptr = &strv_tests[0];
    while (ptr->test_str) {
        strv_copy_test (ptr->test_str,
                        ptr->expected_strv,
                        ptr->expected_len);
        ptr++;
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_corner_case ();
    test_strv_create ();
    test_strv_copy ();

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
