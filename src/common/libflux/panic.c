/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <flux/core.h>

#include "panic.h"

int flux_panic (flux_t *h, uint32_t nodeid, int flags, const char *reason)
{
    flux_future_t *f;

    if (!h || !reason || flags != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc_pack (h,
                             "cmb.panic",
                             nodeid,
                             FLUX_RPC_NORESPONSE,
                             "{s:s s:i}",
                             "reason",
                             reason,
                             "flags",
                             flags)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
