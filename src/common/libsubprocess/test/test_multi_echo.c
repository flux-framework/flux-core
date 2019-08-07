/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple tool that outputs args to stdout multiple times */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int count = 4;

int
main (int argc, char *argv[])
{
    while (1) {
        int c = getopt (argc, argv, "nc:");
        if (c < 0)
            break;

        switch (c) {
        case 'c':
            count = atoi (optarg);
            if (count <= 0) {
                fprintf (stderr, "count invalid\n");
                exit (1);
            }
            break;
        }
    }

    if (optind != argc) {
        while (optind < argc) {
            int i;
            for (i = 0; i < count; i++) {
                printf ("%s", argv[optind]);
                fflush (stdout);
            }
            optind++;
        }
        printf ("\n");
        fflush (stdout);
    }

    exit (0);
}
