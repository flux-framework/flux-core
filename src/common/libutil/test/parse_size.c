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
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ccan/array_size/array_size.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/parse_size.h"

struct entry {
    const char *s;
    uint64_t val;
    int errnum;
};

const struct entry testvec[] = {
    // bad
    { "xx", 0, EINVAL },
    { "", 0, EINVAL },
    { "1q", 0, EINVAL },
    { "1kb", 0, EINVAL },
    { "-1", 0, EINVAL },
    { "1E20", 0, EOVERFLOW },
    { "M", 0, EINVAL },
    { "1m", 0, EINVAL },
    { "1g", 0, EINVAL },
    { "nan", 0, EINVAL },
    { "inf", 0, EINVAL },
    { "1b", 0, EINVAL },
    // good
    { "0", 0, 0 },
    { "0K", 0, 0 },
    { "077", 63, 0 },
    { "0xff", 255, 0 },
    { "+42", 42, 0 },
    { "1", 1, 0 },
    { "1E2", 100, 0 },
    { "4k", 4096, 0 },
    { "1M", 1048576, 0 },
    { "2G", 2147483648, 0 },
    { "0.5k", 512, 0 },
    { "4T", 4398046511104, 0 },
    { "18446744073709551615", UINT64_MAX, 0 },
    { "  42", 42, 0 },
    { "1P", 1125899906842624, 0 },
    { "0.5E", 576460752303423488, 0 },
};

static void test_parse (void)
{
    uint64_t val;
    int rc;

    lives_ok ({parse_size (NULL, &val);},
        "parse_size input=NULL doesn't crash");
    lives_ok ({parse_size ("x", NULL);},
        "parse_size value=NULL doesn't crash");

    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        val = 0;
        errno = 0;
        rc = parse_size (testvec[i].s, &val);
        if (testvec[i].errnum == 0) {
            ok (rc == 0 && val == testvec[i].val,
                "parse_size val=%s works", testvec[i].s);
            if (rc == 0 && val != testvec[i].val)
                diag ("got %ju", (uintmax_t)val);
        }
        else {
            ok (rc == -1 && errno == testvec[i].errnum,
                "parse_size val=%s fails with errno=%d",
                testvec[i].s,
                testvec[i].errnum);
        }
    }
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);
    test_parse ();
    done_testing ();
    return 0;
}


// vi: ts=4 sw=4 expandtab
