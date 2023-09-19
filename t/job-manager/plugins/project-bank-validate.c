/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* allow updates of attributes.system.{project,bank} for jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

static int project_bank_cb (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *arg)
{
    flux_job_state_t state;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "state", &state) < 0) {
        flux_jobtap_error (p, args, "plugin args unpack failed");
        return -1;
    }
    if (state == FLUX_JOB_STATE_RUN
        || state == FLUX_JOB_STATE_CLEANUP) {
        flux_jobtap_error (p,
                           args,
                           "update of project or bank for running job not supported");
        return -1;
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.update.attributes.system.project", project_bank_cb, NULL },
    { "job.update.attributes.system.bank", project_bank_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "project-bank-validate", tab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
