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

    if (!(env = specutil_env_create (environ)))
        BAIL_OUT ("specutil_env_create failed");
    ok (json_object_size (env) > 0,
        "specutil_env_create() works");

    ok (specutil_env_set (env, "TEST_FOO", "42") == 0
        && object_is_string (env, "TEST_FOO", "42"),
        "specutil_env_set TEST_FOO=42 works");

    ok (specutil_env_set (env, "TEST_FOO", "43") == 0
        && object_is_string (env, "TEST_FOO", "43"),
        "specutil_env_set TEST_FOO=43 works");

    ok (specutil_env_put (env, "TEST_FOO=44") == 0
        && object_is_string (env, "TEST_FOO", "44"),
        "specutil_env_put TEST_FOO=44 works");

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

void check_jobspec (void)
{
    char *argv[] = { "this", "is", "a", "test" };
    int argc = sizeof (argv) / sizeof (argv[0]);
    json_t *attr;
    json_t *av;
    json_t *spec;
    json_t *val;
    struct resource_param p = { 0, 0, 0, 0 };
    char errbuf[128];

    if (!(attr = json_object ()))
        BAIL_OUT ("json_object failed");
    if (!(av = specutil_argv_create (argc, argv)))
        BAIL_OUT ("specutil_argv_create failed");

    ok ((spec = specutil_jobspec_create (attr, av, &p,
                                         errbuf, sizeof (errbuf))) != NULL,
        "specutil_jobspec_create works");
    ok (json_object_get (spec, "resources") != NULL,
        "jobspec has resources section");
    ok (json_object_get (spec, "tasks") != NULL,
        "jobspec has tasks section");
    ok (json_object_get (spec, "attributes") != NULL,
        "jobspec has attributes section");
    ok ((val = json_object_get (spec, "version")) != NULL
        && json_is_integer (val)
        && json_integer_value (val) == 1,
        "jobspec has version 1");

    json_decref (attr);
    json_decref (av);
    json_decref (spec);
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
    errno = 0;
    *errbuf = '\0';
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) < 0
        && errno == EINVAL
        && strlen (errbuf) > 0,
        "specutil_attr_check failed with EINVAL, errbuf");
    diag ("errbuf=%s", errbuf);
    json_object_del (attr, "a");

    if (specutil_attr_pack (attr, "system", "{}") < 0)
        BAIL_OUT ("could not set system={}");
    errno = 0;
    *errbuf = '\0';
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) < 0
        && errno == EINVAL
        && strlen (errbuf) > 0,
        "specutil_attr_check attr= failed with EINVAL, errbuf");
    diag ("errbuf=%s", errbuf);

    if (specutil_attr_pack (attr, "system.duration", "f", 0.1) < 0)
        BAIL_OUT ("could not set system.duration=0.1");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.duration=0.1 OK");

    if (specutil_attr_pack (attr, "system.duration", "s", "x") < 0)
        BAIL_OUT ("could not set system.duration=x");
    errno = 0;
    *errbuf = '\0';
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) < 0
        && errno == EINVAL
        && strlen (errbuf) > 0,
        "specutil_attr_check system.duration=x failed with EINVAL, errbuf");
    diag ("errbuf=%s", errbuf);

    if (specutil_attr_del (attr, "system") < 0)
        BAIL_OUT ("could not remove system attribute dict");
    if (specutil_attr_pack (attr, "system.environment", "{}") < 0)
        BAIL_OUT ("could not set system.environment={}");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.environment={} OK");

    if (specutil_attr_pack (attr, "system.environment", "s", "x") < 0)
        BAIL_OUT ("could not set system.environment=x");
    errno = 0;
    *errbuf = '\0';
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) < 0
        && errno == EINVAL
        && strlen (errbuf) > 0,
        "specutil_attr_check system.environment=x failed with EINVAL, errbuf");
    diag ("errbuf=%s", errbuf);

    if (specutil_attr_del (attr, "system") < 0)
        BAIL_OUT ("could not remove system attribute dict");
    if (specutil_attr_pack (attr, "system.shell.options", "{}") < 0)
        BAIL_OUT ("could not set system.shell.options={}");
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) == 0,
        "specutil_attr_check system.shell.options={} OK");

    if (specutil_attr_pack (attr, "system.shell.options", "s", "x") < 0)
        BAIL_OUT ("could not set system.shell.options=x");
    errno = 0;
    *errbuf = '\0';
    ok (specutil_attr_check (attr, errbuf, sizeof (errbuf)) < 0
        && errno == EINVAL
        && strlen (errbuf) > 0,
        "specutil_attr_check system.shell.options=x failed with EINVAL, errbuf");
    diag ("errbuf=%s", errbuf);

    json_decref (attr);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_env ();
    check_argv ();
    check_attr ();
    check_jobspec ();
    check_attr_check ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
