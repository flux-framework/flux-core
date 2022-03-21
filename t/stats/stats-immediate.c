/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include <flux/jobtap.h>
#include <time.h>

#include "src/common/libutil/monotime.h"

struct cb_data {
    ssize_t inactive;
    struct timespec ts;
};

static int state_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    struct cb_data *data = arg;
    flux_job_state_t state;
    flux_job_state_t prev_state = 4096;
    flux_t *h = flux_jobtap_get_flux (p);


    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                               "{s:i s?i}",
                               "state", &state,
                               "prev_state", &prev_state) < 0) {
        flux_log (h,
                 LOG_ERR,
                 "flux_plugin_arg_unpack: %s",
                 flux_plugin_arg_strerror(args));
        return -1;
    }

    flux_stats_gauge_inc (h, flux_job_statetostr (state, false), 1);
    flux_stats_gauge_inc (h, flux_job_statetostr (prev_state, false), -1);

    switch (state) {
        case FLUX_JOB_STATE_CLEANUP:
            monotime (&data->ts);
            break;
        case FLUX_JOB_STATE_INACTIVE:
            flux_stats_timing (h, "cleanup.timing", monotime_since (data->ts));
            flux_stats_count (h, "inactive.count", ++data->inactive);
            break;
        default:
            break;
    }

    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    struct cb_data *data;
    flux_t *h;
    if (!(h = flux_jobtap_get_flux (p)))
        return -1;
    if (!(data = calloc (1, sizeof (*data))))
        return -1;
    if (flux_plugin_aux_set (p, "data", data, free) < 0)
        return -1;

    flux_stats_set_prefix (h, "flux.job.state.immediate");
    flux_stats_set_period (h, 0.0);

    // try setting a prefix with a length longer than the limit (127)
    // and expect it to remain as "flux.job.stats.immediate"
    flux_stats_set_prefix (h, "aQmi173rvgumStDdMZdwtJtpLLVJOUXol2aDndev/XsH/gM\
wlPz/vMZhajJWGctwJZa1uFoAoINjwITPvezGoQDb/9DD3vkPcknN+f/u3vSc0tg/+3aFTONhUIomK\
B4qiSKSotbtZl3Ewe2Oh+wI+nuG3/ebqIXSoEXjIFOB7vvGA==");

    return flux_plugin_add_handler (p, "job.state.*", state_cb, data);
}
