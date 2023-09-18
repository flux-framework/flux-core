/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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

#include <flux/core.h>
#include "monitor.h"

#include "src/common/libtap/tap.h"

void test_badargs (void)
{
    /* Note: these are stubbed for older libzmq (e.g. centos 7),
     * so checking for errno == EINVAL is not going to happen there.
     */
    ok (zmqutil_monitor_create (NULL, NULL, NULL, NULL, NULL) == NULL,
        "zmqutil_monitor_create sock=NULL fails");

    lives_ok({zmqutil_monitor_destroy (NULL);},
        "zmqutil_monitor_destroy sock=NULL doesn't crash");

    ok (zmqutil_monitor_get (NULL, NULL) < 0,
        "zmqutil_monitor_get mon=NULL fails");

    lives_ok({zmqutil_monitor_iserror (NULL);},
        "zmqutil_monitor_iserror mevent=NULL doesn't crash");

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_badargs ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
