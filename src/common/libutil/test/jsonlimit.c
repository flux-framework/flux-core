/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "jsonlimit.h"

/*  Create an object nested 'depth' levels deep, e.g. for depth=2:
 *   {"a":{"a":1}}
 *  A depth of 0 returns a scalar.
 */
static json_t *nested_object (int depth)
{
    json_t *o = json_integer (1);
    if (!o)
        BAIL_OUT ("failed to create integer");
    for (int i = 0; i < depth; i++) {
        json_t *n = json_pack ("{s:o}", "a", o);
        if (!n)
            BAIL_OUT ("failed to create nested object");
        o = n;
    }
    return o;
}

/*  Same, but nesting arrays instead of objects: [[[1]]]
 */
static json_t *nested_array (int depth)
{
    json_t *o = json_integer (1);
    if (!o)
        BAIL_OUT ("failed to create integer");
    for (int i = 0; i < depth; i++) {
        json_t *n = json_pack ("[o]", o);
        if (!n)
            BAIL_OUT ("failed to create nested array");
        o = n;
    }
    return o;
}

static void badargs (void)
{
    json_t *o;
    flux_error_t error;

    /*  A NULL object is not an error, so that the result of a lookup for
     *  an optional value may be passed directly.
     */
    ok (json_check_limits (NULL, 128, 8192, &error) == 0,
        "json_check_limits succeeds on NULL object");

    /*  errp is optional
     */
    o = nested_object (129);
    ok (json_check_limits (o, 128, 8192, NULL) < 0,
        "json_check_limits works with NULL errp");
    json_decref (o);
}

static void depth (void)
{
    json_t *o;
    flux_error_t error;

    o = nested_object (128);
    ok (json_check_limits (o, 128, 0, &error) == 0,
        "json_check_limits accepts object exactly at depth limit");
    json_decref (o);

    o = nested_object (129);
    errno = 0;
    ok (json_check_limits (o, 128, 0, &error) < 0 && errno == EINVAL,
        "json_check_limits rejects object one level over depth limit");
    diag ("%s", error.text);
    json_decref (o);

    o = nested_array (129);
    ok (json_check_limits (o, 128, 0, &error) < 0,
        "json_check_limits rejects nested arrays over depth limit");
    diag ("%s", error.text);
    json_decref (o);

    /*  The #7815 shape: constraints repeatedly wrapped in {"and":[...]}
     */
    o = json_pack ("{s:[]}", "and");
    if (!o)
        BAIL_OUT ("failed to create constraint object");
    for (int i = 0; i < 200; i++) {
        json_t *n = json_pack ("{s:[o]}", "and", o);
        if (!n)
            BAIL_OUT ("failed to wrap constraint object");
        o = n;
    }
    ok (json_check_limits (o, JSON_LIMIT_MAX_DEPTH, 0, &error) < 0,
        "json_check_limits rejects repeatedly wrapped constraints (#7815)");
    diag ("%s", error.text);
    json_decref (o);

    /*  A limit of 0 disables the depth check
     */
    o = nested_object (500);
    ok (json_check_limits (o, 0, 0, &error) == 0,
        "max_depth of 0 disables depth check");
    json_decref (o);
}

static void scalars (void)
{
    json_t *o;
    flux_error_t error;

    /*  Scalars and empty containers have no depth and must always pass
     */
    o = json_integer (42);
    ok (json_check_limits (o, 1, 8192, &error) == 0,
        "json_check_limits accepts a scalar integer");
    json_decref (o);

    o = json_string ("foo");
    ok (json_check_limits (o, 1, 8192, &error) == 0,
        "json_check_limits accepts a scalar string");
    json_decref (o);

    o = json_object ();
    ok (json_check_limits (o, 1, 8192, &error) == 0,
        "json_check_limits accepts an empty object");
    json_decref (o);

    o = json_array ();
    ok (json_check_limits (o, 1, 8192, &error) == 0,
        "json_check_limits accepts an empty array");
    json_decref (o);

    /*  A typical jobspec update is shallow and must pass comfortably
     */
    o = json_pack ("{s:s s:i}",
                   "attributes.system.queue", "batch",
                   "attributes.system.duration", 3600);
    if (!o)
        BAIL_OUT ("failed to create update object");
    ok (json_check_default_limits (o, &error) == 0,
        "json_check_default_limits accepts a typical jobspec update");
    json_decref (o);
}

/*  An optional value looked up in a containing object may be passed
 *  directly, which is how an eventlog entry context is checked.
 */
static void optional_value (void)
{
    json_t *o;
    flux_error_t error;

    o = json_pack ("{s:s}", "name", "test");
    if (!o)
        BAIL_OUT ("failed to create entry");
    ok (json_check_default_limits (json_object_get (o, "context"),
                                   &error) == 0,
        "json_check_limits succeeds when optional value is absent");

    if (json_object_set_new (o, "context", nested_object (129)) < 0)
        BAIL_OUT ("failed to set context");
    errno = 0;
    ok (json_check_default_limits (json_object_get (o, "context"),
                                   &error) < 0 && errno == EINVAL,
        "json_check_limits rejects over-limit optional value");
    diag ("%s", error.text);

    json_decref (o);
}

static void size (void)
{
    json_t *o;
    flux_error_t error;
    char *buf;
    /*  Two strings of this length exceed the limit, one does not.
     */
    const size_t len = (JSON_LIMIT_MAX_SIZE / 2) + 1;

    if (!(buf = malloc (len + 1)))
        BAIL_OUT ("out of memory");
    memset (buf, 'x', len);
    buf[len] = '\0';

    /*  One string fits under the limit
     */
    o = json_pack ("{s:s}", "attributes.system.foo", buf);
    if (!o)
        BAIL_OUT ("failed to create object");
    ok (json_check_limits (o, 0, JSON_LIMIT_MAX_SIZE, &error) == 0,
        "json_check_limits accepts object under size limit");
    json_decref (o);

    /*  Two of them do not
     */
    o = json_pack ("{s:s s:s}",
                   "attributes.system.foo", buf,
                   "attributes.system.bar", buf);
    if (!o)
        BAIL_OUT ("failed to create object");
    errno = 0;
    ok (json_check_limits (o, 0, JSON_LIMIT_MAX_SIZE, &error) < 0
        && errno == EINVAL,
        "json_check_limits rejects object over size limit");
    diag ("%s", error.text);

    /*  A limit of 0 disables the size check
     */
    ok (json_check_limits (o, 0, 0, &error) == 0,
        "max_size of 0 disables size check");
    json_decref (o);

    free (buf);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    badargs ();
    depth ();
    scalars ();
    size ();
    optional_value ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
