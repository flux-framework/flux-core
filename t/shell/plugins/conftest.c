/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "conftest"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <flux/core.h>
#include <flux/shell.h>

int flux_plugin_init (flux_plugin_t *p)
{
    char *key1, *key2, *key3;
    char *val1, *val2, *val3;
    if (flux_plugin_conf_unpack (p, "{s:[sss]}",
                                    "keys", &key1, &key2, &key3) < 0) {
        fprintf (stderr, "flux_plugin_conf_unpack\n");
        return -1;
    }
    fprintf (stderr, "conftest: keys = %s %s %s\n", key1, key2, key3);
    if (flux_plugin_conf_unpack (p, "{s:s s:s s:s}",
                                    key1, &val1,
                                    key2, &val2,
                                    key3, &val3) < 0) {
        fprintf (stderr, "flux_plugin_conf_unpack values\n");
        return -1;
    }
    fprintf (stderr, "conftest: %s=%s\n", key1, val1);
    fprintf (stderr, "conftest: %s=%s\n", key2, val2);
    fprintf (stderr, "conftest: %s=%s\n", key3, val3);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
