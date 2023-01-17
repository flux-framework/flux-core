/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* singlex.c - dso wrapper for 'single' builtin plugin */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

int upmi_single_init (flux_plugin_t *p);

int flux_plugin_init (flux_plugin_t *p)
{
    if (upmi_single_init (p) < 0)
        return -1;
    if (flux_plugin_set_name (p, "singlex") < 0) // override 'single'
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
