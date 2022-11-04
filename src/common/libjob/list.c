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
#include <ctype.h>
#include <flux/core.h>
#include <jansson.h>

#include "job.h"

flux_future_t *flux_job_list (flux_t *h,
                              int max_entries,
                              const char *json_str,
                              uint32_t userid,
                              int states)
{
    flux_future_t *f;
    json_t *o = NULL;
    int valid_states = (FLUX_JOB_STATE_PENDING
                        | FLUX_JOB_STATE_RUNNING
                        | FLUX_JOB_STATE_INACTIVE);
    int saved_errno;

    if (!h || max_entries < 0 || !json_str
           || !(o = json_loads (json_str, 0, NULL))
           || states & ~valid_states) {
        json_decref (o);
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-list.list", FLUX_NODEID_ANY, 0,
                             "{s:i s:o s:i s:i s:i}",
                             "max_entries", max_entries,
                             "attrs", o,
                             "userid", userid,
                             "states", states,
                             "results", 0))) {
        saved_errno = errno;
        json_decref (o);
        errno = saved_errno;
        return NULL;
    }
    return f;
}

flux_future_t *flux_job_list_inactive (flux_t *h,
                                       int max_entries,
                                       double since,
                                       const char *json_str)
{
    flux_future_t *f;
    json_t *o = NULL;
    int saved_errno;

    if (!h || max_entries < 0 || since < 0. || !json_str
           || !(o = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-list.list", FLUX_NODEID_ANY, 0,
                             "{s:i s:f s:i s:i s:i s:o}",
                             "max_entries", max_entries,
                             "since", since,
                             "userid", FLUX_USERID_UNKNOWN,
                             "states", FLUX_JOB_STATE_INACTIVE,
                             "results", 0,
                             "attrs", o))) {
        saved_errno = errno;
        json_decref (o);
        errno = saved_errno;
        return NULL;
    }
    return f;
}

flux_future_t *flux_job_list_id (flux_t *h,
                                 flux_jobid_t id,
                                 const char *json_str)
{
    flux_future_t *f;
    json_t *o = NULL;
    int saved_errno;

    if (!h || (json_str
               && !(o = json_loads (json_str, 0, NULL)))) {
        errno = EINVAL;
        return NULL;
    }
    if (o)
        f = flux_rpc_pack (h, "job-list.list-id", FLUX_NODEID_ANY, 0,
                           "{s:I s:O}",
                           "id", id,
                           "attrs", o);
    else
        f = flux_rpc_pack (h, "job-list.list-id", FLUX_NODEID_ANY, 0,
                           "{s:I}",
                           "id", id);
    if (!f)
        goto error;

    json_decref (o);
    return f;

error:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
