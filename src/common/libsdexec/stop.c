/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* stop.c - reset/stop units
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "stop.h"

flux_future_t *sdexec_stop_unit (flux_t *h,
                                 uint32_t rank,
                                 const char *name,
                                 const char *mode)
{
    if (!h || !name || !mode) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "sdbus.call",
                          rank,
                          0,
                          "{s:s s:[ss]}",
                          "member", "StopUnit",
                          "params", name, mode);
}

flux_future_t *sdexec_reset_failed_unit (flux_t *h,
                                         uint32_t rank,
                                         const char *name)
{
    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "sdbus.call",
                          rank,
                          0,
                          "{s:s s:[s]}",
                          "member", "ResetFailedUnit",
                          "params", name);
}

flux_future_t *sdexec_kill_unit (flux_t *h,
                                 uint32_t rank,
                                 const char *name,
                                 const char *who,
                                 int signum)
{
    if (!h || !name || !who ) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "sdbus.call",
                          rank,
                          0,
                          "{s:s s:[ssi]}",
                          "member", "KillUnit",
                          "params", name, who, signum);

}

// vi:ts=4 sw=4 expandtab
