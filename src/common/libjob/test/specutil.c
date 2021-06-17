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
#include <jansson.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/specutil.h"

bool object_is_string (json_t *object, const char *name, const char *val)
{
    json_t *o;
    const char *s;
    if (!(o = json_object_get (object, name)))
        return false;
    if (!json_is_string (o))
        return false;
    if (!(s = json_string_value (o)))
        return false;
    if (strcmp (val, s) != 0)
        return false;
    return true;
}

bool entry_is_string (json_t *array, int index, const char *val)
{
    json_t *o;
    const char *s;
    if (!(o = json_array_get (array, index)))
        return false;
    if (!json_is_string (o))
        return false;
    if (!(s = json_string_value (o)))
        return false;
    if (strcmp (val, s) != 0)
        return false;
    return true;
}

void check_env (void)
{
    json_t *env;
    char *bad_environ[2] = {NULL, NULL};

    if (!(env = specutil_env_create (environ)))
        BAIL_OUT ("specutil_env_create failed");
    ok (json_object_size (env) > 0,
        "specutil_env_create() works");
    if (!(bad_environ[0] = strdup("TEST_BAR")))
        BAIL_OUT ("initializing bad environ failed");
    ok (specutil_env_create (bad_environ) == NULL && errno == EINVAL,
        "specutil_env_create failed on bad environ");
    errno = 0;
    free(bad_environ[0]);

    ok (specutil_env_set (env, "TEST_FOO", "42", 1) == 0
        && object_is_string (env, "TEST_FOO", "42"),
        "specutil_env_set TEST_FOO=42 works");

    ok (specutil_env_set (env, "TEST_FOO", "43", 1) == 0
        && object_is_string (env, "TEST_FOO", "43"),
        "specutil_env_set TEST_FOO=43 works");

    ok (specutil_env_set (env, "TEST_FOO", "44", 0) == 0
        && object_is_string (env, "TEST_FOO", "43"),
        "specutil_env_set TEST_FOO=44 fails when overwrite == 0");

    ok (specutil_env_put (env, "TEST_FOO=44") == 0
        && object_is_string (env, "TEST_FOO", "44"),
        "specutil_env_put TEST_FOO=44 works");

    ok (specutil_env_put (env, "TEST_FOO2") < 0
        && errno == EINVAL
        && !json_object_get (env, "TEST_FOO2"),
        "specutil_env_put TEST_FOO2 (no value) fails with EINVAL");
    errno = 0;

    ok (specutil_env_put (env, "=44") < 0,
        "specutil_env_put =44 (no variable name) fails with EINVAL");
    errno = 0;

    ok (specutil_env_unset (env, "TEST_FOO") == 0
        && !json_object_get (env, "TEST_FOO"),
        "specutil_env_del TEST_FOO works");

    json_decref (env);
}

void check_argv (void)
{
    char *argv[] = { "this", "is", "a", "test" };
    int argc = sizeof (argv) / sizeof (argv[0]);
    json_t *av;
    int i;
    int errors = 0;

    if (!(av = specutil_argv_create (argc, argv)))
        BAIL_OUT ("specutil_argv_create failed");
    ok (json_array_size (av) == argc,
        "specutil_argv_create works");
    for (i = 0; i < argc; i++) {
        if (!entry_is_string (av, i, argv[i]))
            errors++;
    }
    ok (errors == 0,
        "specutil_argv_create set correct array values");

    json_decref (av);

    if (!(av = specutil_argv_create (-1, argv)))
        BAIL_OUT ("specutil_argv_create failed");
    ok (json_array_size (av) == 0,
        "specutil_argv_create works when argc < 0");
    json_decref (av);
}

void check_attr (void)
{
    json_t *attr;
    json_t *val;

    if (!(attr = json_object ()))
        BAIL_OUT ("json_object failed");
    ok (specutil_attr_pack (attr, "foo", "s", "bar") == 0
        && object_is_string (attr, "foo", "bar"),
        "specutil_attr_pack foo=bar works");
    ok (specutil_attr_pack (attr, "foo", "s", "baz") == 0
        && object_is_string (attr, "foo", "baz"),
        "specutil_attr_pack foo=baz works");
    ok (specutil_attr_pack (attr, "a.b", "f", 0.1) == 0
        && (val = json_object_get (attr, "a"))
        && json_is_object (val),
        "specutil_attr_pack a.b=(0.1) created object named a");
    ok ((val = specutil_attr_get (attr, "a.b"))
        && json_is_real (val)
        && json_real_value (val) == 0.1,
        "specutil_attr_get a.b returns expected value");
    ok (specutil_attr_del (attr, "a.b") == 0,
        "specutil_attr_del a.b works");
    errno = 0;
    ok (specutil_attr_del (attr, "") < 0 && errno == EINVAL,
        "specutil_attr_del on empty string fails with EINVAL");
    errno = 0;
    ok (specutil_attr_del (attr, ".a") < 0 && errno == EINVAL,
        "specutil_attr_del on .a (leading period) fails with EINVAL");
    errno = 0;
    ok (specutil_attr_del (attr, "a.") < 0 && errno == EINVAL,
        "specutil_attr_del on a. (trailing period) fails with EINVAL");

    errno = 0;
    ok (specutil_attr_get (attr, "") == NULL && errno == EINVAL,
        "specutil_attr_get on empty string fails with EINVAL");
    errno = 0;
    ok (specutil_attr_get (attr, ".a") == NULL && errno == EINVAL,
        "specutil_attr_get on .a (leading period) fails with EINVAL");
    errno = 0;
    ok (specutil_attr_get (attr, "a.") == NULL && errno == EINVAL,
        "specutil_attr_get on a. (trailing period) fails with EINVAL");
    errno = 0;
    ok (specutil_attr_get (attr, "a.b") == NULL && errno == ENOENT,
        "specutil_attr_get a.b fails with ENOENT");
    ok ((val = json_object_get (attr, "a"))
        && json_is_object (val),
        "but 'a' is still there");
    ok (specutil_attr_del (attr, "a") == 0
        && !json_object_get (attr, "a"),
        "specutil_attr_del a works");
    ok (specutil_attr_del (attr, "noexist") == 0,
        "specutil_attr_del noexist returns success");
    ok (specutil_attr_del (attr, "noexist.a") == 0,
        "specutil_attr_del noexist.a returns success");


    errno = 0;
    ok (specutil_attr_pack (attr, ".", "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack path=. fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (attr, ".a", "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack path=.a fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (attr, "a.", "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack path=a. fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (attr, "a..b", "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack path=a..b fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (NULL, "a", "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack attr=NULL fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (attr, NULL, "s", "a") < 0 && errno == EINVAL,
        "specutil_attr_pack path=NULL fails with EINVAL");
    errno = 0;
    ok (specutil_attr_pack (attr, "a", NULL, "a") < 0 && errno == EINVAL,
        "specutil_attr_pack fmt=a fails with EINVAL");

    errno = 0;
    ok (specutil_attr_get (NULL, "a") == NULL && errno == EINVAL,
        "specutil_attr_get attr=NULL fails with EINVAL");
    errno = 0;
    ok (specutil_attr_get (attr, NULL) == NULL && errno == EINVAL,
        "specutil_attr_get path=NULL fails with EINVAL");

    errno = 0;
    ok (specutil_attr_set (NULL, "a", json_null ()) < 0 && errno == EINVAL,
        "specutil_attr_set attr=NULL fails with EINVAL");
    errno = 0;
    ok (specutil_attr_set (attr, NULL, json_null ()) < 0 && errno == EINVAL,
        "specutil_attr_set path=NULL fails with EINVAL");
    errno = 0;
    ok (specutil_attr_set (attr, "a", NULL) < 0 && errno == EINVAL,
        "specutil_attr_set value=NULL fails with EINVAL");

    json_decref (attr);
}

void check_resources_create (void)
{
    json_t *resources;
    json_t *mapping = NULL;
    json_t *with_mapping = NULL;
    json_t *val;

    // test with gpus_per_task but no nodes
    if (!(resources = specutil_resources_create (5, 2, 3, 0))
        || !(mapping = json_array_get (resources, 0))) {
        BAIL_OUT ("specutil_resources_create failed");
    }
    ok (json_array_size (resources) == 1, "resources length is correct");
    ok (object_is_string (mapping, "type", "slot"), "resources has type:slot");
    ok (object_is_string (mapping, "label", "task"), "resources has label:task");
    ok ((val = json_object_get (mapping, "count")) && json_is_integer (val)
        && json_integer_value (val) == 5,
        "resources has correct task count");
    if (!(val = json_object_get (mapping, "with"))
        || !(with_mapping = json_array_get (val, 0))){
        BAIL_OUT ("resources has no 'with' mapping for cores_per_task");
    }
    ok (object_is_string (with_mapping, "type", "core"),
        "resources has 'with' type:core");
    ok ((val = json_object_get (with_mapping, "count")) && json_is_integer (val)
        && json_integer_value (val) == 2,
        "resources has correct cores_per_task count");
    if (!(val = json_object_get (mapping, "with"))
        || !(with_mapping = json_array_get (val, 1))){
        BAIL_OUT ("resources has no 'with' mapping for gpus_per_task");
    }
    ok (object_is_string (with_mapping, "type", "gpu"),
        "resources has 'with' type:core");
    ok ((val = json_object_get (with_mapping, "count"))
        && json_is_integer (val)
        && json_integer_value (val) == 3,
        "resources has correct gpus_per_task count");
    json_decref (resources);

    // test with neither gpus nor nodes
    if (!(resources = specutil_resources_create (-1, -1, 0, 0))
        || !(mapping = json_array_get (resources, 0))) {
        BAIL_OUT ("specutil_resources_create failed");
    }
    ok (json_array_size (resources) == 1, "resources length is correct");
    ok (object_is_string (mapping, "type", "slot"), "resources has type:slot");
    ok (object_is_string (mapping, "label", "task"),
        "resources has label:task");
    ok ((val = json_object_get (mapping, "count"))
        && json_is_integer (val)
        && json_integer_value (val) == 1,
        "resources correctly sets tasks=0 when value is negative");
    if (!(val = json_object_get (mapping, "with"))
        || !(with_mapping = json_array_get (val, 0))){
        BAIL_OUT ("resources has no 'with' mapping for cores_per_task");
    }
    ok (json_array_size (val) == 1,
        "'with' array has only one entry when gpus_per_task == 0");
    ok (object_is_string (with_mapping, "type", "core"),
        "resources has 'with' type:core");
    ok ((val = json_object_get (with_mapping, "count"))
        && json_is_integer (val)
        && json_integer_value (val) == 1,
        "resources correctly sets cores_per_task=0 when value is negative");
    json_decref (resources);

    // test with gpus and nodes
    if (!(resources = specutil_resources_create (20, 2, 1, 17))
        || !(mapping = json_array_get (resources, 0))) {
        BAIL_OUT ("specutil_resources_create failed");
    }
    ok (json_array_size (resources) == 1, "resources length is correct");

    ok (object_is_string (mapping, "type", "node"), "resources has type:node");
    ok ((val = json_object_get (mapping, "count")) && json_is_integer (val)
            && json_integer_value (val) == 17,
        "resources has correct node count");
    if (!(val = json_object_get (mapping, "with"))
        || !(with_mapping = json_array_get (val, 0))){
        BAIL_OUT ("resources has no 'with' mapping for tasks when nodes > 0");
    }
    ok (json_array_size (val) == 1,
        "'with' array has only one entry when nodes > 0");
    ok (object_is_string (with_mapping, "type", "slot"),
        "resources has type:slot");
    ok (object_is_string (with_mapping, "label", "task"),
        "resources has label:task");
    ok ((val = json_object_get (with_mapping, "count"))
        && json_is_integer (val)
        && json_integer_value (val) == 20,
        "resources has correct task count");
    json_decref (resources);

    // check nnodes > tasks
    ok (specutil_resources_create (2, 2, 3, 17) == NULL && errno == EINVAL,
        "caught nodes > tasks");
    errno = 0;
}

void check_tasks_create (void)
{
    char *argv[] = {"this", "is", "a", "test"};
    int argc = sizeof (argv) / sizeof (argv[0]);
    json_t *tasks;
    json_t *val;
    json_t *mapping = NULL;

    if (!(tasks = specutil_tasks_create (argc, argv))
        || !(mapping = json_array_get (tasks, 0))) {
        BAIL_OUT ("specutil_tasks_create failed");
    }
    ok (json_array_size (tasks) == 1, "tasks length is correct");
    ok ((val = json_object_get (mapping, "command"))
        && json_array_size (val) == argc,
        "tasks has command section of correct length");
    ok ((val = json_object_get (mapping, "count"))
        && (val = json_object_get (val, "per_slot"))
        && json_is_integer (val)
        && json_integer_value (val) == 1,
        "tasks has count: {per_slot: 1}");
    ok (object_is_string (mapping, "slot", "task"), "tasks has slot:task");
    json_decref (tasks);
}

void attr_check_fail (json_t *attr, const char *checkstr)
{
    char errbuf[128] = {0};
    int rc;

    errno = 0;
    rc = specutil_attr_check (attr, errbuf, sizeof (errbuf));
    ok (rc < 0 && errno == EINVAL && *errbuf != '\0',
        "specutil_attr_check %s fails with expected error", checkstr);
    if (rc < 0)
        diag ("%s", errbuf);
}

void check_attr_check (void)
{
    json_t *attr;
    char errbuf[128];

    if (!(attr = json_object ()))
        BAIL_OUT ("json_object failed");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check attr={} OK");

    if (specutil_attr_pack (attr, "a.b", "s", "foo") < 0)
        BAIL_OUT ("could not set a.b");
    attr_check_fail (attr, "a.b=\"foo\"");
    json_object_del (attr, "a");

    if (specutil_attr_pack (attr, "system", "{}") < 0)
        BAIL_OUT ("could not set system={}");
    attr_check_fail (attr, "system={}");

    if (specutil_attr_pack (attr, "system.duration", "f", 0.1) < 0)
        BAIL_OUT ("could not set system.duration=0.1");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.duration=0.1 OK");

    if (specutil_attr_pack (attr, "user", "{}") < 0)
        BAIL_OUT ("could not set user={}");
    attr_check_fail (attr, "user={}");
    json_object_del (attr, "user");

    if (specutil_attr_pack (attr, "system.duration", "s", "x") < 0)
        BAIL_OUT ("could not set system.duration");
    attr_check_fail (attr, "system.duration=\"x\"");
    json_object_del (attr, "system");

    if (specutil_attr_pack (attr, "system.environment", "{}") < 0)
        BAIL_OUT ("could not set system.environment");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.environment={} OK");

    if (specutil_attr_pack (attr, "system.environment", "s", "x") < 0)
        BAIL_OUT ("could not set system.environment");
    attr_check_fail (attr, "system.environment=\"x\"");
    json_object_del (attr, "system");

    if (specutil_attr_pack (attr, "system.shell.options", "{}") < 0)
        BAIL_OUT ("could not set system.shell.options");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.shell.options={} OK");

    if (specutil_attr_pack (attr, "system.shell.options", "s", "x") < 0)
        BAIL_OUT ("could not set system.shell.options");
    attr_check_fail (attr, "system.shell.options=\"x\"");
    json_object_del (attr, "system");

    json_decref (attr);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_env ();
    check_argv ();
    check_attr ();
    check_resources_create ();
    check_tasks_create ();
    check_attr_check ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
