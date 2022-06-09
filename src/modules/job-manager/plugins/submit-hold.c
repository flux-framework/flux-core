/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* submit-hold.c - jobtap plugin that holds all submitted jobs
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/types.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

/* Set all jobs urgency to FLUX_JOB_URGENCY_HOLD.
 */
static int depend_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    int urgency;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "urgency", &urgency) < 0)
        return -1;

    if (urgency != FLUX_JOB_URGENCY_HOLD) {
        return flux_jobtap_event_post_pack (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "urgency",
                                            "{s:i s:i}",
                                            "userid", getuid (),
                                            "urgency", FLUX_JOB_URGENCY_HOLD);
    }
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "job.state.depend", depend_cb, NULL);
}
