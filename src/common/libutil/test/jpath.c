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
#include <sys/param.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "jpath.h"

void diag_json (json_t *o)
{
    char *s = json_dumps (o, JSON_COMPACT);
    diag ("%s", s);
    free (s);
}

void badargs (void)
{
    json_t *o;

    if (!(o = json_object ()))
        BAIL_OUT ("json_object() failed");

    errno = 0;
    ok (jpath_get (NULL, "foo") ==NULL && errno == EINVAL,
        "jpath_get o=NULL fails with EINVAL");
    errno = 0;
    ok (jpath_del (NULL, "foo") < 0 && errno == EINVAL,
        "jpath_del o=NULL fails with EINVAL");
    errno = 0;
    ok (jpath_set (NULL, "foo", json_null ()) < 0 && errno == EINVAL,
        "jpath_set o=NULL fails with EINVAL");

    errno = 0;
    ok (jpath_get (o, NULL) == NULL && errno == EINVAL,
        "jpath_get path=NULL fails with EINVAL");
    errno = 0;
    ok (jpath_del (o, NULL) < 0 && errno == EINVAL,
        "jpath_del path=NULL fails with EINVAL");
    errno = 0;
    ok (jpath_set (o, NULL, json_null ()) < 0 && errno == EINVAL,
        "jpath_set path=NULL fails with EINVAL");

    errno = 0;
    ok (jpath_set (o, "foo", NULL) < 0 && errno == EINVAL,
        "jpath_set val=NULL fails with EINVAL");

    json_decref (o);
}

void basic (void)
{
    json_t *o;
    json_t *val[3];
    json_t *tmp;
    int i;

    o = json_object ();
    val[0] = json_object ();
    val[1] = json_real (3.14);
    val[2] = json_string ("foo");
    if (!o || !val[0] || !val[1] || !val[2])
        BAIL_OUT ("error creating test objects");

    ok (jpath_set (o, "a.c.d", val[0]) == 0,
        "jpath_set a.c.d=object works");
    ok (jpath_set (o, "a.c.e", val[1]) == 0,
        "jpath_set a.c.e=3.14 works");
    ok (jpath_set (o, "a.b", val[2]) == 0,
        "jpath_set a.b=\"foo\" works");

    tmp = jpath_get (o, "a.c.d");
    ok (tmp
        && json_is_object (tmp),
        "jpath_get a.c.d returned expected value");

    tmp = jpath_get (o, "a.c.e");
    ok (tmp
        && json_is_real (tmp)
        && json_real_value (tmp) == 3.14,
        "jpath_get a.c.e returned expected value");

    tmp = jpath_get (o, "a.b");
    ok (tmp
        && json_is_string (tmp)
        && !strcmp (json_string_value (tmp), "foo"),
        "jpath_get a.b returned expected value");

    diag_json (o);

    ok (jpath_del (o, "a.b") == 0,
        "jpath_del a.b works");
    errno = 0;
    ok (jpath_get (o, "a.b") == NULL && errno == ENOENT,
        "jpath_get a.b fails with ENOENT");

    ok (jpath_del (o, "a.c") == 0,
        "jpath_del a.c works");
    errno = 0;
    ok (jpath_get (o, "a.c.e") == NULL && errno == ENOENT,
        "jpath_get a.c.e fails with ENOENT");
    errno = 0;
    ok (jpath_get (o, "a.c.d") == NULL && errno == ENOENT,
        "jpath_get a.c.d fails with ENOENT");

    diag_json (o);

    ok (jpath_del (o, "a.c.d") == 0,
        "jpath_del on nonexistent path does not fail");

    for (i = 0; i < sizeof (val)/sizeof (val[0]); i++)
        json_decref (val[i]);
    json_decref (o);
}

void edge (void)
{
    json_t *o;

    if (!(o = json_object ())
        || jpath_set (o, "foo.bar", json_null ()) < 0)
        BAIL_OUT ("error setting up test objects failed");

    errno = 0;
    ok (jpath_del (o, ".foo") < 0 && errno == EINVAL,
        "jpath_del .foo fails with EINVAL");
    errno = 0;
    ok (jpath_del (o, "foo..bar") < 0 && errno == EINVAL,
        "jpath_del foo..bar fails with EINVAL");
    errno = 0;
    ok (jpath_del (o, "foo.") < 0 && errno == EINVAL,
        "jpath_del foo. fails with EINVAL");

    errno = 0;
    ok (jpath_get (o, ".foo") == NULL && errno == EINVAL,
        "jpath_get .foo fails with EINVAL");
    errno = 0;
    ok (jpath_get (o, "foo..bar") == NULL && errno == EINVAL,
        "jpath_get foo..bar fails with EINVAL");
    errno = 0;
    ok (jpath_get (o, "foo.") == NULL && errno == EINVAL,
        "jpath_get foo. fails with EINVAL");

    errno = 0;
    ok (jpath_set (o, ".foo", json_null ()) < 0 && errno == EINVAL,
        "jpath_set .foo fails with EINVAL");
    errno = 0;
    ok (jpath_set (o, "foo..bar", json_null ()) < 0 && errno == EINVAL,
        "jpath_set foo..bar fails with EINVAL");
    errno = 0;
    ok (jpath_set (o, "foo.", json_null ()) < 0 && errno == EINVAL,
        "jpath_set foo. fails with EINVAL");

    json_decref (o);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    badargs ();
    basic ();
    edge ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
