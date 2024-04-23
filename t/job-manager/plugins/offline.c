/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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

/* Disconnect rank 3 by default */
static int rank = 3;

static int run_cb (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *arg)
{
    /*  Immediately on state RUN, disconnect the configured rank
     */
    flux_t *h = flux_jobtap_get_flux (p);
    flux_future_t *f;

    /*  Assumes parent of rank is rank 0 */
    if (!(f = flux_rpc_pack (h,
                             "overlay.disconnect-subtree",
                             0,
                             0,
                             "{s:i}",
                             "rank", rank))
        || flux_rpc_get (f, NULL) < 0) {
        flux_log_error (h, "failed to disconnect rank %d", rank);
    }
    flux_future_destroy (f);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "job.state.run",
                                     run_cb,
                                     NULL);
}

// vi:ts=4 sw=4 expandtab
