/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <errno.h>

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libioencode/ioencode.h"

void basic_corner_case (void)
{
    errno = 0;
    ok (ioencode (NULL, NULL, NULL, -1, false) == NULL
        && errno == EINVAL,
        "ioencode returns EINVAL on bad input");

    errno = 0;
    ok (iodecode (NULL, NULL, NULL, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "iodecode returns EINVAL on bad input");
}

void basic (void)
{
    json_t *o;
    const char *stream;
    const char *rank;
    char *data;
    int len;
    bool eof;

    ok ((o = ioencode ("stdout", "1", "foo", 3, false)) != NULL,
        "ioencode success (data, eof = false)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (streq (stream, "stdout")
        && streq (rank, "1")
        && len == 3
        && !strncmp (data, "foo", len)
        && eof == false,
        "iodecode returned correct info");
    free (data);
    json_decref (o);

    ok ((o = ioencode ("stdout", "[0-8]", "bar", 3, true)) != NULL,
        "ioencode success (data, eof = true)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (streq (stream, "stdout")
        && streq (rank, "[0-8]")
        && len == 3
        && !strncmp (data, "bar", len)
        && eof == true,
        "iodecode returned correct info");
    free (data);

    ok (iodecode (o, &stream, &rank, NULL, &len, &eof) == 0,
        "iodecode can be passed NULL data to query len");
    ok (streq (stream, "stdout")
        && streq (rank, "[0-8]")
        && len == 3
        && eof == true,
        "iodecode returned correct info");
    json_decref (o);

    ok ((o = ioencode ("stderr", "[4,5]", NULL, 0, true)) != NULL,
        "ioencode success (no data, eof = true)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (streq (stream, "stderr")
        && streq (rank, "[4,5]")
        && data == NULL
        && len == 0
        && eof == true,
        "iodecode returned correct info");
    free (data);

    ok (iodecode (o, &stream, &rank, NULL, &len, &eof) == 0,
        "iodecode can be passed NULL data to query len");
    ok (streq (stream, "stderr")
        && streq (rank, "[4,5]")
        && len == 0
        && eof == true,
        "iodecode returned correct info");
    json_decref (o);
}

static void binary_data (void)
{
    json_t *o = NULL;
    const char *stream;
    const char *rank;
    char *data;
    char *encoding = NULL;
    int len;
    bool eof;

    /*  \xed\xbf\xbf is not a valid utf-8 codepoint */
    const char buffer[15] = "\xed\xbf\xbf\x4\x5\x6\x7\x8\x9\xa\xb\xc\xd\xe\xf";

    ok ((o = ioencode ("stdout", "1", buffer, sizeof (buffer), false)) != NULL,
        "ioencode of binary data works");
    ok (json_unpack (o, "{s:s}", "encoding", &encoding) == 0,
        "ioencode used alternate encoding");
    is (encoding, "base64",
        "ioencode encoded data as base64");
    ok (iodecode (o, &stream, &rank, &data, &len, &eof) == 0,
        "iodecode success");
    is (rank, "1",
        "rank is correct");
    ok (len == sizeof (buffer),
        "len is correct");
    ok (eof == false,
        "eof is correct");
    ok (memcmp (data, buffer, len) == 0,
        "data matches");
    free (data);
    ok (iodecode (o, &stream, &rank, NULL, &len, &eof) == 0,
        "iodecode can be passed NULL data to query len");
    ok (len == sizeof (buffer),
        "len is correct");
    json_decref (o);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_corner_case ();
    basic ();
    binary_data ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
