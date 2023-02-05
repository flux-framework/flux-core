/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libutil/slice.h"

const char *testinput = "ABCD";

struct testent {
    const char *s;
    struct slice slice;
    const char *result;
};

struct testent testvec[] = {
    { "[0:2]",    { .start = 0, .stop = 2,  .step = 1 },   "AB" },
    { "[0:4:2]",  { .start = 0, .stop = 4,  .step = 2 },   "AC" },
    { "[1:]",     { .start = 1, .stop = 4,  .step = 1 },   "BCD" },
    { "[:3]",     { .start = 0, .stop = 3,  .step = 1 },   "ABC" },
    { "[1:3]",    { .start = 1, .stop = 3,  .step = 1 },   "BC" },
    { "[1:3:]",   { .start = 1, .stop = 3,  .step = 1 },   "BC" },
    { "[1:99]",   { .start = 1, .stop = 99, .step = 1 },   "BCD" },
    { "[::2]",    { .start = 0, .stop = 4,  .step = 2 },   "AC" },
    { "[::]",     { .start = 0, .stop = 4,  .step = 1 },   "ABCD" },
    { "[:]",      { .start = 0, .stop = 4,  .step = 1 },   "ABCD" },
    { "[8:]",     { .start = 8, .stop = 4,  .step = 1 },   "" },
    { "[3:1]",    { .start = 3, .stop = 1,  .step = 1 },   "" },
    { "[::-1]",   { .start = 3, .stop = -1, .step = -1 },  "DCBA" },
    { "[-1:0:-1]",{ .start = 3, .stop = 0,  .step = -1 },  "DCB" },
    { "[-3:-1]",  { .start = 1, .stop = 3,  .step = 1 },   "BC" },
    { "[99:0:-1]", { .start = 99,.stop = 0, .step = -1 },  "DCB" },
    { "[0:4:-1]", { .start = 0, .stop = 4,  .step = -1 },  "" },
};

const char *badvec[] = {
    ":",
    "[:",
    ":]",
    "[:]x",
    "x[:]",
    "[]",
};

bool check_parse (struct testent test, const char *in)
{
    size_t slen = strlen (in);
    struct slice sl;

    if (slice_parse (&sl, test.s, slen) < 0) {
        diag ("parse %s failed", test.s);
        return false;
    }
    if (sl.start != test.slice.start) {
        diag ("parse %s: start=%d != %d", test.s, sl.start, test.slice.start);
        return false;
    }
    if (sl.stop != test.slice.stop) {
        diag ("parse %s: stop=%d != %d", test.s, sl.stop, test.slice.stop);
        return false;
    }
    if (sl.step != test.slice.step) {
        diag ("parse %s: step=%d != %d", test.s, sl.step, test.slice.step);
        return false;
    }

    return true;
}

static int string_slice (struct slice *sl, const char *in, char **out)
{
    size_t slen = strlen (in);
    char *s;
    char *cp;
    int i;

    if (!(s = calloc (1, slen + 1)))
        BAIL_OUT ("out of memory");
    cp = s;
    i = slice_first (sl);
    while (i >= 0) {
        if (i >= slen)
            BAIL_OUT ("unexpected slice_first/next index %d", i);
        *cp++ = in[i];
        i = slice_next (sl);
    }
    *out = s;
    return 0;
}

bool check_slice (struct testent test)
{
    struct slice sl;
    char *result;

    if (slice_parse (&sl, test.s, strlen (testinput)) < 0) {
        diag ("parse %s failed", test.s);
        return false;
    }
    if (string_slice (&sl, testinput, &result) < 0) {
        diag ("slice %s failed", test.s);
        return false;
    }
    if (!streq (result, test.result)) {
        diag ("slice %s: %s != %s", test.s, result, test.result);
        free (result);
        return false;
    }
    free (result);
    return true;
}

int main (int argc, char *argv[])
{
    struct slice sl;

    plan (NO_PLAN);

    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        ok (check_parse (testvec[i], testinput),
            "parsed \"%s\"", testvec[i].s);
    }

    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        ok (check_slice (testvec[i]),
            "sliced \"%s\"", testvec[i].s);
    }

    for (int i = 0; i < ARRAY_SIZE (badvec); i++) {
        ok (slice_parse (&sl, badvec[i], strlen (testinput)) < 0,
           "rejected \"%s\"", badvec[i]);
    }

    ok (slice_parse (NULL, "[:]", 4) < 0,
        "slice_parse sl=NULL fails");
    ok (slice_parse (&sl, NULL, 4) < 0,
        "slice_parse s=NULL fails");
    ok (slice_first (NULL) == -1,
        "slice_first sl=NULL returns -1");
    ok (slice_next (NULL) == -1,
        "slice_next sl=NULL returns -1");

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
