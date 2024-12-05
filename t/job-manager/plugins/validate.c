/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Test plugin which limits job to 4 per user */

#include <stdio.h>

#include <flux/core.h>
#include <flux/jobtap.h>

static int reject_id = 4;

static int validate (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    int i;
    flux_jobid_t jobid;
    /* Failure to unpack not an error, just let jobs without
     *  validate-test-id through
     */
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:{s:{s:{s:i}}}}}",
                                "id", &jobid,
                                "jobspec",
                                 "attributes", "system", "jobtap",
                                  "validate-test-id", &i) < 0)
        return 0;
    if (i == reject_id)
        return flux_jobtap_reject_job (p,
			               args,
			               "Job had reject_id == %d jobid=%ju",
			               i,
				       (uintmax_t)jobid);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_plugin_set_name (p, "test-validate");
    return flux_plugin_add_handler (p, "job.validate", validate, NULL);
}

// vi:ts=4 sw=4 expandtab
