/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/command.h"
#include "src/common/libsubprocess/command_private.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

/*
 *  Check basic flux_cmd_create () with args
 */
void check_basic_create ()
{
    char **av;
    char * argv[] = {
        "test",
        "--option=foo",
        "bar",
        NULL
    };
    int argc = ARRAY_SIZE (argv) - 1;
    char * env[] = {
        "FOO=bar",
        "PATH=/bin",
        NULL
    };
    flux_cmd_t *cmd;

    diag ("simple flux_cmd_create (argc, argv, env)");
    cmd = flux_cmd_create (argc, argv, env);
    ok (cmd != NULL, "flux_cmd_create ()");
    av = cmd_argv_expand (cmd);
    ok (av != NULL, "cmd_argv_expand ()");
    is (av[0], "test", "av[0] == test");
    is (av[1], "--option=foo", "av[1] == --option=foo");
    is (av[2], "bar", "av[2] == bar");
    ok (av[3] == NULL, "av[3] == NULL");
    free (av);
    is (flux_cmd_getenv (cmd, "FOO"), "bar", "flux_cmd_getenv");
    is (flux_cmd_getenv (cmd, "PATH"), "/bin", "flux_cmd_getenv");

    flux_cmd_destroy (cmd);
}


void check_empty_cmd_attributes (flux_cmd_t *cmd)
{
    char **argv, **env;

    ok (flux_cmd_argc (cmd) == 0, "flux_cmd_argc");

    argv = cmd_argv_expand (cmd);
    ok (argv != NULL, "cmd_argv_expand returned an argv");
    ok (argv[0] == NULL, "argv is properly NULL terminated");
    free (argv);

    env = cmd_env_expand (cmd);
    ok (env != NULL, "cmd_env_expand works");
    ok (env[0] == NULL, "cmd_env_expand properly terminates env");
    free (env);

    ok (flux_cmd_getcwd (cmd) == NULL,
        "flux_cmd_getcwd returns NULL");
}

/*
 * Set some basic known cmd attributes for testing
 */
void set_cmd_attributes (flux_cmd_t *cmd)
{
    assert (flux_cmd_argc (cmd) == 0);

    // Append to argv
    ok (flux_cmd_argv_append (cmd, "command") >= 0,
        "flux_cmd_argv_append");
    ok (flux_cmd_argv_append (cmd, "foo") >= 0,
        "flux_cmd_argv_append");
    ok (flux_cmd_argv_appendf (cmd, "%s", "bar") >= 0,
        "flux_cmd_argv_appendf");

    // Test setenvf
    ok (flux_cmd_setenvf (cmd, 0, "PATH", "/bin:/usr/bin") >= 0,
        "flux_cmd_setenvf (PATH)");

    ok (flux_cmd_setcwd (cmd, "/tmp") >= 0,
        "flux_cmd_setcwd (/tmp)");
    ok (flux_cmd_add_channel (cmd, "MY_FD") >= 0,
        "flux_cmd_add_channel");
    ok (flux_cmd_setopt (cmd, "OPTION", "VALUE") >= 0,
        "flux_cmd_setopt");
    ok (flux_cmd_set_label (cmd, "foo") == 0,
        "flux_cmd_set_label (cmd, 'foo')");
}

/* set alternate way, to ensure alternate ways also work */
void set_cmd_attributes2 (flux_cmd_t *cmd)
{
    char *env[] = { "PATH=/bin:/usr/bin", NULL };

    ok (flux_cmd_env_replace (cmd, env) == 0,
        "cmd_set_env");
}

void check_cmd_attributes (flux_cmd_t *cmd)
{
    char **argv, **env;
    const char *arg = NULL;

    ok (flux_cmd_argc (cmd) == 3, "flux_cmd_argc");

    argv = cmd_argv_expand (cmd);
    ok (argv != NULL, "cmd_argv_expand returned an argv");
    ok (argv[3] == NULL, "argv is properly NULL terminated");
    is (argv[0], "command", "argv[0] is correct");
    is (argv[1], "foo", "argv[1] is correct");
    is (argv[2], "bar", "argv[2] is correct");
    free (argv);

    ok (flux_cmd_arg (cmd, 3) == NULL
        && errno == EINVAL,
        "flux_cmd_arg returns EINVAL on bad range");
    arg = flux_cmd_arg (cmd, 0);
    ok (arg != NULL
        && streq (arg, "command"),
        "flux_cmd_arg returns correct argv[0]");
    arg = flux_cmd_arg (cmd, 1);
    ok (arg != NULL
        && streq (arg, "foo"),
        "flux_cmd_arg returns correct argv[1]");
    arg = flux_cmd_arg (cmd, 2);
    ok (arg != NULL
        && streq (arg, "bar"),
        "flux_cmd_arg returns correct argv[2]");

    is (flux_cmd_getenv (cmd, "PATH"), "/bin:/usr/bin",
        "flux_cmd_getenv");

    env = cmd_env_expand (cmd);
    ok (env != NULL, "cmd_env_expand works");
    ok (env[1] == NULL, "cmd_env_expand properly terminates env");
    is (env[0], "PATH=/bin:/usr/bin",
        "first entry of env is as expected");
    free (env);

    is (flux_cmd_getcwd (cmd), "/tmp",
        "flux_cmd_getcwd");
    is (flux_cmd_getopt (cmd, "OPTION"), "VALUE",
        "flux_cmd_getopt (cmd, 'OPTION') == VALUE");
    is (flux_cmd_get_label (cmd), "foo",
        "flux_cmd_get_label (cmd) returns 'foo'");
}

void test_find_opts (void)
{
    flux_cmd_t *cmd;
    const char *substrings1[] = { "FOO", NULL };
    const char *substrings2[] = { "DUH", "BAZ", "UHH", NULL };
    const char *substrings3[] = { "OOPS",  NULL };
    const char *substrings4[] = { NULL };

    cmd = flux_cmd_create (0, NULL, NULL);
    ok (cmd != NULL,
        "flux_cmd_create works");

    ok (flux_cmd_setopt (cmd, "a_FOO", "val") == 0,
        "flux_cmd_setopt works");
    ok (flux_cmd_setopt (cmd, "a_BAR", "val") == 0,
        "flux_cmd_setopt works");
    ok (flux_cmd_setopt (cmd, "b_BAR", "val") == 0,
        "flux_cmd_setopt works");
    ok (flux_cmd_setopt (cmd, "a_BAZ", "val") == 0,
        "flux_cmd_setopt works");
    ok (flux_cmd_setopt (cmd, "b_BAZ", "val") == 0,
        "flux_cmd_setopt works");

    ok (cmd_find_opts (cmd, substrings1) == 1,
        "cmd_find_opts finds substrings");

    ok (cmd_find_opts (cmd, substrings2) == 1,
        "cmd_find_opts finds substrings");

    ok (cmd_find_opts (cmd, substrings3) == 0,
        "cmd_find_opts doesn't find substrings");

    ok (cmd_find_opts (cmd, substrings4) == 0,
        "cmd_find_opts doesn't find substrings");

    flux_cmd_destroy (cmd);
}

void test_stringify (void)
{
    flux_cmd_t *cmd;
    char *s;
    char * argv[] = {
        "test",
        "--option=foo",
        "-c",
        "5",
        "bar",
        NULL
    };
    int argc = ARRAY_SIZE (argv) - 1;
    char * env[] = {
        "FOO=bar",
        "PATH=/bin",
        NULL
    };

    ok ((cmd = flux_cmd_create (0, NULL, NULL)) != NULL,
        "flux_cmd_create empty");

    s = flux_cmd_stringify (cmd);
    ok (s != NULL,
        "flux_cmd_stringify on empty cmd works");
    is (s, "",
        "flux_cmd_stringify on empty cmd returns empty string");
    free (s);
    flux_cmd_destroy (cmd);

   ok ((cmd = flux_cmd_create (argc, argv, env)) != NULL,
        "flux_cmd_create");
   if (!cmd)
       BAIL_OUT ("flux_cmd_create failed");

    s = flux_cmd_stringify (cmd);
    ok (s != NULL,
        "flux_cmd_stringify works");
    is (s, "test --option=foo -c 5 bar",
        "flux_cmd_stringify returns expected string");
    free (s);
    flux_cmd_destroy (cmd);
}

void test_arg_insert_delete (void)
{
    flux_cmd_t *cmd;
    char **av;
    char * argv[] = {
        "test",
        "--option=foo",
        "-c",
        "5",
        "bar",
        NULL
    };
    int argc = ARRAY_SIZE (argv) - 1;
    char * env[] = {
        "FOO=bar",
        "PATH=/bin",
        NULL
    };

    ok ((cmd = flux_cmd_create (0, NULL, NULL)) != NULL,
        "flux_cmd_create empty");
    ok (flux_cmd_argv_delete (cmd, 0) < 0 && errno == EINVAL,
        "flux_cmd_delete 0 on empty cmd returns EINVAL");
    ok (flux_cmd_argv_insert (cmd, 0, "foo") == 0,
        "flux_cmd_insert (cmd, 0) inserts at front of empty cmd");
    ok (flux_cmd_argc (cmd) == 1,
        "flux_cmd argc == 1");
    is (flux_cmd_arg (cmd, 0), "foo",
        "flux_cmd_arg returns foo for arg0");
    flux_cmd_destroy (cmd);


    ok ((cmd = flux_cmd_create (argc, argv, env)) != NULL,
        "flux_cmd_create");
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    ok (flux_cmd_argc (cmd) == argc,
        "flux_cmd_argc == %d (expected %d)",
        flux_cmd_argc (cmd), argc);

    ok (flux_cmd_argv_delete (cmd, 10) < 0 && errno == EINVAL,
        "flux_cmd_argv_delete returns EINVAL for invalid index");

    ok (flux_cmd_argv_delete (cmd, 0) == 0,
        "flux_cmd_argv_delete first entry");
    ok (flux_cmd_argc (cmd) == argc - 1,
        "flux_cmd_argc is now %d (expected %d)",
        flux_cmd_argc (cmd), argc - 1);
    ok (flux_cmd_argv_insert (cmd, 0, "inserted") == 0,
        "flux_cmd_argv_insert (cmd, 0, inserted)");
    ok (flux_cmd_argc (cmd) == argc,
        "flux_cmd_argc is now %d (expected %d)",
        flux_cmd_argc (cmd), argc);
    is (flux_cmd_arg (cmd, 0), "inserted",
        "first argument is now `inserted`");

    ok (flux_cmd_argv_delete (cmd, 2) == 0,
        "flux_cmd_argv_delete from middle of argv works");
    ok (flux_cmd_argc (cmd) == argc - 1,
        "flux_cmd_argc is now %d (expected %d)",
        flux_cmd_argc (cmd), argc - 1);
    ok (flux_cmd_argv_insert (cmd, 2, "-d") == 0,
        "flux_cmd_argv_insert (cmd, 2, -d)");
    is (flux_cmd_arg (cmd, 2), "-d",
        "arg 3 is now `-d`");

    av = cmd_argv_expand (cmd);
    ok (av != NULL, "cmd_argv_expand ()");
    is (av[0], "inserted", "av[0] == inserted");
    is (av[1], "--option=foo", "av[1] == --option=foo");
    is (av[2], "-d", "av[2] == -d");
    is (av[3], "5", "av[3] == 5");
    is (av[4], "bar", "av[4] == bar");
    ok (av[5] == NULL, "av[5] == NULL");

    free (av);
    flux_cmd_destroy (cmd);
}

void test_env (void)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, NULL)))
        BAIL_OUT ("failed to create command object");

    // Test unsetenv with throwaway var
    diag ("Test setenv/getenv/unsetenv");
    ok (flux_cmd_setenvf (cmd, 1, "FOO", "%d", 42) >= 0,
        "flux_cmd_setenvf (FOO=42)");
    is (flux_cmd_getenv (cmd, "FOO"), "42",
        "flux_cmd_getenv (FOO) == 42");
    flux_cmd_unsetenv (cmd, "FOO");
    ok (flux_cmd_getenv (cmd, "FOO") == NULL,
        "flux_cmd_unsetenv works");

    // Test env overwrite
    ok (flux_cmd_setenvf (cmd, 0, "FOO", "%d", 42) >= 0,
        "flux_cmd_setenvf (FOO=42)");
    is (flux_cmd_getenv (cmd, "FOO"), "42",
        "flux_cmd_getenv (FOO) == 42");
    ok (flux_cmd_setenvf (cmd, 0, "FOO", "%d", 24) == 0,
        "flux_cmd_setenvf (FOO=24) no overwrite succeeds");
    is (flux_cmd_getenv (cmd, "FOO"), "42",
        "flux_cmd_getenv (FOO) == 42 (still)");
    ok (flux_cmd_setenvf (cmd, 1, "FOO", "%d", 24) >= 0,
        "flux_cmd_setenvf (FOO=24, overwrite=true)");
    is (flux_cmd_getenv (cmd, "FOO"), "24",
        "flux_cmd_getenv (FOO) == 24");
    flux_cmd_unsetenv (cmd, "FOO");

    flux_cmd_destroy (cmd);
}

void test_env_glob (void)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, NULL)))
        BAIL_OUT ("failed to create command object");
    lives_ok ({flux_cmd_unsetenv (NULL, "FOO");},
        "flux_cmd_unset (NULL, FOO) doesn't crash");
    lives_ok ({flux_cmd_unsetenv (cmd, NULL);},
        "flux_cmd_unset (cmd, NULL) doesn't crash");
    lives_ok ({flux_cmd_unsetenv (cmd, "FOO");},
        "flux_cmd_unset (cmd, FOO) doesn't crash on empty cmd env");
    ok (flux_cmd_setenvf (cmd, 0, "NOMATCH_FOO", "%d", 1) >= 0,
        "flux_cmd_setenvf (NOMATCH_FOO=1)");
    ok (flux_cmd_setenvf (cmd, 0, "MATCH_FOO", "%d", 2) >= 0,
        "flux_cmd_setenvf (MATCH_FOO=2)");
    ok (flux_cmd_setenvf (cmd, 0, "NOMATCH_BAR", "%d", 3) >= 0,
        "flux_cmd_setenvf (NOMATCH_BAR=3)");
    ok (flux_cmd_setenvf (cmd, 0, "MATCH_BAR", "%d", 4) >= 0,
        "flux_cmd_setenvf (MATCH_BAR=4)");
    flux_cmd_unsetenv (cmd, "MATCH_*");
    diag ("flux_cmd_unsetenv (MATCH_*)");
    is (flux_cmd_getenv (cmd, "NOMATCH_FOO"), "1",
        "flux_cmd_getenv (NOMATCH_FOO == 1");
    ok (flux_cmd_getenv (cmd, "MATCH_FOO") == NULL,
        "flux_cmd_getenv (MATCH_FOO == NULL)");
    is (flux_cmd_getenv (cmd, "NOMATCH_BAR"), "3",
        "flux_cmd_getenv (NOMATCH_BAR == 3");
    ok (flux_cmd_getenv (cmd, "MATCH_BAR") == NULL,
        "flux_cmd_getenv (MATCH_BAR == NULL)");
    flux_cmd_destroy (cmd);
}

void test_label (void)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, NULL)))
        BAIL_OUT ("failed to create command object");
    ok (flux_cmd_get_label (cmd) == NULL,
        "flux_cmd_get_label () returns NULL for unset label");
    ok (flux_cmd_set_label (NULL, NULL) < 0 && errno == EINVAL,
        "flux_cmd_set_label () returns EINVAL on invalid argument");
    ok (flux_cmd_set_label (cmd, "") < 0 && errno == EINVAL,
        "flux_cmd_set_label () returns EINVAL with zero-length label");
    ok (flux_cmd_set_label (cmd, "foo") == 0,
        "flux_cmd_set_label () works");
    is (flux_cmd_get_label (cmd), "foo",
        "flux_cmd_get_label now returns 'foo'");
    ok (flux_cmd_set_label (cmd, NULL) == 0,
        "flux_cmd_set_label (NULL) works");
    ok (flux_cmd_get_label (cmd) == NULL,
        "flux_cmd_get_label (cmd) now shows that label unset");

    flux_cmd_destroy (cmd);
}

int main (int argc, char *argv[])
{
    json_t *o;
    flux_cmd_t *cmd, *copy;

    plan (NO_PLAN);

    diag ("Basic flux_cmd_create");
    check_basic_create ();

    diag ("Create a flux_cmd_t and fill it with known values");
    // Create an empty command then fill it with nonsense:
    cmd = flux_cmd_create (0, NULL, NULL);
    ok (cmd != NULL, "flux_cmd_create (0, NULL, NULL)");
    check_empty_cmd_attributes (cmd);
    set_cmd_attributes (cmd);

    diag ("Ensure flux_cmd_t contains expected values and test interfaces");
    // Check the nonsense
    check_cmd_attributes (cmd);

    set_cmd_attributes2 (cmd);

    diag ("Ensure flux_cmd_t contains expected values again");
    check_cmd_attributes (cmd);

    // Test opt overwrite
    ok (flux_cmd_setopt (cmd, "FOO", "BAR") >= 0,
        "flux_cmd_setopt");
    is (flux_cmd_getopt (cmd, "FOO"), "BAR",
        "flux_cmd_getopt (cmd, 'FOO') == BAR");
    ok (flux_cmd_setopt (cmd, "FOO", "BAZ") >= 0,
        "flux_cmd_setopt");
    is (flux_cmd_getopt (cmd, "FOO"), "BAZ",
        "flux_cmd_getopt (cmd, 'FOO') == BAZ");

    diag ("Copy a flux_cmd_t and and ensure it matches source cmd");
    copy = flux_cmd_copy (cmd);
    ok (copy != NULL, "flux_cmd_copy");
    check_cmd_attributes (copy);
    flux_cmd_destroy (copy);

    diag ("Convert flux_cmd_t to/from JSON");
    o = cmd_tojson (cmd);
    ok (o != NULL, "cmd_tojson works");
    if (o) {
        json_error_t error;
        copy = cmd_fromjson (o, &error);
        json_decref (o);
        ok (copy != NULL, "cmd_fromjson returned a new cmd");
        if (copy) {
            check_cmd_attributes (copy);
            flux_cmd_destroy (copy);
        }
        else
            diag ("%d:%d: %s", error.line, error.column, error.text);
    }
    flux_cmd_destroy (cmd);

    test_env ();
    test_env_glob ();

    test_find_opts ();

    test_arg_insert_delete ();

    test_stringify ();

    test_label ();

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
