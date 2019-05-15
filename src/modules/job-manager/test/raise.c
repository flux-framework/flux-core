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

#include "src/modules/job-manager/queue.h"
#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/raise.h"

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    ok (raise_check_type ("cancel") == 0, "raise_check_type type=cancel works");
    ok (raise_check_type ("") < 0, "raise_check_type type=empty fails");
    ok (raise_check_type (" cancel") < 0,
        "raise_check_type type=word with leading space fails");
    ok (raise_check_type ("cancel ") < 0,
        "raise_check_type type=word with trailing space fails");
    ok (raise_check_type ("can cel") < 0,
        "raise_check_type type=word with embedded space fails");
    ok (raise_check_type ("can\tcel") < 0,
        "raise_check_type type=word with embedded tab fails");
    ok (raise_check_type ("cancel\n") < 0,
        "raise_check_type type=word with trailing newline fails");

    ok (raise_check_severity (0) == 0, "raise_check_severity sev=0 works");
    ok (raise_check_severity (7) == 0, "raise_check_severity sev=7 works");
    ok (raise_check_severity (8) < 0, "raise_check_severity sev=8 fails");
    ok (raise_check_severity (-1) < 0, "raise_check_severity sev=-1 fails");

    ok (raise_allow (FLUX_ROLE_OWNER, 42, 43) == 0,
        "raise_allow permits instance owner");
    ok (raise_allow (FLUX_ROLE_USER, 42, 42) == 0, "raise_allow permits job owner");
    ok (raise_allow (FLUX_ROLE_USER, 42, 43) < 0,
        "raise_allow denies guest non-job owner");
    ok (raise_allow (FLUX_ROLE_NONE, FLUX_USERID_UNKNOWN, 43) < 0,
        "raise_allow denies default message creds");

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
