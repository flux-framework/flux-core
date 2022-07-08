/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* create-reject.c - reject a job from job.create callback
 */

#include <flux/jobtap.h>

static int create_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    return flux_jobtap_reject_job (p, args, "nope");
}


int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "job.create", create_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
