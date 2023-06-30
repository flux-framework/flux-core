/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "src/common/libjob/jobspec1.h"
#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

extern char **environ;

void check_stdio_cwd (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);
    flux_jobspec1_t *jobspec;
    char *path;

    if (!(jobspec = flux_jobspec1_from_command (argc, argv, NULL, 1, 1, 1, 0, 0.0))) {
        BAIL_OUT ("flux_jobspec1_from_command failed");
    }
    ok (flux_jobspec1_set_cwd (NULL, "/foo/bar/baz") < 0 && errno == EINVAL,
        "flux_jobspec1_set_cwd catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_set_cwd (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_set_cwd catches NULL cwd");
    errno = 0;
    ok (flux_jobspec1_set_cwd (jobspec, "/foo/bar/baz") == 0
            && (flux_jobspec1_attr_unpack (jobspec, "system.cwd", "s", &path) == 0)

            && streq ("/foo/bar/baz", path),
        "flux_jobspec1_set_cwd works");
    ok (flux_jobspec1_set_stdin (NULL, "/foo/bar/baz") < 0 && errno == EINVAL,
        "flux_jobspec1_set_stdin catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_set_stdin (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_set_stdin catches NULL path");
    errno = 0;
    ok (flux_jobspec1_set_stdin (jobspec, "/foo/bar/stdin.txt") == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.shell.options.input.stdin.path",
                                           "s",
                                           &path)
                == 0)

            && streq ("/foo/bar/stdin.txt", path),
        "flux_jobspec1_set_stdin sets right path");
    ok ((flux_jobspec1_attr_unpack (jobspec,
                                    "system.shell.options.input.stdin.type",
                                    "s",
                                    &path)
         == 0)
            && streq ("file", path),
        "flux_jobspec1_set_stdin sets right type");
    ok (flux_jobspec1_set_stdout (NULL, "/foo/bar/baz") < 0 && errno == EINVAL,
        "flux_jobspec1_set_stdout catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_set_stdout (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_set_stdout catches NULL path");
    errno = 0;
    ok (flux_jobspec1_set_stdout (jobspec, "/foo/bar/stdout.txt") == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.shell.options.output.stdout.path",
                                           "s",
                                           &path)
                == 0)

            && streq ("/foo/bar/stdout.txt", path),
        "flux_jobspec1_set_stdout sets right path");
    ok ((flux_jobspec1_attr_unpack (jobspec,
                                    "system.shell.options.output.stdout.type",
                                    "s",
                                    &path)
         == 0)
            && streq ("file", path),
        "flux_jobspec1_set_stdout sets right type");
    ok (flux_jobspec1_set_stderr (NULL, "/foo/bar/baz") < 0 && errno == EINVAL,
        "flux_jobspec1_set_stderr catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_set_stderr (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_set_stderr catches NULL path");
    errno = 0;
    ok (flux_jobspec1_set_stderr (jobspec, "/foo/bar/stderr.txt") == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.shell.options.output.stderr.path",
                                           "s",
                                           &path)
                == 0)

            && streq ("/foo/bar/stderr.txt", path),
        "flux_jobspec1_set_stderr sets right path");
    ok ((flux_jobspec1_attr_unpack (jobspec,
                                    "system.shell.options.output.stderr.type",
                                    "s",
                                    &path)
         == 0)
            && streq ("file", path),
        "flux_jobspec1_set_stderr sets right type");
    flux_jobspec1_destroy (jobspec);
}

void check_env (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);
    flux_jobspec1_t *jobspec;
    char *val;

    if (!(jobspec =
              flux_jobspec1_from_command (argc, argv, environ, 1, 1, 1, 0, 0.0))) {
        BAIL_OUT ("flux_jobspec1_from_command failed with environ");
    }
    ok (flux_jobspec1_setenv (NULL, "FOO", "BAR", 1) < 0 && errno == EINVAL,
        "flux_jobspec1_setenv catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_setenv (jobspec, NULL, "BAR", 1) < 0 && errno == EINVAL,
        "flux_jobspec1_setenv catches NULL variable name");
    errno = 0;
    ok (flux_jobspec1_setenv (jobspec, "FOO", NULL, 1) < 0 && errno == EINVAL,
        "flux_jobspec1_setenv catches NULL variable value");
    errno = 0;
    ok (flux_jobspec1_unsetenv (NULL, "FOO") < 0 && errno == EINVAL,
        "flux_jobspec1_unsetenv catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_unsetenv (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_unsetenv catches NULL variable");
    errno = 0;
    ok (flux_jobspec1_setenv (jobspec, "FOO1", "BAR1", 1) == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.environment.FOO1",
                                           "s",
                                           &val)
                == 0)
            && streq ("BAR1", val),
        "jobspec_setenv FOO1=BAR1 works");
    ok (flux_jobspec1_setenv (jobspec, "FOO1", "BAZ1", 1) == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.environment.FOO1",
                                           "s",
                                           &val)
                == 0)
            && streq ("BAZ1", val),
        "jobspec_setenv FOO1=BAZ1 works (overwrite=1)");
    ok (flux_jobspec1_setenv (jobspec, "FOO1", "BAZ2", 0) == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.environment.FOO1",
                                           "s",
                                           &val)
                == 0)
            && streq ("BAZ1", val),
        "jobspec_setenv FOO1=BAZ2 works (overwrite=0)");
    ok (flux_jobspec1_unsetenv (jobspec, "FOO1") == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.environment.FOO1",
                                           "s",
                                           &val)
                < 0),
        "unset FOO1 works");
    ok (flux_jobspec1_setenv (jobspec, "FOO2", "BAR2", 1) == 0
            && (flux_jobspec1_attr_unpack (jobspec,
                                           "system.environment.FOO2",
                                           "s",
                                           &val)
                == 0)
            && streq ("BAR2", val),
        "jobspec_setenv FOO2=BAR2 works");

    // ensure empty ("") value works
    ok (flux_jobspec1_setenv (jobspec, "empty", "", 1) == 0,
        "flux_jobspec1_setenv accepts empty string value");
    ok (flux_jobspec1_attr_unpack (jobspec,
                                  "system.environment.empty",
                                  "s",
                                  &val) == 0
        && val != NULL
        && streq (val, ""),
        "empty string value was correctly represented in object");
    // test functions when environment object is deleted
    ok (flux_jobspec1_attr_del (jobspec, "system.environment") == 0,
        "deleting environment works");
    ok (flux_jobspec1_setenv (jobspec, "FOO1", "BAR1", 1) == 0,
        "flux_jobspec1_setenv works after deleting environment object");
    ok (flux_jobspec1_unsetenv (jobspec, "FOO") == 0,
        "flux_jobspec1_unsetenv works after deleting environment");
    flux_jobspec1_destroy (jobspec);
}

void check_attr (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);
    flux_jobspec1_t *jobspec;
    json_t *json_ptr;
    int int_val;
    char *char_ptr;

    if (!(jobspec = flux_jobspec1_from_command (argc, argv, NULL, 1, 1, 1, 0, 0.0))) {
        BAIL_OUT ("flux_jobspec1_from_command failed");
    }
    ok (flux_jobspec1_attr_pack (NULL, "foo.bar", "i", 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_pack catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_attr_pack (jobspec, NULL, "i", 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_pack catches NULL path");
    errno = 0;
    ok (flux_jobspec1_attr_pack (jobspec, "foo.bar", NULL, 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_pack catches NULL format string");
    errno = 0;
    ok (flux_jobspec1_attr_unpack (NULL, "foo.bar", "i", 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_unpack catches NULL jobspec");
    errno = 0;
    ok (flux_jobspec1_attr_unpack (jobspec, NULL, "i", 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_unpack catches NULL path");
    errno = 0;
    ok (flux_jobspec1_attr_unpack (jobspec, "foo.bar", NULL, 5) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_unpack catches NULL format string");
    errno = 0;
    ok (flux_jobspec1_attr_del (jobspec, NULL) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_del catches NULL path");
    errno = 0;
    ok (flux_jobspec1_attr_del (NULL, "foo.bar") < 0 && errno == EINVAL,
        "flux_jobspec1_attr_del catches NULL jobspec");
    errno = 0;

    ok (flux_jobspec1_attr_pack (jobspec, "foo.bar", "s", "baz") == 0
            && (flux_jobspec1_attr_unpack (jobspec, "foo.bar", "s", &char_ptr) == 0)
            && streq ("baz", char_ptr),
        "flux_jobspec1_attr_pack works on strings");
    ok (flux_jobspec1_attr_pack (jobspec, "foo.bar", "i", 19) == 0
            && (flux_jobspec1_attr_unpack (jobspec, "foo.bar", "i", &int_val) == 0)
            && 19 == int_val,
        "flux_jobspec1_attr_pack works on integers");
    ok (flux_jobspec1_attr_pack (jobspec, "foo", "{s:s}", "bar", "baz") == 0
            && (flux_jobspec1_attr_unpack (jobspec, "foo", "{s:s}", "bar", &char_ptr)
                == 0)
            && streq ("baz", char_ptr),
        "flux_jobspec1_attr_pack works on objects");
    ok (flux_jobspec1_attr_del (jobspec, "foo.bar.baz") == 0
            && (flux_jobspec1_attr_unpack (jobspec, "foo.bar.baz", "o", &json_ptr) < 0),
        "flux_jobspec1_attr_del works");
    ok (flux_jobspec1_attr_del (jobspec, "foo") == 0
            && (flux_jobspec1_attr_unpack (jobspec, "foo", "o", &json_ptr) < 0),
        "flux_jobspec1_attr_del works");
    flux_jobspec1_destroy (jobspec);
}

void check_jobspec (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);
    flux_jobspec1_t *jobspec;
    flux_jobspec1_error_t error;
    char *str;
    json_t *val;
    double passed_duration = 5.0;
    double duration;

    if (!(jobspec = flux_jobspec1_from_command (argc,
                                                argv,
                                                NULL,
                                                1,
                                                1,
                                                1,
                                                0,
                                                passed_duration))) {
        BAIL_OUT ("flux_jobspec1_from_command failed");
    }
    errno = 0;
    ok (flux_jobspec1_attr_check (NULL, &error) < 0 && errno == EINVAL,
        "flux_jobspec1_attr_check catches NULL jobspec");
    ok (flux_jobspec1_attr_check (jobspec, NULL) == 0,
        "flux_jobspec1_attr_check works with NULL error struct");

    ok (flux_jobspec1_attr_check (jobspec, &error) == 0,
        "flux_jobspec1_attr_check passed");
    ok (flux_jobspec1_attr_unpack (jobspec, "system", "o", &val) == 0,
        "jobspec has system attribute");
    ok (flux_jobspec1_attr_unpack (jobspec, "system.duration", "f", &duration) == 0
            && (duration - passed_duration < 0.001),
        "jobspec has system.duration attribute set to correct value");
    ok (flux_jobspec1_attr_unpack (jobspec, "system.environment", "o", &val) == 0
            && json_is_object (val) && json_object_size (val) == 0,
        "jobspec has system.environment object of size 0");
    ok (flux_jobspec1_attr_unpack (jobspec, "foo.bar", "s", &str) < 0,
        "jobspec has no foo.bar attribute");
    flux_jobspec1_destroy (jobspec);
    passed_duration = 0.0;
    if (!(jobspec = flux_jobspec1_from_command (argc,
                                                argv,
                                                NULL,
                                                1,
                                                1,
                                                1,
                                                0,
                                                passed_duration))) {
        BAIL_OUT ("flux_jobspec1_from_command failed");
    }
    ok ((flux_jobspec1_attr_unpack (jobspec, "system.duration", "f", &duration) == 0)
            && (duration == passed_duration),
        "jobspec has system.duration attribute set to correct value");
    flux_jobspec1_destroy (jobspec);
    ok (flux_jobspec1_from_command (argc, argv, NULL, 1, 1, 1, 5, 0) == NULL,
        "flux_jobspec1_from_command failed when nnodes > ntasks");
    if (!(jobspec = flux_jobspec1_from_command (argc, argv, NULL, 5, 1, 1, 3, 0.0))) {
        BAIL_OUT ("flux_jobspec1_from_command failed when nnodes < ntasks");
    }
    ok (flux_jobspec1_attr_check (jobspec, &error) == 0,
        "flux_jobspec1_attr_check passed when nnodes < ntasks");
    ok (flux_jobspec1_attr_pack (jobspec, "system.duration", "s", "not a number") == 0
            && flux_jobspec1_attr_unpack (jobspec, "system.duration", "f", &duration)
                   < 0,
        "deleting system.duration works");
    ok (flux_jobspec1_attr_check (jobspec, &error) < 0,
        "flux_jobspec1_attr_check failed after changing system.duration to a string");
    flux_jobspec1_destroy (jobspec);
    if (!(jobspec = flux_jobspec1_from_command (argc, argv, NULL, 5, 1, 1, 5, 0.0))) {
        BAIL_OUT ("flux_jobspec1_from_command failed when nnodes == ntasks");
    }
    ok (flux_jobspec1_attr_check (jobspec, &error) == 0,
        "flux_jobspec1_attr_check passed when nnodes == ntasks");
    ok (flux_jobspec1_attr_pack (jobspec, "foo", "f", 19.5) == 0
            && flux_jobspec1_attr_check (jobspec, &error) < 0,
        "attr_check failed after adding spurious attribute");
    flux_jobspec1_destroy (jobspec);
}

void check_encoding (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);
    flux_jobspec1_t *jobspec;
    char *encoded;
    flux_jobspec1_t *dup;
    flux_jobspec1_error_t error;

    if (!(jobspec = flux_jobspec1_from_command (argc, argv,
                                                NULL, 5, 3, 2, 0, 0.0)))
        BAIL_OUT ("flux_jobspec1_from_command failed");

    ok (flux_jobspec1_check (jobspec, &error) == 0,
        "flux_jobspec1_check returns success on valid jobspec");

    ok ((encoded = flux_jobspec1_encode (jobspec, 0)) != NULL,
        "flux_jobspec1_encode works");
    ok ((dup = flux_jobspec1_decode (encoded, &error)) != NULL,
        "flux_jobspec1_decode works");
    free (encoded);
    flux_jobspec1_destroy (dup);

    errno = EINVAL;
    flux_jobspec1_destroy (jobspec);
    ok (errno == EINVAL,
        "flux_jobspec1_destroy preserves errno");

    errno = 0;
    ok (flux_jobspec1_encode (NULL, 0) == NULL && errno == EINVAL,
        "flux_jobspec1_encode catches NULL jobspec");

    errno = 0;
    error.text[0] = '\0';
    ok (flux_jobspec1_decode ("{", &error) == NULL
        && errno == EINVAL
        && error.text[0] != '\0',
        "flux_jobspec1_decode on bad JSON fails with EINVAL and error buf set");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (flux_jobspec1_decode (NULL, &error) == NULL
        && errno == EINVAL
        && error.text[0] != '\0',
        "flux_jobspec1_decode NULL fails with EINVAL and error buf set");

    errno = 0;
    error.text[0] = '\0';
    ok (flux_jobspec1_check (NULL, &error) < 0
        && errno == EINVAL
        && error.text[0] != '\0',
        "flux_jobspec1_check NULL fails with EINVAL and error buf set");
}

void check_bad_args (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = ARRAY_SIZE (argv);

    ok (flux_jobspec1_from_command (-1, argv, NULL, 1, 1, 1, 0, 5.0) == NULL
            && errno == EINVAL,
        "flux_jobspec1_from_command catches bad argc");
    errno = 0;
    ok (flux_jobspec1_from_command (argc, NULL, NULL, 1, 1, 1, 0, 5.0) == NULL
            && errno == EINVAL,
        "flux_jobspec1_from_command catches bad argv");
    errno = 0;
    ok (flux_jobspec1_from_command (argc, argv, NULL, 1, 1, 1, 0, -1.5) == NULL
            && errno == EINVAL,
        "flux_jobspec1_from_command catches bad duration");
    errno = 0;

    flux_jobspec1_destroy (NULL);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_stdio_cwd ();
    check_env ();
    check_jobspec ();
    check_attr ();
    check_encoding ();
    check_bad_args ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
