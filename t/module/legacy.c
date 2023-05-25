/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Do not include project config.h, we are testing public module
 * interface */
#include <flux/core.h>

int mod_main (flux_t *h, int argc, char **argv)
{
    return flux_reactor_run (flux_get_reactor (h), 0);
}

MOD_NAME("legacy");

// vi:ts=4 sw=4 expandtab
