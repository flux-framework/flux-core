/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* main.c - module main() for content module
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "cache.h"

int mod_main (flux_t *h, int argc, char **argv)
{
    struct content_cache *cache;
    int rc = -1;

    if (!(cache = content_cache_create (h, argc, argv))) {
        flux_log_error (h, "error initializing content cache");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "reactor exited abnormally");
        goto done;
    }
    rc = 0;
done:
    content_cache_destroy (cache);
    return rc;
}

// vi:ts=4 sw=4 expandtab
