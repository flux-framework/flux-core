/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple tool that outputs args or stdin to stdout or stderr */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int
main (int argc, char *argv[])
{
    int out = 0;
    int err = 0;

    while (1) {
        int c = getopt (argc, argv, "OE");
        if (c < 0)
            break;

        switch (c) {
        case 'O':
            out++;
            break;
        case 'E':
            err++;
            break;
        }
    }

    if ((out + err) == 0) {
        fprintf (stderr, "must specify -O and/or -E\n");
        exit (1);
    }


    if (optind != argc) {
        while (optind < argc) {
            if (out) {
                fprintf (stdout, "%s\n", argv[optind]);
                fflush (stdout);
            }
            if (err) {
                fprintf (stderr, "%s\n", argv[optind]);
                fflush (stderr);
            }
            optind++;
        }
    }
    else {
        char buf[1024];
        memset (buf, '\0', 1024);
        while (read (STDIN_FILENO, buf, 1024) > 0) {
            if (out) {
                fprintf (stdout, "%s\n", buf);
                fflush (stdout);
            }
            if (err) {
                fprintf (stderr, "%s\n", buf);
                fflush (stderr);
            }
            memset (buf, '\0', 1024);
        }
    }

    exit (0);
}
