/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* priority-invert.c - invert all priorities from what they currently
 * are / should be.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/jobtap.h>

static void trigger_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    flux_plugin_t *p = arg;

    if (flux_jobtap_reprioritize_all (p) < 0)
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
    flux_t *h = flux_jobtap_get_flux (p);
    int urgency;
    int64_t priority;

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:i s:I}",
                                "urgency", &urgency,
                                "priority", &priority) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "flux_plugin_arg_unpack: %s",
                  flux_plugin_arg_strerror (args));
        return -1;
    }

    /* if this is the first time we're initializing priority, let the
     * job-manager set the default */
    if (priority < 0)
        return 0;

    priority = FLUX_JOB_URGENCY_MAX - urgency;
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                              "{s:I}",
                              "priority", priority) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "flux_plugin_arg_pack: %s",
                  flux_plugin_arg_strerror (args));
        return -1;
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.priority", priority_cb, NULL },
    { "job.priority.get", priority_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "priority-invert", tab) < 0
        || flux_jobtap_service_register (p, "trigger", trigger_cb, p) < 0)
        return -1;

    return 0;
}

// vi:ts=4 sw=4 expandtab
