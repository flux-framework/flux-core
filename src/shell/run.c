/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "run.h"

int shell_run (flux_t *h, struct shell_info *info)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc;

    rc = flux_reactor_run (r, 0);

    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
