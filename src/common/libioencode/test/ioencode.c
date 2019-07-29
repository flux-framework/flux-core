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

#include "src/common/libtap/tap.h"
#include "src/common/libioencode/ioencode.h"

void basic_corner_case (void)
{
    errno = 0;
    ok (ioencode (NULL, -1, NULL, -1, false) == NULL
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
    int rank;
    char *data;
    int len;
    bool eof;

    ok ((o = ioencode ("stdout", 1, "foo", 3, false)) != NULL,
        "ioencode success (data, eof = false)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (!strcmp (stream, "stdout")
        && rank == 1
        && !strcmp (data, "foo")
        && len == 3
        && eof == false,
        "iodecode returned correct info");
    free (data);
    json_decref (o);

    ok ((o = ioencode ("stdout", 2, "bar", 3, true)) != NULL,
        "ioencode success (data, eof = true)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (!strcmp (stream, "stdout")
        && rank == 2
        && !strcmp (data, "bar")
        && len == 3
        && eof == true,
        "iodecode returned correct info");
    free (data);
    json_decref (o);

    ok ((o = ioencode ("stderr", 3, NULL, 0, true)) != NULL,
        "ioencode success (no data, eof = true)");
    ok (!iodecode (o, &stream, &rank, &data, &len, &eof),
        "iodecode success");
    ok (!strcmp (stream, "stderr")
        && rank == 3
        && data == NULL
        && len == 0
        && eof == true,
        "iodecode returned correct info");
    free (data);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_corner_case ();
    basic ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
