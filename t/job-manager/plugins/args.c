/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* args.c - test job-manager jobtap plugin callback for expected args
 */

#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>
#include "ccan/str/str.h"

static int cb (flux_plugin_t *p,
               const char *topic,
               flux_plugin_arg_t *args,
               void *arg)
{
    json_t *resources = NULL;
    json_t *entry = NULL;
    flux_jobid_t id = FLUX_JOBID_ANY;
    uint32_t userid = (uint32_t) -1;
    int urgency = -1;
    unsigned int priority = 1234;
    flux_job_state_t state = 4096;
    flux_job_state_t prev_state = 4096;
    double t_submit = 0.0;
    flux_t *h = flux_jobtap_get_flux (p);

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:{s:o} s?o s:I s:i s:i s:i s:i s?i s:f}",
                                "jobspec", "resources", &resources,
                                 "entry", &entry,
                                "id", &id,
                                "userid", &userid,
                                "urgency", &urgency,
                                "priority", &priority,
                                "state", &state,
                                "prev_state", &prev_state,
                                "t_submit", &t_submit) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "flux_plugin_arg_unpack: %s",
                  flux_plugin_arg_strerror (args));
        return -1;
    }

    if (streq (topic, "job.new")) {
        /*  Subscribe to events so we get all job.event.* callbacks */
        if (flux_jobtap_job_subscribe (p, FLUX_JOBTAP_CURRENT_JOB) < 0) {
            flux_log (h,
                      LOG_ERR,
                      "%s: jobtap_job_subscribe: %s",
                       topic,
                       strerror (errno));
        }
    }
    if (strstarts (topic, "job.state.")) {
        if (entry == NULL
            || state == 4096
            || prev_state == 4096) {
            flux_log (h,
                      LOG_ERR,
                      "%s: entry=%p state=%d prev_state=%d",
                       topic, entry, state, prev_state);
            return -1;
        }
    }
    if (resources == NULL
        || id == FLUX_JOBID_ANY
        || userid == (uint32_t) -1
        || urgency == -1
        || priority == 1234
        || t_submit == 0.0) {
        flux_log (h,
                  LOG_ERR,
                  "%s: res=%p id=%ju uid=%d  urg=%d, pri=%d, t_submit=%f",
                  topic,
                  resources,
                  (uintmax_t)id,
                  userid,
                  urgency,
                  priority,
                  t_submit);
        return -1;
    }
    flux_log (h, LOG_INFO, "args-check: %s: OK", topic);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_plugin_set_name (p, "args");
    return flux_plugin_add_handler (p, "job.*", cb, NULL);
}

// vi:ts=4 sw=4 expandtab
