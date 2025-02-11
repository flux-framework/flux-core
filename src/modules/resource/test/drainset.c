/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <string.h>

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "src/modules/resource/drainset.h"

static void check_drainset (struct drainset *ds,
                           const char *json_str)
{
    char *s;
    json_t *o = drainset_to_json (ds);
    json_t *expected = json_loads (json_str, 0, NULL);
    struct drainset *ds2;
    if (!o || !expected)
        BAIL_OUT ("drainset_to_json failed");
    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed");
    diag ("drainset_to_json = %s", s);
    diag ("expected =         %s", json_str);
    ok (json_equal (expected, o),
        "drainset_to_json got expected result");

    if (!(ds2 = drainset_from_json (o)))
        BAIL_OUT ("drainset_from_json failed");
    json_decref (o);

    ok (true, "drainset_from_json worked");
    if (!(o = drainset_to_json (ds2)))
        BAIL_OUT ("drainset_to_json failed");

    ok (json_equal (expected, o),
        "drainset_to_json after from_json got expected result");

    drainset_destroy (ds2);
    json_decref (o);
    json_decref (expected);
    free (s);
}

static void test_empty ()
{
    struct drainset *ds = drainset_create ();
    if (!ds)
        BAIL_OUT ("drainset_create failed");
    diag ("empty drainset should return empty JSON object");
    check_drainset (ds, "{}");

    ok (drainset_undrain (ds, 0) < 0 && errno == ENOENT,
        "drainset_undrain() fails on empty drainset");

    drainset_destroy (ds);
}

static void test_basic ()
{
    struct drainset *ds = drainset_create ();
    if (!ds)
        BAIL_OUT ("drainset_create failed");

    ok (drainset_drain_rank (NULL, 0, 1234.0, NULL) < 0 && errno == EINVAL,
        "drainset_drain_rank (NULL, ...) returns EINVAL");

    for (unsigned int i = 0; i < 8; i++) {
        ok (drainset_drain_rank (ds, i, 1234.0, "test") == 0,
            "drainset_drain_rank: rank=%u", i);
    }
    check_drainset (ds,
                    "{\"0-7\":{\"timestamp\":1234.0,\"reason\":\"test\"}}");

    ok (drainset_undrain (ds, 3) == 0,
        "drainset_undrain(3) works");

    check_drainset (ds,
                    "{\"0-2,4-7\":{\"timestamp\":1234.0,\"reason\":\"test\"}}");

    ok (drainset_undrain (ds, 0) == 0,
        "drainset_undrain(0) works");

    check_drainset (ds,
                    "{\"1-2,4-7\":{\"timestamp\":1234.0,\"reason\":\"test\"}}");

    drainset_destroy (ds);
}

static void test_multiple ()
{
    struct drainset *ds = drainset_create ();

    if (!ds)
        BAIL_OUT ("drainset_create failed");

    ok (drainset_drain_rank (ds, 0, 1234.0, "test") == 0,
        "drainset_drain_rank: rank=0");
    ok (drainset_drain_rank (ds, 1, 2345.0, "test") == 0,
        "drainset_drain_rank: rank=1");
    ok (drainset_drain_rank (ds, 2, 1234.0, "test1") == 0,
        "drainset_drain_rank: rank=1");
    ok (drainset_drain_rank (ds, 3, 1234.0, "test") == 0,
        "drainset_drain_rank: rank=0");
    ok (drainset_drain_rank (ds, 4, 1234.0, NULL) == 0,
        "drainset_drain_rank: rank=1");

    check_drainset (ds,
                    "{\"0,3\":{\"timestamp\":1234.0,\"reason\":\"test\"},"
                    "\"1\":{\"timestamp\":2345.0,\"reason\":\"test\"},"
                    "\"2\":{\"timestamp\":1234.0,\"reason\":\"test1\"},"
                    "\"4\":{\"timestamp\":1234.0,\"reason\":\"\"}}");

    ok (drainset_undrain (ds, 1) == 0,
        "drainset_undrain (1) works");

    check_drainset (ds,
                    "{\"0,3\":{\"timestamp\":1234.0,\"reason\":\"test\"},"
                    "\"2\":{\"timestamp\":1234.0,\"reason\":\"test1\"},"
                    "\"4\":{\"timestamp\":1234.0,\"reason\":\"\"}}");

    /* drainset_drain_ex() invalid args
     */
    ok (drainset_drain_ex (NULL, 0, 0., NULL, 0) < 0 && errno == EINVAL,
        "drainset_drain_ex() with invalid args returns EINVAL");

    /* overwrite=1 - update reason but not timestamp
     */
    ok (drainset_drain_ex (ds, 0, 1235.0, "test2", 1) == 0,
        "drainset_drain_ex with overwrite=1 works");

    check_drainset (ds,
                    "{\"3\":{\"timestamp\":1234.0,\"reason\":\"test\"},"
                    "\"0\":{\"timestamp\":1234.0,\"reason\":\"test2\"},"
                    "\"2\":{\"timestamp\":1234.0,\"reason\":\"test1\"},"
                    "\"4\":{\"timestamp\":1234.0,\"reason\":\"\"}}");

    /* overwrite=2 - update reason and timestamp
     */
    ok (drainset_drain_ex (ds, 4, 2345.0, "foo", 2) == 0,
        "drainset_drain_ex with overwrite=1 works");

    check_drainset (ds,
                    "{\"3\":{\"timestamp\":1234.0,\"reason\":\"test\"},"
                    "\"0\":{\"timestamp\":1234.0,\"reason\":\"test2\"},"
                    "\"2\":{\"timestamp\":1234.0,\"reason\":\"test1\"},"
                    "\"4\":{\"timestamp\":2345.0,\"reason\":\"foo\"}}");

    drainset_destroy (ds);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    test_empty ();
    test_basic ();
    test_multiple ();
    done_testing ();
    return (0);
}


/*
 * vi:ts=4 sw=4 expandtab
 */
