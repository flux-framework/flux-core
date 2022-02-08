/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple tool that outputs environment variables that match input */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main (int argc, char *argv[])
{
    int i;

    if (argc < 2) {
        fprintf (stderr, "must specify environment variables to check for\n");
        exit (1);
    }

    for (i = 1; i < argc; i++) {
        char *val = getenv (argv[i]);
        if (val)
            printf ("%s=%s\n", argv[i], val);
    }

    exit (0);
}
