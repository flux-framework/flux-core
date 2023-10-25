/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobspec-update-job-list.c - test jobspec-update event in job-list
 * module
 */

#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    double expiration;
    flux_jobid_t id;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s:F}}}",
                                "id", &id,
                                "R",
                                 "execution",
                                  "expiration", &expiration) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "resource-update", 0,
                                     "unpack failure");
        return -1;
    }

    if (flux_jobtap_event_post_pack (p,
                                     id,
                                     "resource-update",
                                     "{s:f}",
                                     "expiration", expiration + 3600.) < 0) {
        flux_jobtap_raise_exception (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "resource-update", 0,
                                     "update failure");
        return -1;
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.run", run_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "resource-update-expiration", tab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
