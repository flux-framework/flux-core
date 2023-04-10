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

int mod_main (flux_t *h, int argc, char **argv)
{
    if (argc == 1 && !strcmp (argv[0], "--init-failure")) {
        flux_log (h, LOG_INFO, "aborting during init per test request");
        errno = EIO;
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    return 0;
error:
    return -1;
}

MOD_NAME ("parent");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
