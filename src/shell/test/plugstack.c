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
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "plugstack.h"

static int called_foo = 0;
static int called_bar = 0;

static int foo (flux_plugin_t *p, const char *s,
                flux_plugin_arg_t *args, void *arg)
{
    called_foo++;
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "called foo");
}

static int bar (flux_plugin_t *p, const char *s,
                flux_plugin_arg_t *args, void *arg)
{
    called_bar++;
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "called bar");
}

static int next_level (flux_plugin_t *p, const char *s,
                       flux_plugin_arg_t *args, void *arg)
{
    struct plugstack *st = arg;
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT|FLUX_PLUGIN_ARG_UPDATE,
                                 "{s:s}",
                                 "next_name", plugstack_current_name (st));
}

static int check_name (flux_plugin_t *p, const char *s,
                       flux_plugin_arg_t *args, void *arg)
{
    struct plugstack *st = arg;
    int rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_OUT,
                                   "{s:s}",
                                   "name", plugstack_current_name (st));
    ok (rc == 0,
        "in check_name: flux_plugin_arg_pack worked");

    /* Check a recursive call to plugstack_call () */
    return plugstack_call (st, "next.level", args);
}

void test_invalid_args (struct plugstack *st, flux_plugin_t *p)
{
    ok (plugstack_push (NULL, p) < 0 && errno == EINVAL,
        "plugstack_push (NULL, p) returns EINVAL");
    ok (plugstack_push (st, NULL) < 0 && errno == EINVAL,
        "plugstack_push (st, NULL) returns EINVAL");

    ok (plugstack_load (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "plugstack_load (NULL, NULL, NULL, NULL) returns EINVAL");
    ok (plugstack_load (st, NULL, NULL) < 0 && errno == EINVAL,
        "plugstack_load (st, NULL, NULL, NULL) returns EINVAL");

    ok (plugstack_set_searchpath (NULL, NULL) < 0 && errno == EINVAL,
        "plugstack_set_searchpath (NULL, NULL) returns EINVAL");
    errno = 0;
    ok (plugstack_get_searchpath (NULL) == NULL && errno == EINVAL,
        "plugstack_get_searchpath (NULL) sets errno to EINVAL");
    ok (plugstack_plugin_aux_set (NULL, "foo", NULL) < 0 && errno == EINVAL,
        "plugstack_plugin_aux_set (NULL, ...) returns EINVAL");
    ok (plugstack_plugin_aux_set (st, NULL, NULL) < 0 && errno == EINVAL,
        "plugstack_plugin_aux_set (NULL, ...) returns EINVAL");
    ok (plugstack_current_name (NULL) == NULL && errno == EINVAL,
        "plugstack_current_name (NULL) returns EINVAL");
}

void test_load (void)
{
    const char *searchpath = "./test/a/.libs:./test/b/.libs:./test/c/.libs";
    const char *result = NULL;
    const char *aux = NULL;
    struct plugstack  *st = NULL;
    flux_plugin_arg_t *args = NULL;

    if (!(st = plugstack_create ()))
        BAIL_OUT ("plugstack_create");
    if (!(args = flux_plugin_arg_create ()))
        BAIL_OUT ("flux_plugin_arg_create");

    ok (plugstack_get_searchpath (st) == NULL,
        "plugstack searchpath is initially unset");

    ok (plugstack_load (st, "./*.noexist", NULL) == 0,
        "plugstack_load (st, \"noexist\", NULL) returns 0");
    ok (plugstack_load (st, "/tmp", NULL) < 0,
        "plugstack_load (st, \"/tmp\", NULL) returns -1");

    ok (plugstack_load (st, "./test/a/.libs/*.so", NULL) == 1,
        "plugstack_load works without searchpath");
    ok (plugstack_load (st, "./test/a/.libs/*.so", NULL) == 1,
        "plugstack_load works without searchpath");
    ok (plugstack_load (st, "./test/a/.libs/*.so", "a") < 0,
        "plugstack_load with invalid JSON conf fails");

    ok (plugstack_set_searchpath (st, searchpath) == 0,
        "plugstack_set_searchpath worked");
    is (plugstack_get_searchpath (st), searchpath,
        "plugstack_get_searchpath now returns search path");
    ok (plugstack_load (st, "./test/c/.libs/*.so", NULL) == 1,
        "plugstack_load still loads single plugin with explicit pattern");
    ok (plugstack_call (st, "test.run", args) == 0,
        "plugstack_call test.run");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s s:n}",
                                 "result", &result,
                                 "aux") == 0,
        "plugin set result in output args");
    is (result, "C",
        "plugstack correctly called callback in 'c'");

    ok (plugstack_plugin_aux_set (st, "test", "test") == 0,
        "plugstack_plugin_aux_set works");

    ok (plugstack_load (st, "*.so", NULL) == 3,
        "plugstack load works with searchpath");
    ok (plugstack_call (st, "test.run", args) == 0,
        "plugstack_call test.run");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s s:s}",
                                 "result", &result,
                                 "aux", &aux) == 0,
        "plugin set result in output args");
    is (result, "A",
        "plugstack correctly called callback in 'a'");
    is (aux, "test",
        "plugstack supplied aux == 'test' to plugin");

    plugstack_destroy (st);
    flux_plugin_arg_destroy (args);
}

int main (int argc, char **argv)
{
    const char *result;
    struct plugstack *st = NULL;
    flux_plugin_t *p1 = NULL;
    flux_plugin_t *p2 = NULL;
    flux_plugin_t *p3 = NULL;
    flux_plugin_arg_t *args = NULL;

    plan (NO_PLAN);

    if (!(st = plugstack_create ()))
        BAIL_OUT ("plugstack_create");

    if (!(p1 = flux_plugin_create ())
        || !(p2 = flux_plugin_create ())
        || !(p3 = flux_plugin_create ()))
        BAIL_OUT ("flux_plugin_create");

    test_invalid_args (st, p1);

    ok (flux_plugin_set_name (p1, "mikey") == 0,
       "flux_plugin_set_name (p1, 'mikey')");
    ok (flux_plugin_set_name (p2, "mikey") == 0,
       "flux_plugin_set_name (p2, 'mikey')");
    ok (flux_plugin_set_name (p3, "joey") == 0,
       "flux_plugin_set_name (p3, 'joey')");

    ok (flux_plugin_add_handler (p1, "callback", foo, NULL) == 0,
        "flux_plugin_add_handler (p1, 'callback', &foo)");
    ok (flux_plugin_add_handler (p1, "check.name", check_name, st) == 0,
        "flux_plugin_add_handler (p1, 'check.name', &check_name)");
    ok (flux_plugin_add_handler (p2, "callback", bar, NULL) == 0,
        "flux_plugin_add_handler (p2, 'callback', &bar)");
    ok (flux_plugin_add_handler (p3, "callback", bar, NULL) == 0,
        "flux_plugin_add_handler (p3, 'callback', &bar)");
    ok (flux_plugin_add_handler (p3, "next.level", next_level, st) == 0,
        "flux_plugin_add_handler (p3, 'next.level', &next_level)");

    if (!(args = flux_plugin_arg_create ()))
        BAIL_OUT ("flux_plugin_args_create");

    ok (plugstack_push (st, p1) == 0,
        "plugstack_push (st, p1)");
    ok (plugstack_call (st, "callback", args) == 0,
        "plugstack_call (st, 'callback')");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "result", &result) == 0,
        "flux_plugin_arg_unpack");
    is (result, "called foo",
        "plugstack_call called foo()");
    ok (called_foo == 1,
        "called foo() one time");

   called_foo = 0;
    called_bar = 0;
    ok (plugstack_push (st, p3) == 0,
        "plugstack_push (st, p3)");
    ok (plugstack_call (st, "callback", args) == 0,
        "plugstack_call with 2 plugins in stack");
    ok (called_foo == 1 && called_bar == 1,
        "plugstack_call invoked both foo() and bar()");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "result", &result) == 0,
        "flux_plugin_arg_unpack");
    is (result, "called bar",
        "plugstack_call called bar() last");

    /*  Check plugin_current_name() and recursive plugstack_call()
     *   between two plugins.
     */
    ok (plugstack_call (st, "check.name", args) == 0,
        "plugstack_call (st, 'check.name')");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "name", &result) == 0,
        "flux_plugin_arg_unpack");
    is (result, "mikey",
        "plugstack_current_name() worked");

    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "next_name", &result) == 0,
        "flux_plugin_arg_unpack");
    is (result, "joey",
        "plugstack_current_name() worked");

    ok (plugstack_current_name (st) == NULL,
        "plugstack_current_name() outside of plugstack_call returns NULL");

    called_foo = 0;
    called_bar = 0;
    ok (plugstack_push (st, p2) == 0,
        "plugstack_push (st, p2) (plugin with same name)");
    ok (plugstack_call (st, "callback", args) == 0,
        "plugstack call with 3 plugins in stack");
    ok (called_bar == 2 && called_foo == 0,
        "plugstack_call didn't call foo() only bar()");

    plugstack_destroy (st);
    flux_plugin_arg_destroy (args);

    test_load ();
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
