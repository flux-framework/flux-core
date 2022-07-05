/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* cleanup-event.c - emit a test event in CLEANUP state
 */

#include <flux/jobtap.h>

static int cleanup_cb (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *arg)
{
    return flux_jobtap_event_post_pack (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                       "test-event",
                                       NULL);
}


int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "job.state.cleanup",
                                     cleanup_cb,
                                     NULL);
}

// vi:ts=4 sw=4 expandtab
