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

#include "job.h"

flux_future_t *flux_job_wait (flux_t *h, flux_jobid_t id)
{
    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "job-manager.wait",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:I}",
                          "id",
                          id);
}

int flux_job_wait_get_status (flux_future_t *f,
                              bool *successp,
                              const char **errstrp)
{
    int success;
    const char *errstr;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:b s:s}",
                             "success",
                             &success,
                             "errstr",
                             &errstr) < 0)
        return -1;
    if (successp)
        *successp = success ? true : false;
    if (errstrp)
        *errstrp = errstr;
    return 0;
}

int flux_job_wait_get_id (flux_future_t *f, flux_jobid_t *jobid)
{
    flux_jobid_t id;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:I}",
                             "id", &id) < 0)
        return -1;
    if (jobid)
        *jobid = id;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
