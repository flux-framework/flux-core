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

flux_future_t *flux_job_event_watch (flux_t *h, flux_jobid_t id,
                                     const char *path, int flags)
{
    flux_future_t *f;
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    int valid_flags = FLUX_JOB_EVENT_WATCH_WAITCREATE;

    if (!h || !path || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h,
                             topic,
                             FLUX_NODEID_ANY,
                             rpc_flags,
                             "{s:I s:s s:i}",
                             "id", id,
                             "path", path,
                             "flags", flags)))
        return NULL;
    return f;
}

int flux_job_event_watch_get (flux_future_t *f, const char **event)
{
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", "event", &s) < 0)
        return -1;
    if (event)
        *event = s;
    return 0;
}

int flux_job_event_watch_cancel (flux_future_t *f)
{
    flux_future_t *f2;
    const char *topic = "job-info.eventlog-watch-cancel";

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              topic,
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
