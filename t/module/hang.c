/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <flux/core.h>

int mod_main (flux_t *h, int argc, char *argv[])
{
    if (flux_module_set_running (h) < 0)
        return -1;
    pause (); // pthreads(7) cancellation point
    return 0;
}

// vi:ts=4 sw=4 expandtab
