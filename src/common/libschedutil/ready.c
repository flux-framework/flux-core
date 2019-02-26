/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "ready.h"

int schedutil_ready (flux_t *h, const char *mode, int *queue_depth)
{
    flux_future_t *f;
    int count;

    if (!h || !mode) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc_pack (h, "job-manager.sched-ready",
                             FLUX_NODEID_ANY, 0,
                             "{s:s}", "mode", mode)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:i}", "count", &count) < 0)
        goto error;
    if (queue_depth)
        *queue_depth = count;
    flux_future_destroy (f);
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
