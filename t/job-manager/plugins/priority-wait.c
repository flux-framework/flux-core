/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* priority-wait.c - keep jobs in priority state and wait for
 *  an RPC to assign priority.
 */

#include <flux/core.h>
#include <flux/jobtap.h>


static void release_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    flux_jobid_t id;
    int64_t priority = -1;
    flux_plugin_t *p = arg;

    if (flux_request_unpack (msg, NULL,
                             "{s:I s:I}",
                             "id", &id,
                             "priority", &priority) < 0) {
        flux_log_error (h, "failed to unpack priority-wait.release msg");
        goto error;
    }
    if (priority < FLUX_JOB_PRIORITY_MIN
        || priority > FLUX_JOB_PRIORITY_MAX) {
        errno = EINVAL;
        goto error;
    }
    if (flux_jobtap_reprioritize_job (p, id, (unsigned int) priority) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "flux_respond");
    return;
error:
    flux_respond_error (h, msg, errno, flux_msg_last_error (msg));
}

static int priority_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    return flux_jobtap_priority_unavail (p, args);
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.priority", priority_cb, NULL },
    { "job.priority.get", priority_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "priority-wait", tab) < 0
        || flux_jobtap_service_register (p, "release", release_cb, p) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
