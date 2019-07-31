/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <assert.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/command.h"

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
    int argc = (sizeof (argv)/sizeof (argv[0])) - 1;
    char * env[] = {
        "FOO=bar",
        "PATH=/bin",
        NULL
    };
    flux_cmd_t *cmd;

    diag ("simple flux_cmd_create (argc, argv, env)");
    cmd = flux_cmd_create (argc, argv, env);
    ok (cmd != NULL, "flux_cmd_create ()");
    av = flux_cmd_argv_expand (cmd);
    ok (av != NULL, "flux_cmd_argv_expand ()");
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

    argv = flux_cmd_argv_expand (cmd);
    ok (argv != NULL, "flux_cmd_argv_expand returned an argv");
    ok (argv[0] == NULL, "argv is properly NULL terminated");
    free (argv);

    env = flux_cmd_env_expand (cmd);
    ok (env != NULL, "flux_cmd_env_expand works");
    ok (env[0] == NULL, "flux_cmd_env_expand properly terminates env");
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
}

/* set alternate way, to ensure alternate ways also work */
void set_cmd_attributes2 (flux_cmd_t *cmd)
{
    char *env[] = { "PATH=/bin:/usr/bin", NULL };

    ok (flux_cmd_set_env (cmd, env) == 0,
        "flux_cmd_set_env");
}

void check_cmd_attributes (flux_cmd_t *cmd)
{
    char **argv, **env;
    const char *arg = NULL;

    ok (flux_cmd_argc (cmd) == 3, "flux_cmd_argc");

    argv = flux_cmd_argv_expand (cmd);
    ok (argv != NULL, "flux_cmd_argv_expand returned an argv");
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
        && !strcmp (arg, "command"),
        "flux_cmd_arg returns correct argv[0]");
    arg = flux_cmd_arg (cmd, 1);
    ok (arg != NULL
        && !strcmp (arg, "foo"),
        "flux_cmd_arg returns correct argv[1]");
    arg = flux_cmd_arg (cmd, 2);
    ok (arg != NULL
        && !strcmp (arg, "bar"),
        "flux_cmd_arg returns correct argv[2]");

    is (flux_cmd_getenv (cmd, "PATH"), "/bin:/usr/bin",
        "flux_cmd_getenv");

    env = flux_cmd_env_expand (cmd);
    ok (env != NULL, "flux_cmd_env_expand works");
    ok (env[1] == NULL, "flux_cmd_env_expand properly terminates env");
    is (env[0], "PATH=/bin:/usr/bin",
        "first entry of env is as expected");
    free (env);

    is (flux_cmd_getcwd (cmd), "/tmp",
        "flux_cmd_getcwd");
    is (flux_cmd_getopt (cmd, "OPTION"), "VALUE",
        "flux_cmd_getopt (cmd, 'OPTION') == VALUE");
}

int main (int argc, char *argv[])
{
    char *s;
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
    ok (flux_cmd_setenvf (cmd, 0, "FOO", "%d", 24) < 0,
        "flux_cmd_setenvf (FOO=24) no overwrite fails");
    ok (flux_cmd_setenvf (cmd, 1, "FOO", "%d", 24) >= 0,
        "flux_cmd_setenvf (FOO=24, overwrite=true)");
    is (flux_cmd_getenv (cmd, "FOO"), "24",
        "flux_cmd_getenv (FOO) == 24");
    flux_cmd_unsetenv (cmd, "FOO");

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
    s = flux_cmd_tojson (cmd);
    ok (s != NULL, "flux_cmd_tojson (%d bytes)", strlen (s));
    if (s) {
        json_error_t error;
        diag (s);
        copy = flux_cmd_fromjson (s, &error);
        free (s);
        ok (copy != NULL, "flux_cmd_fromjson returned a new cmd");
        if (copy) {
            check_cmd_attributes (copy);
            flux_cmd_destroy (copy);
        }
        else
            diag ("%d:%d: %s", error.line, error.column, error.text);
    }
    flux_cmd_destroy (cmd);

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
