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
#include "src/modules/job-manager/raise.h"

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    ok (raise_check_type ("cancel") == 0,
        "raise_check_type type=cancel works");
    ok (raise_check_type ("") < 0,
        "raise_check_type type=empty fails");
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

    ok (raise_check_severity (0) == 0,
        "raise_check_severity sev=0 works");
    ok (raise_check_severity (7) == 0,
        "raise_check_severity sev=7 works");
    ok (raise_check_severity (8) < 0,
        "raise_check_severity sev=8 fails");
    ok (raise_check_severity (-1) < 0,
        "raise_check_severity sev=-1 fails");

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
