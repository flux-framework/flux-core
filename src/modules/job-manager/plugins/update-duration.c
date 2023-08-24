/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* update-duration.c - allow updates of attributes.system.duration for jobs
 *
 * This plugin implements a 'job.update.attributes.system.duration'
 * callback to enable duration updates for pending jobs.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

/*
 *  Allow instance owner to update duration to any value, even if it
 *  exceeds a configured duration limit. By default, this is true, to
 *  disable this behavior, reload the `.update-duration` plugin with
 *  owner-allow-any=0.
 */
static int owner_allow_any = 1;

static int duration_update_cb (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *arg)
{
    struct flux_msg_cred cred;
    flux_job_state_t state;
    double duration;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:F s:i s:{s:i s:i}}",
                                "value", &duration,
                                "state", &state,
                                "cred",
                                  "userid", &cred.userid,
                                  "rolemask", &cred.rolemask) < 0) {
        flux_jobtap_error (p, args, "plugin args unpack failed");
        return -1;
    }
    if (duration < 0.) {
        flux_jobtap_error (p, args, "duration must not be negative");
        return -1;
    }
    if (state == FLUX_JOB_STATE_RUN
        || state == FLUX_JOB_STATE_CLEANUP) {
        flux_jobtap_error (p,
                           args,
                           "update of duration for running job not supported");
        return -1;
    }
    if ((cred.rolemask & FLUX_ROLE_OWNER) && owner_allow_any) {
        /* If owner is allowed to make any duration adjustment, then
         * report that value is validated via out arguments:
         */
        flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:i}",
                              "validated", 1);
    }
    return 0;
}

int update_duration_plugin_init (flux_plugin_t *p)
{
    flux_plugin_conf_unpack (p,
                             "{s:i}",
                             "owner-allow-any",
                             &owner_allow_any);
    return flux_plugin_add_handler (p,
                                    "job.update.attributes.system.duration",
                                    duration_update_cb,
                                    NULL);
}

// vi:ts=4 sw=4 expandtab
