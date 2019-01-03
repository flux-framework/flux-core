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
#include "config.h"
#endif
#include <flux/core.h>
/*
 *  Dummy sched module, do nothing but answer pings
 */
int mod_main (flux_t *h, int argc, char *argv[])
{
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        return (-1);
    return (0);
}
MOD_NAME ("sched");
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
