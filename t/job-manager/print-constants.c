/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <flux/core.h>
#include "ccan/str/str.h"

int main (int argc, char *argv[])
{
    if (argc != 2) {
        fprintf (stderr, "Usage: print-constants NAME\n");
        return 1;
    }
    if (streq (argv[1], "FLUX_JOBID_ANY")) {
        printf ("%llx\n", (long long unsigned)FLUX_JOBID_ANY);
    }
    else {
        fprintf (stderr, "unknown name\n");
        return 1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
