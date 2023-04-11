/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Try to watch a non-existent key from a module.
 * Sharness code will verify that watch count goes up, and
 * then when module unloads, watch count will go down
 * because broker generated disconnect message.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

int mod_main (flux_t *h, int argc, char *argv[])
{
    flux_future_t *f;
    int rc;

    if (!(f = flux_kvs_lookup (h,
                               NULL,
                               FLUX_KVS_WATCH | FLUX_KVS_WAITCREATE,
                               "noexist"))) {
        flux_log_error (h, "flux_kvs_lookup");
        return -1;
    }
    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");

    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
