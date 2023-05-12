/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <locale.h>
#include <stdint.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/base256.h"
#include "ccan/str/str.h"

static const char invalid[] = "😀🐨😀🌳👣🚘😹🐨";
static const char valid[] = "🇫😀🐨😀🌳👣🚘😹🐨";
static const char invalidemoji[] = "🇫😀🐨😀🌳👣🚘ffff";

struct b256_test {
    uint64_t data;
    const char *result;
} b256_tests[] =  {
    { 0,                 "🇫😀🐨😀🐨😀🐨😀🐨" },
    { 1,                 "🇫😁🐨😀🐨😀🐨😀🐨" },
    { 1234,              "🇫👢🐔😀🐨😀🐨😀🐨" },
    { 12342435,          "🇫👅🍳🗨🐨😀🐨😀🐨" },
    { 21900760568561664, "🇫😀🐨😀🌳👣🚘😹🐨" },
    { 0,                 NULL                 },
};

void test_basic (void)
{
    struct b256_test *tp = &b256_tests[0];
    int encodelen = BASE256_ENCODED_SIZE (sizeof (tp->data));
    diag ("encode size for uint64_t is %d", encodelen);

    while (tp->result != NULL) {
        char buf[encodelen];
        uint64_t data;

        ok (is_base256 (tp->result),
            "is_base256 (%s) works",
            tp->result);

        ok (base256_encode (buf, encodelen, &tp->data, sizeof (tp->data)) > 0,
            "base256_encode (%ju)",
            tp->data);
        ok (strcmp (buf, tp->result) == 0,
            "result: %s, wanted: %s",
            buf,
            tp->result);
        ok (base256_decode ((char *)&data, sizeof (data), buf) > 0,
            "base256_decode worked");
        ok (data == tp->data,
            "expected %ju got %ju",
            (uintmax_t) tp->data,
            (uintmax_t) data);
        ++tp;
    }
}

void test_errors (void)
{
    char buf[256];
    ok (base256_encode (NULL, 0, NULL, 0) < 0 && errno == EINVAL,
        "base256_encode (NULL, 0, NULL, 0) fails with EINVAL");
    ok (base256_encode (buf, -1, "", 0) < 0 && errno == EINVAL,
        "base256_encode (buf, -1, \"\", 0) fails with EINVAL");
    ok (base256_encode (buf, 256, "", -1) < 0 && errno == EINVAL,
        "base256_encode (buf, 256, \"\", -1) fails with EINVAL");

    ok (base256_decode (NULL, 0, NULL) < 0 && errno == EINVAL,
        "base256_decode (NULL, 0, NULL) fails with EINVAL");
    ok (base256_decode (buf, 256, NULL) < 0 && errno == EINVAL,
        "base256_decode (buf, 256, NULL) fails with EINVAL");
    ok (base256_decode (buf, 0, valid) < 0 && errno == EINVAL,
        "base256_decode (buf, 0, valid) fails with EINVAL");
    ok (base256_decode (buf, 256, invalid) < 0 && errno == EINVAL,
        "base256_decode (buf, 0, invalid) fails with EINVAL");
    ok (base256_decode (buf, 256, invalidemoji) < 0 && errno == ENOENT,
        "base256_decode (buf, 0, invalidemoji) fails with ENOENT");

    ok (!is_base256 (invalid),
        "is_base256 (invalid) returns false");
    ok (!is_base256 (""),
        "is_base256 (\"\") returns false");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_errors ();
    test_basic ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
