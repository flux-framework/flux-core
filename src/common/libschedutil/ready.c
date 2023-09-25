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

#include "ccan/str/str.h"

#include "schedutil_private.h"
#include "init.h"
#include "ready.h"

int schedutil_ready (schedutil_t *util, const char *mode, int *queue_depth)
{
    flux_future_t *f;
    int limit = 0;
    int count;

    if (!util || !mode) {
        errno = EINVAL;
        return -1;
    }
    if (strstarts (mode, "limited=")) {
        char *endptr;
        int n = strtol (mode+8, &endptr, 0);
        if (*endptr != '\0' || n <= 0) {
            errno = EINVAL;
            return -1;
        }
        mode = "limited";
        limit = n;
    }
    else if (!streq (mode, "unlimited")) {
        errno = EINVAL;
        return -1;
    }
    if (limit) {
        if (!(f = flux_rpc_pack (util->h,
                                 "job-manager.sched-ready",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:s s:i}",
                                 "mode", mode,
                                 "limit", limit)))
            return -1;
    }
    else {
        if (!(f = flux_rpc_pack (util->h,
                                 "job-manager.sched-ready",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:s}",
                                 "mode", mode)))
            return -1;
    }
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
