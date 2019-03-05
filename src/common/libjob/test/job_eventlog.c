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

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/job.h"

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errno = EINVAL;
    ok (!flux_job_eventlog_lookup (NULL, 0, 0)
        && errno == EINVAL,
        "flux_job_eventlog_lookup fails with EINVAL on bad input");

    errno = EINVAL;
    ok (flux_job_eventlog_lookup_get (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_job_eventlog_lookup_get fails with EINVAL on bad input");

    errno = EINVAL;
    ok (flux_job_eventlog_lookup_cancel (NULL) < 0
        && errno == EINVAL,
        "flux_job_eventlog_lookup_cancel fails with EINVAL on bad input");

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
