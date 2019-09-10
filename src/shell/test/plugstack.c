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

static int foo (flux_plugin_t *p, const char *s, flux_plugin_arg_t *args)
{
    called_foo++;
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "called foo");
}

static int bar (flux_plugin_t *p, const char *s, flux_plugin_arg_t *args)
{
    called_bar++;
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "called bar");
}

void test_invalid_args (struct plugstack *st, flux_plugin_t *p)
{
    ok (plugstack_push (NULL, p) < 0 && errno == EINVAL,
        "plugstack_push (NULL, p) returns EINVAL");
    ok (plugstack_push (st, NULL) < 0 && errno == EINVAL,
        "plugstack_push (st, NULL) returns EINVAL");
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

    ok (flux_plugin_add_handler (p1, "callback", foo) == 0,
        "flux_plugin_add_handler (p1, 'callback', &foo)");
    ok (flux_plugin_add_handler (p2, "callback", &bar) == 0,
        "flux_plugin_add_handler (p2, 'callback', &bar)");
    ok (flux_plugin_add_handler (p3, "callback", &bar) == 0,
        "flux_plugin_add_handler (p3, 'callback', &bar)");

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

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
