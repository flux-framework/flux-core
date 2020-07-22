/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/alloc.h"

void basic (void)
{
    struct job j = {0};
    json_t *orig;
    json_t *new;
    json_t *cmp;

    orig = json_object ();
    new = json_object ();
    cmp = json_object ();
    if (!orig || !new || !cmp)
        BAIL_OUT ("json_object() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive does nothing on empty dictionary");

    json_decref (new);
    json_decref (cmp);

    new = json_pack ("{s:n}", "blah");
    cmp = json_object ();
    if (!new || !cmp)
        BAIL_OUT ("json_object() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive does nothing removing non-existant key");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:s s:i}", "str", "foo", "num", 1);
    cmp = json_pack("{s:s s:i}", "str", "foo", "num", 1);
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive updates orig appropriately");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:s}", "str", "bar");
    cmp = json_pack("{s:s s:i}", "str", "bar", "num", 1);
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive overwrites existing key");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:n}", "num");
    cmp = json_pack("{s:s}", "str", "bar");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive removes value on json null setting");

    json_decref (new);
    json_decref (cmp);
}

void recursive (void)
{
    struct job j = {0};
    json_t *orig;
    json_t *new;
    json_t *cmp;

    orig = json_object ();
    new = json_pack ("{s:{}}", "obj", "str", "foo");
    cmp = json_pack ("{}");
    if (!orig || !new || !cmp)
        BAIL_OUT ("json_object/pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively does nothing on empty dictionary");

    new = json_pack ("{s:{s:s}}", "obj", "str", "foo");
    cmp = json_pack ("{s:{s:s}}", "obj", "str", "foo");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive sets dictionary");

    json_decref (new);
    json_decref (cmp);

    new = json_pack ("{s:{s:n}}", "obj", "blah");
    cmp = json_pack ("{s:{s:s}}", "obj", "str", "foo");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively does nothing removing non-existant key");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:{s:i}}", "obj", "num", 1);
    cmp = json_pack("{s:{s:s s:i}}", "obj", "str", "foo", "num", 1);
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively updates orig appropriately");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:{s:s}}", "obj", "str", "bar");
    cmp = json_pack("{s:{s:s s:i}}", "obj", "str", "bar", "num", 1);
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively overwrites existing key");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:{s:n}}", "obj", "num");
    cmp = json_pack("{s:{s:s}}", "obj", "str", "bar");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively removes value on json null setting");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:{s:n}}", "obj", "str");
    cmp = json_pack("{}");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive recursively removes empty sub-dictionaries");

    json_decref (new);
    json_decref (cmp);
}

void overwrite (void)
{
    struct job j = {0};
    json_t *orig;
    json_t *new;
    json_t *cmp;

    orig = json_object ();
    new = json_pack ("{s:{s:s}}", "obj", "str", "foo");
    cmp = json_pack ("{s:{s:s}}", "obj", "str", "foo");
    if (!orig || !new || !cmp)
        BAIL_OUT ("json_object/pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive sets dictionary");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:s}", "obj", "foo");
    cmp = json_pack("{s:s}", "obj", "foo");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive overwrites object with non-object");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:{s:s}}", "obj", "str", "bar");
    cmp = json_pack("{s:{s:s}}", "obj", "str", "bar");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive overwrites non-object with object");

    json_decref (new);
    json_decref (cmp);

    new = json_pack("{s:n}", "obj");
    cmp = json_pack("{}");
    if (!new || !cmp)
        BAIL_OUT ("json_pack() failed");

    update_recursive (&j, orig, new, NULL, NULL);
    ok (json_equal (orig, cmp) > 0,
        "update_recursive removes whole dict on json null setting");

    json_decref (new);
    json_decref (cmp);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    recursive ();
    overwrite ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
