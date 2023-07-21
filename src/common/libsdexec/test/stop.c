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

#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "stop.h"

void test_inval (void)
{
    flux_t *h;

    if (!(h = loopback_create (0)))
        BAIL_OUT ("could not create loopback flux_t handle for testing");

    errno = 0;
    ok (sdexec_stop_unit (NULL, 0, "foo", "bar") == NULL && errno == EINVAL,
        "sdexec_stop_unit h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_stop_unit (h, 0, NULL, "bar") == NULL && errno == EINVAL,
        "sdexec_stop_unit name=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_stop_unit (h, 0, "foo", NULL) == NULL && errno == EINVAL,
        "sdexec_stop_unit mode=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_reset_failed_unit (NULL, 0, "foo") == NULL && errno == EINVAL,
        "sdexec_reset_failed_unit h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_reset_failed_unit (h, 0, NULL) == NULL && errno == EINVAL,
        "sdexec_reset_failed_unit name=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_kill_unit (NULL, 0, "foo", "bar", 0) == NULL && errno == EINVAL,
        "sdexec_kill_unit h=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_kill_unit (h, 0, NULL, "bar", 0) == NULL && errno == EINVAL,
        "sdexec_kill_unit name=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_kill_unit (h, 0, "foo", NULL, 0) == NULL && errno == EINVAL,
        "sdexec_kill_unit who=NULL fails with EINVAL");

    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
