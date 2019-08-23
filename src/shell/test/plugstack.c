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

#include <errno.h>
#include <string.h>

#include "src/common/libtap/tap.h"

#include "plugstack.h"

static char *result;
static int called_foo = 0;
static int called_bar = 0;

static int foo (char **resultp)
{
    called_foo++;
    *resultp = "called foo";
    return 0;
}

static int bar (char **resultp)
{
    called_bar++;
    *resultp = "called bar";
    return 0;
}

void test_invalid_args (struct plugstack *st, struct splugin *p)
{
    ok (plugstack_push (NULL, p) < 0 && errno == EINVAL,
        "plugstack_push (NULL, p) returns EINVAL");
    ok (plugstack_push (st, NULL) < 0 && errno == EINVAL,
        "plugstack_push (st, NULL) returns EINVAL");

    ok (splugin_set_name (NULL, "foo") < 0 && errno == EINVAL,
       "splugin_set_name (NULL, ..) returns EINVAL");
    ok (splugin_set_name (p, NULL) < 0 && errno == EINVAL,
       "splugin_set_name (p, NULL) returns EINVAL");

    ok (splugin_set_sym (NULL, "foo", &foo) < 0 && errno == EINVAL,
        "splugin_set_sym (NULL, ...) returns EINVAL");
    ok (splugin_set_sym (p, NULL, &foo) < 0 && errno == EINVAL,
        "splugin_set_sym (p, NULL, ...) returns EINVAL");

    errno = 0;
    ok (splugin_get_sym (NULL, "foo") == NULL && errno == EINVAL,
        "splugin_get_sym (NULL, ...) returns EINVAL");
    errno = 0;
    ok (splugin_get_sym (p, NULL) == NULL && errno == EINVAL,
        "splugin_get_sym (p, NULL) returns EINVAL");
}

int main (int argc, char **argv)
{
    struct plugstack *st = NULL;
    struct splugin *p1 = NULL;
    struct splugin *p2 = NULL;
    struct splugin *p3 = NULL;

    plan (NO_PLAN);

    if (!(st = plugstack_create ()))
        BAIL_OUT ("plugstack_create");

    if (!(p1 = splugin_create ())
        || !(p2 = splugin_create ())
        || !(p3 = splugin_create ()))
        BAIL_OUT ("splugin_create");

    test_invalid_args (st, p1);

    ok (splugin_set_name (p1, "mikey") == 0,
       "splugin_set_name (p1, 'mikey')");
    ok (splugin_set_name (p2, "mikey") == 0,
       "splugin_set_name (p2, 'mikey')");
    ok (splugin_set_name (p3, "joey") == 0,
       "splugin_set_name (p3, 'joey')");

    ok (splugin_set_sym (p1, "callback", NULL) == 0,
        "splugin_set_sym with NULL callback doesn't error");
    ok (splugin_get_sym (p1, "callback") == NULL,
        "splugin_get_sym (p1, 'callback') == NULL");

    ok (splugin_set_sym (p1, "callback", &foo) == 0,
        "splugin_set_sym (p1, 'callback', &foo)");
    ok (splugin_set_sym (p1, "callback", NULL) == 0,
        "splugin_set_sym with NULL callback deletes existing callback");
    ok (splugin_get_sym (p1, "callback") == NULL,
        "splugin_get_sym (p1, 'callback') == NULL");

    ok (splugin_set_sym (p1, "callback", &foo) == 0,
        "splugin_set_sym (p1, 'callback', &foo)");
    ok (splugin_get_sym (p1, "callback") == &foo,
        "splugin_get_sym (p1, 'callback') == &foo");

    ok (splugin_set_sym (p2, "callback", &bar) == 0,
        "splugin_set_sym (p2, 'callback', &bar)");
    ok (splugin_get_sym (p2, "callback") == &bar,
        "splugin_get_sym (p1, 'callback') == &foo");

    ok (splugin_set_sym (p3, "callback", &bar) == 0,
        "splugin_set_sym (p3, 'callback', &bar)");
    ok (splugin_get_sym (p3, "callback") == &bar,
        "splugin_get_sym (p3, 'callback') == &bar");

    ok (plugstack_push (st, p1) == 0,
        "plugstack_push (st, p1)");
    ok (plugstack_call (st, "callback", 1, &result) == 0,
        "plugstack_call (st, 'callback')");
    is (result, "called foo",
        "plugstack_call called foo()");
    ok (called_foo == 1,
        "called foo() one time");

    called_foo = 0;
    called_bar = 0;
    ok (plugstack_push (st, p3) == 0,
        "plugstack_push (st, p3)");
    ok (plugstack_call (st, "callback", 1, &result) == 0,
        "plugstack_call with 2 plugins in stack");
    ok (called_foo == 1 && called_bar == 1,
        "plugstack_call invoked both foo() and bar()");
    is (result, "called bar",
        "plugstack_call called bar() last");

    called_foo = 0;
    called_bar = 0;
    ok (plugstack_push (st, p2) == 0,
        "plugstack_push (st, p2) (plugin with same name)");
    ok (plugstack_call (st, "callback", 1, &result) == 0,
        "plugstack call with 3 plugins in stack");
    ok (called_bar == 2 && called_foo == 0,
        "plugstack_call didn't call foo() only bar()");

    plugstack_destroy (st);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
