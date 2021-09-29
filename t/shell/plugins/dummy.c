/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "dummy"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <flux/core.h>
#include <flux/shell.h>

int flux_plugin_init (flux_plugin_t *p)
{
    int rc;
    if (flux_plugin_conf_unpack (p, "{s:i}", "result", &rc) < 0) {
        fprintf (stderr, "flux_plugin_conf_unpack\n");
        return -1;
    }
    fprintf (stderr, "dummy: OK result=%d\n", rc);
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
