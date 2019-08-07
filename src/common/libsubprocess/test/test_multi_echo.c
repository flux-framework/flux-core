/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple tool that outputs args to stdout/stderr multiple times */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <poll.h>

int out = 0;
int err = 0;
int count = 4;

int
main (int argc, char *argv[])
{
    int maxcount;

    while (1) {
        int c = getopt (argc, argv, "OEc:");
        if (c < 0)
            break;

        switch (c) {
        case 'O':
            out++;
            break;
        case 'E':
            err++;
            break;
        case 'c':
            count = atoi (optarg);
            if (count <= 0) {
                fprintf (stderr, "count invalid\n");
                exit (1);
            }
            break;
        }
    }

    if ((out + err) == 0) {
        fprintf (stderr, "must specify -O and/or -E for output\n");
        exit (1);
    }

    /* +1 for newline */
    maxcount = count + 1;
    while (optind < argc) {
        int ret, outcount = 0, errcount = 0;

        /* some tests can flood / hang fprintf calls, so check
         * that we can output */
        while ((out && outcount < maxcount)
               || (err && errcount < maxcount)) {
            struct pollfd pfds[2];

            pfds[0].fd = STDOUT_FILENO;
            pfds[0].events = (out && outcount < maxcount) ? POLLOUT: 0;
            pfds[0].revents = 0;

            pfds[1].fd = STDERR_FILENO;
            pfds[1].events = (err && errcount < maxcount) ? POLLOUT: 0;
            pfds[1].revents = 0;

            if ((ret = poll (pfds, 2, -1)) < 0) {
                perror ("poll");
                exit (1);
            }
            if (out && pfds[0].revents & POLLOUT) {
                if (outcount == count)
                    fprintf (stdout, "\n");
                else
                    fprintf (stdout, "%s", argv[optind]);
                fflush (stdout);
                outcount++;
            }
            if (err && pfds[1].revents & POLLOUT) {
                if (errcount == count)
                    fprintf (stderr, "\n");
                else
                    fprintf (stderr, "%s", argv[optind]);
                fflush (stderr);
                errcount++;
            }
        }
        optind++;
    }

    exit (0);
}
