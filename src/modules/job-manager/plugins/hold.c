/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* builtins/hold.c - builtin plugin that holds all submitted jobs
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

/* Always set priority=0 so all jobs are submitted in held state
 */
static int hold_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *arg)
{
    flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "{s:I}", "priority", 0LL);
    return 0;
}


int hold_priority_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "job.state.priority", hold_cb, NULL);
}
