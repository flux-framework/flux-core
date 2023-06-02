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
#include <stdint.h>

#include "sigutil.h"
#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

static void test_errors ()
{
    errno = 0;
    ok (sigutil_signum (NULL) < 0 && errno == EINVAL,
        "sigutil_signum (NULL) returns EINVAL");
    errno = 0;
    ok (sigutil_signum ("0") < 0 && errno == EINVAL,
        "sigutil_signum (\"0\") returns EINVAL");
    errno = 0;
    ok (sigutil_signum ("-12") < 0 && errno == EINVAL,
        "sigutil_signum (\"-12\") returns EINVAL");
    errno = 0;
    ok (sigutil_signum ("SIGFOO") < 0 && errno == ENOENT,
        "sigutil_signum() with invalid name returns ENOENT");

    errno = 0;
    ok (sigutil_signame (0) == NULL && errno == EINVAL,
        "sigutil_signame (0) returns EINVAL");
    errno = 0;
    ok (sigutil_signame (-1) == NULL && errno == EINVAL,
        "sigutil_signame (0) returns EINVAL");
    errno = 0;
    ok (sigutil_signame (12345) == NULL && errno == ENOENT,
        "sigutil_signame (12345) returns EINVAL");
}

static void test_basic ()
{
    ok (sigutil_signum ("1") == 1,
        "sigutil_signum() works with string that is a number");
    ok (sigutil_signum ("SIGKILL") == SIGKILL,
        "sigutil_signum (\"SIGKILL\") works");
    ok (sigutil_signum ("KILL") == SIGKILL,
        "sigutil_signum (\"KILL\") works");
    ok (sigutil_signum ("SIGSYS") == SIGSYS,
        "sigutil_signum (\"SIGSYS\") works");
    ok (sigutil_signum ("SYS") == SIGSYS,
        "sigutil_signum (\"SYS\") works");

    is (sigutil_signame (SIGKILL), "SIGKILL",
        "sigutil_signame (SIGKILL) works");
    is (sigutil_signame (SIGHUP), "SIGHUP",
        "sigutil_signame (SIGHUP) works");
    is (sigutil_signame (SIGSYS), "SIGSYS",
        "sigutil_signame (SIGSYS) works");
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
