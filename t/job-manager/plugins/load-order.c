/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* load-order.c - test plugin use of flux_jobtap_set_load_sort_order(3)
 */

#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"

static const char *sort_mode = "none";
static flux_job_state_t prev_state = 0;
static flux_jobid_t prev_id = 0;

static int job_new (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *arg)
{
    flux_job_state_t state;
    flux_jobid_t id;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i}",
                                "id", &id,
                                "state", &state) < 0)
        return -1;

    if (streq (sort_mode, "state")) {
        if (state == prev_state) {
            if (id < prev_id)
                return flux_jobtap_error (p,
                                          args,
                                          "id (%ju) < previous id (%ju)",
                                          (uintmax_t) id,
                                          (uintmax_t) prev_id);
        }
        else if (state < prev_state)
            return flux_jobtap_error (p,
                                      args,
                                      "state (%s) < previous state (%s)",
                                      flux_job_statetostr (state, "S"),
                                      flux_job_statetostr (prev_state, "S"));
    }
    else if (streq (sort_mode, "-state")) {
        if (state == prev_state) {
            if (id < prev_id)
                return flux_jobtap_error (p,
                                          args,
                                          "id (%ju) < previous id (%ju)",
                                          (uintmax_t) id,
                                          (uintmax_t) prev_id);
        }
        else if (state > prev_state)
            return flux_jobtap_error (p,
                                      args,
                                      "state (%s) > previous state (%s)",
                                      flux_job_statetostr (state, "S"),
                                      flux_job_statetostr (prev_state, "S"));
    }
    else if (!streq (sort_mode, "none"))
        return flux_jobtap_error (p,
                                  args,
                                  "got invalid test mode=%s",
                                  sort_mode);
    prev_state = state;
    prev_id = id;
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.new", job_new, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    sort_mode = NULL;
    prev_state = 0;
    prev_id = 0;
    if (flux_plugin_register (p, "update-test", tab) < 0)
        return -1;
    (void) flux_plugin_conf_unpack (p, "{s?s}", "sort", &sort_mode);
    if (flux_jobtap_set_load_sort_order (p, sort_mode) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
