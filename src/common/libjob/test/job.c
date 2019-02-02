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
    flux_t *h = (flux_t *)(uintptr_t)42; // fake but non-NULL

    /* flux_job_submit */

    errno = 0;
    ok (flux_job_submit (NULL, NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_submit with NULL args fails with EINVAL");

    errno = 0;
    ok (flux_job_submit_get_id (NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_submit_get_id with NULL args fails with EINVAL");

    /* flux_job_list */

    errno = 0;
    ok (flux_job_list (NULL, 0, "{}") == NULL && errno == EINVAL,
        "flux_job_list h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, -1, "{}") == NULL && errno == EINVAL,
        "flux_job_list max_entries=-1 fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_list json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_list json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, "wrong") == NULL && errno == EINVAL,
        "flux_job_list json_str=(inval JSON) fails with EINVAL");

    /* flux_job_cancel */

    errno = 0;
    ok (flux_job_cancel (NULL, 0, FLUX_JOB_PURGE) == NULL && errno == EINVAL,
        "flux_job_purge h=NULL fails with EINVAL");

    /* flux_job_set_priority */

    errno = 0;
    ok (flux_job_set_priority (NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_set_priority h=NULL fails with EINVAL");

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
