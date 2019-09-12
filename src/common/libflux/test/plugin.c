/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <errno.h>

#include "src/common/libflux/plugin.h"
#include "src/common/libtap/tap.h"


/* function prototype for invalid args testing below */
static int foo (flux_plugin_t *p, const char *name, flux_plugin_arg_t *args)
{
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}",
                                 "fn", "foo");
}

static int bar (flux_plugin_t *p, const char *name, flux_plugin_arg_t *args)
{
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}",
                                 "fn", "bar");
}

static const struct flux_plugin_handler tab[] = {
    { "foo.*", foo },
    { "*",     bar },
    { NULL,    NULL}
};

void test_invalid_args ()
{
    flux_plugin_t *p;
    int i;

    lives_ok ({flux_plugin_destroy (NULL);},
              "flux_plugin_destroy (NULL) does not crash program");
    if (!(p = flux_plugin_create ()))
        BAIL_OUT ("flux_plugin_create failed");

    ok (flux_plugin_set_name (NULL, NULL) < 0 && errno == EINVAL,
        "flux_plugin_set_name (NULL, NULL) returns EINVAL");
    ok (flux_plugin_set_name (p, NULL) < 0 && errno == EINVAL,
        "flux_plugin_set_name (p, NULL) returns EINVAL");

    ok (flux_plugin_get_name (NULL) == NULL && errno == EINVAL,
        "flux_plugin_get_name (NULL) returns EINVAL");

    ok (flux_plugin_set_conf (NULL, NULL) < 0 && errno == EINVAL,
        "flux_plugin_set_conf (NULL, NULL) returns EINVAL");
    ok (flux_plugin_set_conf (p, NULL) < 0 && errno == EINVAL,
        "flux_plugin_set_conf (NULL, NULL) returns EINVAL");
    ok (flux_plugin_set_conf (p, "a") < 0 && errno == EINVAL,
        "flux_plugin_set_conf (NULL, NULL) returns EINVAL");
    like (flux_plugin_strerror (p), "^parse error: col 1:.*",
        "flux_plugin_last_error returns error text");

    ok (flux_plugin_conf_unpack (p, "{s:i}", "bar", &i) < 0 && errno == ENOENT,
        "flux_plugin_conf_unpack () with no conf returns ENOENT");

    ok (flux_plugin_set_conf (p, "{\"foo\":1, \"bar\":\"a\"}") == 0,
        "flux_plugin_set_conf() works");

    ok (flux_plugin_conf_unpack (NULL, NULL) < 0 && errno == EINVAL,
        "flux_conf_unpack (NULL, NULL) returns EINVAL");
    ok (flux_plugin_conf_unpack (p, NULL) < 0 && errno == EINVAL,
        "flux_conf_unpack (p, NULL) returns EINVAL");

    ok (flux_plugin_conf_unpack (p, "{s:i}", "bar", &i) < 0 && errno == EINVAL,
        "flux_conf_unpack with wrong fmt (%s) got EINVAL",
        flux_plugin_strerror (p), errno);

    ok (flux_plugin_aux_set (p, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_plugin_aux_set (p, NULL, NULL, NULL) returns EINVAL");
    ok (flux_plugin_aux_get (p, NULL) == NULL && errno == EINVAL,
        "flux_plugin_aux_get (p, NULL) returns EINVAL");
    ok (flux_plugin_aux_get (p, "foo") == NULL && errno == ENOENT,
        "flux_plugin_aux_get (p, 'foo') returns ENOENT");

    ok (flux_plugin_add_handler (NULL, "foo.*", foo) < 0 && errno == EINVAL,
        "flux_plugin_add_handler (NULL, ...) returns EINVAL");
    ok (flux_plugin_add_handler (p, NULL, foo) < 0 && errno == EINVAL,
        "flux_plugin_add_handler (p, NULL, foo) returns EINVAL");

    ok (flux_plugin_remove_handler (NULL, "foo.*") < 0 && errno == EINVAL,
        "flux_plugin_remove_handler (NULL, ...) returns EINVAL");
    ok (flux_plugin_remove_handler (p, NULL) < 0 && errno == EINVAL,
        "flux_plugin_remove_handler (p, NULL) returns EINVAL");

    ok (flux_plugin_register (NULL, NULL, tab) < 0 && errno == EINVAL,
        "flux_plugin_add_handlers (NULL, NULL, t) fails with EINVAL");
    ok (flux_plugin_register (p, NULL, NULL) < 0 && errno == EINVAL,
        "flux_plugin_add_handlers (p, NULL) fails with EINVAL");

    ok (flux_plugin_get_handler (NULL, NULL) == NULL && errno == EINVAL,
        "flux_plugin_get_handler (NULL, NULL) returns EINVAL");
    ok (flux_plugin_get_handler (p, NULL) == NULL && errno == EINVAL,
        "flux_plugin_get_handler (p, NULL) returns EINVAL");
    ok (flux_plugin_get_handler (NULL, "foo") == NULL && errno == EINVAL,
        "flux_plugin_get_handler (NULL, 'foo') returns EINVAL");

    ok (flux_plugin_match_handler (NULL, NULL) == NULL && errno == EINVAL,
        "flux_plugin_match_handler (NULL, NULL) returns EINVAL");
    ok (flux_plugin_match_handler (p, NULL) == NULL && errno == EINVAL,
        "flux_plugin_match_handler (p, NULL) returns EINVAL");
    ok (flux_plugin_match_handler (NULL, "foo") == NULL && errno == EINVAL,
        "flux_plugin_match_handler (NULL, 'foo') returns EINVAL");

    ok (flux_plugin_load_dso (NULL, NULL) < 0 && errno == EINVAL,
        "flux_plugin_load_dso (NULL, NULL) returns EINVAL");
    ok (flux_plugin_load_dso (p, NULL) < 0 && errno == EINVAL,
        "flux_plugin_load_dso (p, NULL) returns EINVAL");

    flux_plugin_destroy (p);
}

void test_plugin_args ()
{
    flux_plugin_arg_t *args = flux_plugin_arg_create ();
    char *s;
    int arg;

    if (!args)
        BAIL_OUT ("flux_plugin_arg_create failed");

    errno = EINVAL;
    is (flux_plugin_arg_strerror (NULL), strerror (errno),
        "flux_plugin_arg_strerror (NULL) defaults to strerror");

    ok (flux_plugin_arg_get (NULL, 0, NULL) < 0 && errno == EINVAL,
        "flux_plugin_arg_get with NULL arg returns EINVAL");
    ok (flux_plugin_arg_get (args, 0, NULL) < 0 && errno == EINVAL,
        "flux_plugin_arg_get with NULL string returns EINVAL");

    ok (flux_plugin_arg_set (NULL, 0, NULL) < 0 && errno == EINVAL,
        "flux_plugin_arg_set with NULL arg returns EINVAL");
    ok (flux_plugin_arg_set (args, 0, NULL) == 0,
        "flux_plugin_arg_set with NULL string returns success");

    ok (flux_plugin_arg_get (args, 0, &s) < 0 && errno == ENOENT,
        "flux_plugin_arg_get() returns ENOENT with no args set");
    ok (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_OUT, &s) < 0
        && errno == ENOENT,
        "flux_plugin_arg_get() returns ENOENT with no args set");
    is (flux_plugin_arg_strerror (args), "No args currently set",
        "flux_plugin_arg_strerror returns 'No args currently set'");

    /*  Test set
     */
    ok (flux_plugin_arg_set (args, FLUX_PLUGIN_ARG_IN, "{\"a\":5}") == 0,
        "flux_plugin_arg_set works");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:i}", "a", &arg) == 0,
        "flux_plugin_arg_unpack worked");
    ok (arg == 5,
        "flux_plugin_arg_unpack returned valid value for arg");

    /*  Test pack
     */
    ok (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_IN,
                              "{s:s s:i}",
                              "string", "in",
                              "int", 7) == 0,
        "flux_plugin_arg_pack inargs works");
    ok (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_IN, &s) == 0,
        "flux_plugin_arg_get now returns success");
    ok (s != NULL,
        "flux_plugin_arg_get returned json str: %s", s);
    free (s);
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:i}", "string", &arg) < 0,
        "flux_plugin_arg_unpack detects bad format: errno = %d", errno);
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:i}", "int", &arg) == 0,
        "flux_plugin_arg_unpack allows caller to get one arg");
    ok (arg == 7,
        "returned argument is valid");

    ok (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                              "{s:s s:i}",
                              "string", "out",
                              "int", 8) == 0,
        "flux_plugin_arg_pack outargs works");
    ok (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_OUT, &s) == 0,
        "flux_plugin_arg_get now returns success");
    ok (s != NULL,
        "flux_plugin_arg_get returned json str: %s", s);
    free (s);
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:i}", "int", &arg) == 0,
        "flux_plugin_arg_unpack allows caller to get one arg");
    ok (arg == 8,
        "returned argument is valid");

    flux_plugin_arg_destroy (args);
}

/* Accumulate result of "add" or "multiply" in arg "a",
 * result is "a" op "b".
 */
int op1 (flux_plugin_t *p, const char *topic, flux_plugin_arg_t *args)
{
    int a, b;
    if (flux_plugin_arg_unpack (args, 0, "{s:i s:i}", "a", &a, "b", &b) < 0)
        return -1;
    if (strcmp (topic, "op.add") == 0)
        a += b;
    else if (strcmp (topic, "op.multiply") == 0)
        a *= b;
    else {
        errno = ENOTSUP;
        return -1;
    }
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "{s:i}", "a", a) < 0)
        return -1;
    return 0;
}

void test_basic ()
{
    int a, b;
    flux_plugin_t *p = flux_plugin_create ();
    flux_plugin_arg_t *args = flux_plugin_arg_create ();
    if (!p || !args)
        BAIL_OUT ("flux_plugin_{args_}create failed");

    ok (flux_plugin_set_name (p, "op") == 0,
        "flux_plugin_set_name works");
    is (flux_plugin_get_name (p), "op",
        "flux_plugin_get_name() works");

    ok (flux_plugin_add_handler (p, "foo.*", NULL) == 0,
        "flux_plugin_add_handler (p, 'foo.*', NULL) works");
    ok (flux_plugin_get_handler (p, "foo.*") == NULL,
        "flux_plugin_get_handler (p, 'foo.*') returns NULL");

    ok (flux_plugin_add_handler (p, "op.*", op1) == 0,
        "flux_plugin_add_handler() works");
    ok (flux_plugin_get_handler (p, "op.*") == op1,
        "flux_plugin_get_handler (p, 'op.*') returns op1");
    ok (flux_plugin_match_handler (p, "op.add") == op1,
        "flux_plugin_match_handler (p, 'op.add') returns op1");

    a = 2;
    b = 4;
    ok (flux_plugin_arg_pack (args, 0, "{s:i s:i}", "a", a, "b", b) == 0,
        "flux_plugin_arg_pack works");

    ok (flux_plugin_call (p, "op.add", args) == 0,
        "flux_plugin_call op.add works");

    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:i}", "a", &a) == 0,
        "flux_plugin_arg_unpack worked: %s", flux_plugin_arg_strerror (args));
    ok (a == 6,
        "callback with topic op.add worked");

    a = 2;
    ok (flux_plugin_arg_pack (args, 0, "{s:i s:i}", "a", a, "b", b) == 0,
        "flux_plugin_arg_pack works");
    ok (flux_plugin_call (p, "op.multiply", args) == 0,
        "callback with topic op.multiply worked");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:i}", "a", &a) == 0,
        "flux_plugin_arg_unpack worked");

    ok (a == 8,
        "callback with topic op.multiply worked");
    ok (flux_plugin_call (p, "op.subtract", args) < 0 && errno == ENOTSUP,
        "callback with topic op.subtract returned ENOTSUP");

    ok (flux_plugin_call (p, "foo", args) == 0,
        "callback with no match returns success and does nothing");

    flux_plugin_arg_destroy (args);
    flux_plugin_destroy (p);
}

void test_register ()
{
    const char *fn;
    flux_plugin_t *p = flux_plugin_create ();
    flux_plugin_arg_t *args = flux_plugin_arg_create ();
    if (!args)
        BAIL_OUT ("flux_plugin_arg_create()");
    if (!p)
        BAIL_OUT ("flux_plugin_create()");

    /* Destroy args along with plugin object */
    flux_plugin_aux_set (p, NULL, args, (flux_free_f) flux_plugin_arg_destroy);

    ok (flux_plugin_register (p, "test_register", tab) == 0,
        "flux_plugin_register 2 handlers works");
    ok (flux_plugin_call (p, "foo.test", args) == 0,
        "flux_plugin_call foo.test worked");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "fn", &fn) == 0,
        "flux_plugin_args_unpack result worked");
    is (fn, "foo",
        "flux_plugin_call foo.test called handler foo()");

    ok (flux_plugin_call (p, "fallthru", args) == 0,
        "flux_plugin_call fallthru worked");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "fn", &fn) == 0,
        "flux_plugin_args_unpack result worked");
    is (fn, "bar",
        "flux_plugin_call 'fallthru' fell through to handler bar()");

    flux_plugin_destroy (p);
}

void test_load ()
{
    char *out;
    const char *result;
    flux_plugin_t *p = flux_plugin_create ();
    if (!p)
        BAIL_OUT ("flux_plugin_create");

    ok (flux_plugin_load_dso (p, "/noexist") < 0 && errno == ENOENT,
        "flux_plugin_load_dso on nonexistent path returns ENOENT");
    is (flux_plugin_strerror (p), "/noexist: No such file or directory",
        "flux_plugin_strerror returns expected result");
    ok (flux_plugin_load_dso (p, "/tmp") < 0,
        "flux_plugin_load_dso on directory fails");
    like (flux_plugin_strerror (p), "^dlopen: .*Is a directory",
        "flux_plugin_strerror returns expected result");

    ok (flux_plugin_set_conf (p, "{\"foo\":\"bar\"}") == 0,
        "flux_plugin_set_conf (): %s", flux_plugin_strerror (p));
    ok (flux_plugin_load_dso (p, "test/.libs/plugin_foo.so") == 0,
        "flux_plugin_load worked");
    is (flux_plugin_get_name (p), "plugin-test",
        "loaded dso registered its own name");

    flux_plugin_arg_t *args = flux_plugin_arg_create ();
    if (!args)
        BAIL_OUT ("flux_plugin_arg_create failed");
    ok (flux_plugin_call (p, "test.foo", args) == 0,
        "flux_plugin_call (test.foo) success");
    ok (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "result", &result) == 0,
        "flux_plugin_args_unpack result");
    is (result, "foo",
        "call of test.foo set result foo");

    result = NULL;
    ok (flux_plugin_arg_get (args, FLUX_PLUGIN_ARG_OUT, &out) == 0,
        "flux_plugin_arg_out works");
    diag ("out = %s", out);
    free (out);
    ok (flux_plugin_call (p, "test.bar", args) == 0,
        "flux_plugin_call (test.bar) success");
    ok (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_OUT,
                                "{s:s}", "result", &result) == 0,
        "flux_plugin_args_unpack result");
    is (result, "bar",
        "call of test.bar set result bar");

    flux_plugin_arg_destroy (args);
    flux_plugin_destroy (p);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    test_invalid_args ();
    test_plugin_args ();
    test_basic ();
    test_register ();
    test_load ();
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

