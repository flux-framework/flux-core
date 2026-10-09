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
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "proplist.h"

/* Return true if the property array produced by 'pl' equals 'expect'
 * (JSON text).
 */
static bool finish_is (struct sdexec_proplist *pl, const char *expect)
{
    flux_error_t error;
    json_t *props;
    json_t *o;
    bool result;

    if (!(o = json_loads (expect, 0, NULL)))
        BAIL_OUT ("could not parse expected JSON");
    if (!(props = sdexec_proplist_finish (pl, &error))) {
        diag ("finish: %s", error.text);
        json_decref (o);
        return false;
    }
    result = json_equal (props, o);
    if (!result) {
        char *s = json_dumps (props, JSON_COMPACT);
        diag ("%s", s);
        free (s);
    }
    json_decref (props);
    json_decref (o);
    return result;
}

void test_basic (void)
{
    struct sdexec_proplist *pl;
    char expect[1024];

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    ok (finish_is (pl, "[]"),
        "empty proplist finishes as []");
    sdexec_proplist_add (pl, "Y", "y", 255);
    sdexec_proplist_add (pl, "B", "b", 1);
    sdexec_proplist_add (pl, "N", "n", -32768);
    sdexec_proplist_add (pl, "Q", "q", 65535);
    sdexec_proplist_add (pl, "I", "i", -1);
    sdexec_proplist_add (pl, "U", "u", (uint32_t)UINT32_MAX);
    sdexec_proplist_add (pl, "X", "x", (int64_t)INT64_MIN);
    sdexec_proplist_add (pl, "T", "t", (uint64_t)UINT64_MAX);
    sdexec_proplist_add (pl, "D", "d", 2.5);
    sdexec_proplist_add (pl, "S", "s", "hello");
    sdexec_proplist_add (pl, "O", "o", "/a/b");
    sdexec_proplist_add (pl, "G", "g", "a{sv}");
    sdexec_proplist_add (pl, "H", "h", 3);
    snprintf (expect,
              sizeof (expect),
              "[[\"Y\",[\"y\",255]],"
              "[\"B\",[\"b\",true]],"
              "[\"N\",[\"n\",-32768]],"
              "[\"Q\",[\"q\",65535]],"
              "[\"I\",[\"i\",-1]],"
              "[\"U\",[\"u\",4294967295]],"
              "[\"X\",[\"x\",\"-9223372036854775808\"]],"
              "[\"T\",[\"t\",\"18446744073709551615\"]],"
              "[\"D\",[\"d\",2.5]],"
              "[\"S\",[\"s\",\"hello\"]],"
              "[\"O\",[\"o\",\"/a/b\"]],"
              "[\"G\",[\"g\",\"a{sv}\"]],"
              "[\"H\",[\"h\",{\"fd\":3,\"pid\":%d}]]]",
              (int)getpid ());
    ok (finish_is (pl, expect),
        "sdexec_proplist_add encodes every basic type per RFC 52");
    sdexec_proplist_destroy (pl);
}

void test_array (void)
{
    struct sdexec_proplist *pl;
    uint8_t bytes[] = { 0, 1, 255 };
    uint64_t u64[] = { 0, UINT64_MAX };

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add_array (pl, "AllowedCPUs", "y", bytes, 3);
    sdexec_proplist_add_array (pl, "Empty", "y", NULL, 0);
    sdexec_proplist_add_array (pl, "T", "t", u64, 2);
    ok (finish_is (pl,
                   "[[\"AllowedCPUs\",[\"ay\",[0,1,255]]],"
                   "[\"Empty\",[\"ay\",[]]],"
                   "[\"T\",[\"at\",[\"0\",\"18446744073709551615\"]]]]"),
        "sdexec_proplist_add_array works");
    sdexec_proplist_destroy (pl);
}

void test_json (void)
{
    struct sdexec_proplist *pl;
    json_t *val;

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    if (!(val = json_pack ("[[ss]]", "/dev/null", "rw")))
        BAIL_OUT ("could not create JSON value");
    sdexec_proplist_add_json (pl, "DeviceAllow", "a(ss)", val);
    ok (val->refcount == 2,
        "sdexec_proplist_add_json takes its own reference");
    json_decref (val); // the proplist's reference keeps val alive
    ok (finish_is (pl,
                   "[[\"DeviceAllow\",[\"a(ss)\",[[\"/dev/null\",\"rw\"]]]]]"),
        "sdexec_proplist_add_json works");
    sdexec_proplist_destroy (pl);
}

void test_errors (void)
{
    struct sdexec_proplist *pl;
    flux_error_t error;

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add (pl, "Good", "s", "ok");
    sdexec_proplist_add (pl, "Bad", "s", NULL);
    sdexec_proplist_add (pl, "Later", "zz", 42);
    errno = 0;
    ok (sdexec_proplist_finish (pl, &error) == NULL
        && errno == EINVAL
        && streq (error.text, "Bad: invalid value"),
        "first error is sticky and reported by finish");
    sdexec_proplist_destroy (pl);

    struct {
        const char *type;
        const char *msg;
    } bad_add[] = {
        { "v", "Prop: invalid value" },
        { "as", "Prop: type is not a basic type" },
        { "", "Prop: type is not a basic type" },
    };
    for (int i = 0; i < sizeof (bad_add) / sizeof (bad_add[0]); i++) {
        if (!(pl = sdexec_proplist_create ()))
            BAIL_OUT ("could not create proplist");
        sdexec_proplist_add (pl, "Prop", bad_add[i].type, 1);
        errno = 0;
        ok (sdexec_proplist_finish (pl, &error) == NULL
            && errno == EINVAL
            && streq (error.text, bad_add[i].msg),
            "sdexec_proplist_add type=\"%s\" fails", bad_add[i].type);
        sdexec_proplist_destroy (pl);
    }

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add_array (pl, "Prop", "s", "x", 1);
    errno = 0;
    ok (sdexec_proplist_finish (pl, &error) == NULL && errno == EINVAL,
        "sdexec_proplist_add_array with non-fixed-size type fails");
    sdexec_proplist_destroy (pl);

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add_array (pl, "Prop", "y", NULL, 2);
    errno = 0;
    ok (sdexec_proplist_finish (pl, &error) == NULL && errno == EINVAL,
        "sdexec_proplist_add_array with NULL elements fails");
    sdexec_proplist_destroy (pl);

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add_json (pl, "Prop", "as", NULL);
    errno = 0;
    ok (sdexec_proplist_finish (pl, &error) == NULL && errno == EINVAL,
        "sdexec_proplist_add_json with NULL value fails");
    sdexec_proplist_add (pl, NULL, "s", "x");
    sdexec_proplist_destroy (pl);

    if (!(pl = sdexec_proplist_create ()))
        BAIL_OUT ("could not create proplist");
    sdexec_proplist_add (pl, NULL, "s", "x");
    errno = 0;
    ok (sdexec_proplist_finish (pl, &error) == NULL && errno == EINVAL,
        "sdexec_proplist_add with NULL name fails");
    sdexec_proplist_destroy (pl);

    errno = 0;
    ok (sdexec_proplist_finish (NULL, &error) == NULL && errno == EINVAL,
        "sdexec_proplist_finish pl=NULL fails with EINVAL");
    lives_ok ({sdexec_proplist_add (NULL, "a", "s", "x");},
        "sdexec_proplist_add pl=NULL doesn't crash");
    lives_ok ({sdexec_proplist_destroy (NULL);},
        "sdexec_proplist_destroy pl=NULL doesn't crash");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_array ();
    test_json ();
    test_errors ();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
