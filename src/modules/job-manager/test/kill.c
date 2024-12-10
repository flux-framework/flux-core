/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <jansson.h>

#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/kill.h"

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    ok (kill_check_signal (SIGKILL) == 0,
        "kill_check_signal signum=SIGKILL works");
    ok (kill_check_signal (-1) < 0,
        "kill_check_signal signum=-1 fails");
    ok (kill_check_signal (NSIG) < 0,
        "kill_check_signal signum=NSIG fails");

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
