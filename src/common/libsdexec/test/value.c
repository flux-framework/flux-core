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
#include <stdarg.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "value.h"

static json_t *load (const char *s)
{
    json_t *o;
    if (!(o = json_loads (s, JSON_DECODE_ANY | JSON_ALLOW_NUL, NULL)))
        BAIL_OUT ("could not parse %s", s);
    return o;
}

/* N.B. type is int, not char, because a char parameter undergoes
 * default argument promotion, which is undefined behavior with
 * va_start and an error on clang.
 */
static json_t *encode (int type, ...)
{
    va_list ap;
    json_t *o;

    va_start (ap, type);
    o = sdexec_value_encode (type, &ap);
    va_end (ap);
    return o;
}

void test_basic (void)
{
    json_t *o = load ("[255,true,-32768,65535,-2147483648,4294967295,"
                      "\"-9223372036854775808\",\"18446744073709551615\","
                      "2.5,3,\"str\",\"/a/b\",\"a{sv}\"]");
    uint8_t y;
    int b;
    int16_t n;
    uint16_t q;
    int32_t i;
    uint32_t u;
    int64_t x;
    uint64_t t;
    double d;
    int h;
    const char *s, *op, *g;

    ok (sdexec_params_read (o,
                            "ybnqiuxtdhsog",
                            &y,
                            &b,
                            &n,
                            &q,
                            &i,
                            &u,
                            &x,
                            &t,
                            &d,
                            &h,
                            &s,
                            &op,
                            &g) == 0
        && y == 255 && b == 1 && n == INT16_MIN && q == UINT16_MAX
        && i == INT32_MIN && u == UINT32_MAX && x == INT64_MIN
        && t == UINT64_MAX && d == 2.5 && h == 3
        && streq (s, "str") && streq (op, "/a/b") && streq (g, "a{sv}"),
        "sdexec_params_read reads every basic type at its limits");
    json_decref (o);

    o = load ("[1]");
    ok (sdexec_params_read (o, "d", &d) == 0 && d == 1.0,
        "sdexec_params_read accepts an integer for d");
    json_decref (o);
}

struct bad {
    const char *sig;
    const char *params;
};

/* Selected RFC 52 invalid params vectors.
 */
static const struct bad bad_tab[] = {
    { "ss", "[\"one\"]" },
    { "s", "[\"one\",\"two\"]" },
    { "i", "[\"42\"]" },
    { "i", "[1.5]" },
    { "b", "[1]" },
    { "s", "[null]" },
    { "y", "[256]" },
    { "q", "[-1]" },
    { "u", "[4294967296]" },
    { "t", "[42]" },
    { "t", "[\"-1\"]" },
    { "t", "[\"18446744073709551616\"]" },
    { "x", "[\"9223372036854775808\"]" },
    { "t", "[\"007\"]" },
    { "x", "[\"+5\"]" },
    { "x", "[\"-0\"]" },
    { "t", "[\" 5\"]" },
    { "t", "[\"0x10\"]" },
    { "t", "[\"\"]" },
    { "h", "[-1]" },
    { "(si)", "[[\"x\"]]" },
    { "as", "[\"a\"]" },
    { "a{sv}", "[[]]" },
    { "a{uv}", "[{}]" },
};

void test_bad (void)
{
    for (int i = 0; i < sizeof (bad_tab) / sizeof (bad_tab[0]); i++) {
        json_t *o = load (bad_tab[i].params);
        union { int i; int64_t x; uint64_t t; const char *s; json_t *j; } a, b;
        errno = 0;
        ok (sdexec_params_read (o, bad_tab[i].sig, &a, &b, &a, &b) < 0
            && errno == EPROTO,
            "sdexec_params_read %s %s fails with EPROTO",
            bad_tab[i].sig, bad_tab[i].params);
        json_decref (o);
    }
}

void test_containers (void)
{
    json_t *o;
    const char *name, *path;
    uint32_t job;
    json_t *a;
    json_t *dict;

    o = load ("[\"a.service\",\"desc\",\"loaded\",\"active\",\"running\","
              "\"\",\"/org/x/a\",7,\"\",\"/\"]");
    const char *s2, *s3, *s4, *s5, *s6, *s9, *s10;
    ok (sdexec_value_read (o,
                           "(ssssssouso)",
                           &name,
                           &s2,
                           &s3,
                           &s4,
                           &s5,
                           &s6,
                           &path,
                           &job,
                           &s9,
                           &s10) == 0
        && streq (name, "a.service") && streq (path, "/org/x/a")
        && job == 7 && streq (s10, "/"),
        "sdexec_value_read reads a struct");
    json_decref (o);

    o = load ("[[1,2],{\"k\":[\"s\",\"v\"]},[[7,[\"b\",true]]]]");
    ok (sdexec_params_read (o, "aia{sv}a{uv}", &a, &dict, &a) == 0
        && json_is_array (a) && json_is_object (dict),
        "sdexec_params_read returns arrays and dicts as JSON");
    json_decref (o);
}

void test_variant (void)
{
    json_t *o;
    uint64_t t;
    const char *s;
    json_t *a;

    o = load ("[\"t\",\"18446744073709551615\"]");
    ok (sdexec_variant_read (o, "t", &t) == 0 && t == UINT64_MAX,
        "sdexec_variant_read works");
    errno = 0;
    ok (sdexec_variant_read (o, "u", &t) < 0 && errno == EPROTO,
        "sdexec_variant_read with a different type fails with EPROTO");
    json_decref (o);

    o = load ("[\"s\",[\"v\",[\"s\",\"inner\"]]]");
    ok (sdexec_params_read (o, "sv", &s, "v", "s", &s) == 0
        && streq (s, "inner"),
        "sdexec_params_read reads nested variants");
    json_decref (o);

    o = load ("[[\"a(ss)\",[[\"/dev/null\",\"rw\"]]]]");
    ok (sdexec_params_read (o, "v", "a(ss)", &a) == 0
        && json_array_size (a) == 1,
        "sdexec_params_read reads an a(ss) variant");
    json_decref (o);

    o = load ("[\"s\"]");
    errno = 0;
    ok (sdexec_variant_read (o, "s", &s) < 0 && errno == EPROTO,
        "sdexec_variant_read with no value fails with EPROTO");
    json_decref (o);

    o = load ("[\"s\",\"inner\",\"extra\"]");
    errno = 0;
    ok (sdexec_variant_read (o, "s", &s) < 0 && errno == EPROTO,
        "sdexec_variant_read with extra element fails with EPROTO");
    json_decref (o);
}

void test_roundtrip (void)
{
    json_t *o;
    uint64_t t;
    int64_t x;
    uint32_t u;
    const char *s;

    o = encode ('t', (uint64_t)UINT64_MAX);
    ok (o && sdexec_value_read (o, "t", &t) == 0 && t == UINT64_MAX,
        "encoded t reads back");
    json_decref (o);
    o = encode ('x', (int64_t)INT64_MIN);
    ok (o && sdexec_value_read (o, "x", &x) == 0 && x == INT64_MIN,
        "encoded x reads back");
    json_decref (o);
    o = encode ('u', (uint32_t)UINT32_MAX);
    ok (o && sdexec_value_read (o, "u", &u) == 0 && u == UINT32_MAX,
        "encoded u reads back");
    json_decref (o);
    o = encode ('s', "hello");
    ok (o && sdexec_value_read (o, "s", &s) == 0 && streq (s, "hello"),
        "encoded s reads back");
    json_decref (o);
    ok (encode ('s', NULL) == NULL,
        "sdexec_value_encode s=NULL fails");
    ok (encode ('v', 1) == NULL,
        "sdexec_value_encode of a non-basic type fails");
}

void test_inval (void)
{
    json_t *o = load ("[1]");
    int32_t i;

    errno = 0;
    ok (sdexec_value_read (NULL, "i", &i) < 0 && errno == EINVAL,
        "sdexec_value_read val=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, NULL) < 0 && errno == EINVAL,
        "sdexec_value_read type=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, "ii", &i, &i) < 0 && errno == EINVAL,
        "sdexec_value_read with two types fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, "(i", &i) < 0 && errno == EINVAL,
        "sdexec_value_read with a bad type fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, "{sv}", &i) < 0 && errno == EINVAL,
        "sdexec_value_read with bare dict entry type fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, "a{vs}", &i) < 0 && errno == EINVAL,
        "sdexec_value_read with non-basic dict key fails with EINVAL");
    errno = 0;
    ok (sdexec_value_read (o, "a{svs}", &i) < 0 && errno == EINVAL,
        "sdexec_value_read with 3-member dict entry fails with EINVAL");
    errno = 0;
    ok (sdexec_params_read (NULL, "i", &i) < 0 && errno == EINVAL,
        "sdexec_params_read params=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_variant_read (NULL, "i", &i) < 0 && errno == EINVAL,
        "sdexec_variant_read val=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_reply_read (NULL, "i", &i) < 0 && errno == EINVAL,
        "sdexec_reply_read f=NULL fails with EINVAL");
    json_decref (o);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_bad ();
    test_containers ();
    test_variant ();
    test_roundtrip ();
    test_inval ();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
