/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsdprocess/parse.h"

#define OUTOFRANGEPERCENT \
    "100000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "000000000000000000000000000000000" \
    "00000000000000000000000000000000%"

#define OUTOFRANGENUM "20000000000000000000"

static void test_parse_percent (void)
{
    double percent;
    int ret;

    errno = 0;
    ret = parse_percent (NULL, NULL);
    ok (ret < 0 && errno == EINVAL,
        "parse_percent fails with EINVAL on NULL inputs");

    errno = 0;
    ret = parse_percent ("123", &percent);
    ok (ret < 0 && errno == EINVAL,
        "parse_percent fails with EINVAL on not a percent input");

    errno = 0;
    ret = parse_percent (OUTOFRANGEPERCENT, &percent);
    ok (ret < 0 && errno == ERANGE,
        "parse_percent fails with ERANGE on percent out of range");

    errno = 0;
    ret = parse_percent ("-18%", &percent);
    ok (ret < 0 && errno == EINVAL,
        "parse_percent fails with EINVAL on negative percent");

    errno = 0;
    ret = parse_percent ("110%", &percent);
    ok (ret < 0 && errno == EINVAL,
        "parse_percent fails with EINVAL on percent > 100");

    ret = parse_percent ("0%", &percent);
    ok (ret == 0 && percent == 0.0,
        "parse_percent work with 0%");

    ret = parse_percent ("98%", &percent);
    ok (ret == 0 && percent == 0.98,
        "parse_percent work with 98.0%");

    ret = parse_percent ("100%", &percent);
    ok (ret == 0 && percent == 1.0,
        "parse_percent work with 100%");

    ret = parse_percent ("infinity", &percent);
    ok (ret == 0 && percent == 1.0,
        "parse_percent work with infinity");

    ret = parse_percent ("0.25%", &percent);
    ok (ret == 0 && percent == 0.0025,
        "parse_percent work with 0.25%");

    ret = parse_percent ("50.2%", &percent);
    ok (ret == 0 && percent == 0.502,
        "parse_percent work with 50.2%");
}

static void test_parse_unsigned (void)
{
    uint64_t num;
    int ret;

    errno = 0;
    ret = parse_unsigned (NULL, NULL);
    ok (ret < 0 && errno == EINVAL,
        "parse_unsigned fails with EINVAL on NULL inputs");

    errno = 0;
    ret = parse_unsigned (OUTOFRANGENUM, &num);
    ok (ret < 0 && errno == ERANGE,
        "parse_unsigned fails with ERANGE on num out of range");

    errno = 0;
    ret = parse_unsigned ("0", &num);
    ok (ret < 0 && errno == EINVAL,
        "parse_unsigned fails with EINVAL on zero");

    errno = 0;
    ret = parse_unsigned ("-1000", &num);
    ok (ret < 0 && errno == EINVAL,
        "parse_unsigned fails with EINVAL on negative num");

    errno = 0;
    ret = parse_unsigned ("1000z", &num);
    ok (ret < 0 && errno == EINVAL,
        "parse_unsigned fails with EINVAL on bad suffix");

    errno = 0;
    ret = parse_unsigned ("1000kk", &num);
    ok (ret < 0 && errno == EINVAL,
        "parse_unsigned fails with EINVAL on long suffix");

    ret = parse_unsigned ("1000", &num);
    ok (ret == 0 && num == 1000,
        "parse_unsigned work with just num");

    ret = parse_unsigned ("1000k", &num);
    ok (ret == 0 && num == (1000ULL * 1024ULL),
        "parse_unsigned work with k suffix");

    ret = parse_unsigned ("1000M", &num);
    ok (ret == 0 && num == (1000ULL * 1024ULL * 1024ULL),
        "parse_unsigned work with M suffix");

    ret = parse_unsigned ("1000g", &num);
    ok (ret == 0 && num == (1000ULL * 1024ULL * 1024ULL * 1024ULL),
        "parse_unsigned work with g suffix");

    ret = parse_unsigned ("1000T", &num);
    ok (ret == 0 && num == (1000ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL),
        "parse_unsigned work with T suffix");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_parse_percent ();
    test_parse_unsigned ();

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
