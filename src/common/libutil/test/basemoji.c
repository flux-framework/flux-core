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
#include "src/common/libutil/basemoji.h"
#include "ccan/str/str.h"

static const char invalid[] = "ƒ1234";
static const char valid[] = "😪🏭🐭🍑👨";

struct basemoji_test {
    uint64_t id;
    const char *result;
} basemoji_tests[] =  {
    { 0,                       "😃" },
    { 1,                       "😄" },
    { 1234,                    "😁👌" },
    { 65535,                   "💁📚" },
    { 12342435,                "😠🙇📍" },
    { 2034152287593,           "😪🏭🐭🍑👨" },
    { 21900760568561664,       "🎆🍧🎆🐾🕓😃" },
    { 18446743892750589633ULL, "🚹💗🔥😜💟🎱💃" },
    { 18446744073709551615ULL, "🚹💗💧👗😷📷📚" },
    { 0,                        NULL },
};

void test_basic (void)
{
    struct basemoji_test *tp = &basemoji_tests[0];
    char buf[30];

    while (tp->result != NULL) {
        uint64_t id;

        ok (uint64_basemoji_encode (tp->id, buf, sizeof (buf)) == 0,
            "uint64_basemoji_encode (%ju)",
            tp->id);
        ok (strcmp (buf, tp->result) == 0,
            "result: %s, wanted: %s",
            buf,
            tp->result);
        ok (is_basemoji_string (tp->result),
            "is_basemoji_string (%s) works",
            tp->result);
        ok (uint64_basemoji_decode (buf, &id) == 0,
            "uint64_basemoji_decode worked");
        ok (id == tp->id,
            "expected %ju got %ju",
            (uintmax_t) tp->id,
            (uintmax_t) id);
        ++tp;
    }
}

void test_errors (void)
{
    char buf[30];
    uint64_t id;

    ok (uint64_basemoji_encode (0, NULL, 0) < 0 && errno == EINVAL,
        "uint64_basemoji_encode (0, NULL, 0) fails with EINVAL");
    ok (uint64_basemoji_encode (0, buf, -1) < 0 && errno == EINVAL,
        "uint64_basemoji_encode (0, buf, -1) fails with EINVAL");

    ok (uint64_basemoji_encode (0, buf, 3) < 0 && errno == EOVERFLOW,
        "uint64_basemoji_encode (0, buf, 3) fails with EOVERFLOW");
    ok (uint64_basemoji_encode (UINT64_MAX, buf, 28) < 0
        && errno == EOVERFLOW,
        "uint64_basemoji_encode (UINT64_MAX, buf, 28) fails with EOVERFLOW");

    ok (uint64_basemoji_decode (NULL, NULL) < 0 && errno == EINVAL,
        "uint64_basemoji_decode (NULL, NULL) fails with EINVAL");
    ok (uint64_basemoji_decode ("",  NULL) < 0 && errno == EINVAL,
        "uint64_basemoji_decode (\"\", NULL) fails with EINVAL");
    ok (uint64_basemoji_decode (valid,  NULL) < 0 && errno == EINVAL,
        "uint64_basemoji_decode (valid, NULL) fails with EINVAL");
    ok (uint64_basemoji_decode ("f",  &id) < 0 && errno == EINVAL,
        "uint64_basemoji_decode (\"f\", NULL) fails with EINVAL");
    ok (uint64_basemoji_decode (invalid,  &id) < 0 && errno == EINVAL,
        "uint64_basemoji_decode (invalid, &id) fails with EINVAL");

    ok (!is_basemoji_string (invalid),
        "is_basemoji_string (invalid) returns false");
    ok (!is_basemoji_string (""),
        "is_basemoji_string (\"\") returns false");
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
