/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "util.h"

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    test_server_environment_init ("log-test");

    if (!(h = test_server_create (NULL, NULL)))
        BAIL_OUT ("could not create test server");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly failed");

    errno = 1234;
    flux_log_error (h, "hello world");
    ok (errno == 1234, "flux_log_error didn't clobber errno");

    errno = 1236;
    flux_log (h, LOG_INFO, "errlo orlk");
    ok (errno == 1236, "flux_log didn't clobber errno");

    ok (flux_log (NULL, LOG_INFO, "# flux_t=NULL") == 0, "flux_log h=NULL works");

    test_server_stop (h);
    flux_close (h);
    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
