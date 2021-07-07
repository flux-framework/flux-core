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
#include <jansson.h>

#include "job.h"

flux_future_t *flux_job_raise (flux_t *h, flux_jobid_t id,
                               const char *type, int severity, const char *note)
{
    flux_future_t *f;
    json_t *o;
    int saved_errno;

    if (!h || !type) {
        errno = EINVAL;
        return NULL;
    }
    if (!(o = json_pack ("{s:I s:s s:i}",
                         "id", id,
                         "type", type,
                         "severity", severity)))
        goto nomem;
    if (note) {
        json_t *o_note = json_string (note);
        if (!o_note || json_object_set_new (o, "note", o_note) < 0) {
            json_decref (o_note);
            goto nomem;
        }
    }
    if (!(f = flux_rpc_pack (h, "job-manager.raise", FLUX_NODEID_ANY, 0,
                                                                    "o", o)))
        goto error;
    return f;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return NULL;
}

flux_future_t *flux_job_cancel (flux_t *h, flux_jobid_t id, const char *reason)
{
    return flux_job_raise (h, id, "cancel", 0, reason);
}

flux_future_t *flux_job_kill (flux_t *h, flux_jobid_t id, int signum)
{
    if (!h || signum <= 0) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h, "job-manager.kill", FLUX_NODEID_ANY, 0,
                             "{s:I s:i}",
                             "id", id,
                             "signum", signum);
}

flux_future_t *flux_job_set_urgency (flux_t *h, flux_jobid_t id, int urgency)
{
    flux_future_t *f;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-manager.urgency", FLUX_NODEID_ANY, 0,
                             "{s:I s:i}",
                             "id", id,
                             "urgency", urgency)))
        return NULL;
    return f;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
