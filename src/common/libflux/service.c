/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

flux_future_t *flux_service_register (flux_t *h, const char *name)
{
    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "service.add",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:s}",
                          "service",
                          name);
}

flux_future_t *flux_service_unregister (flux_t *h, const char *name)
{
    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "service.remove",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:s}",
                          "service",
                          name);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
