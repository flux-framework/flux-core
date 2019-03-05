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
#include <unistd.h>
#include <sys/types.h>
#include <flux/core.h>

#include "job.h"

static int validate_lookup_flags (int flags)
{
    if (flags & ~FLUX_JOB_EVENTLOG_WATCH)
        return -1;
    return 0;
}

flux_future_t *flux_job_eventlog_lookup (flux_t *h, int flags, flux_jobid_t id)
{
    flux_future_t *f;
    const char *topic = "job-eventlog.lookup";
    int rpc_flags = FLUX_RPC_STREAMING;

    if (!h || validate_lookup_flags (flags) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, rpc_flags,
                             "{s:I s:i}",
                             "id", id,
                             "flags", flags)))
        return NULL;
    return f;
}

int flux_job_eventlog_lookup_get (flux_future_t *f, const char **event)
{
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", "event", &s) < 0)
        return -1;
    if (event)
        *event = s;
    return 0;
}

int flux_job_eventlog_lookup_cancel (flux_future_t *f)
{
    flux_future_t *f2;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              "job-eventlog.cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        return -1;
    flux_future_destroy (f2);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
