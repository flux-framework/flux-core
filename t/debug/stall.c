/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* stall.c - test program for debugger support: stalling until SIGCONT */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

void handle_sigcont (int sig)
{
    fprintf (stdout, "Caught SIGCONT\n");
    exit (0);
}

int main (int argc, char *argv[])
{
    FILE *fptr = NULL;
    int stall_sec = 0;

    if (argc != 3) {
        fprintf (stderr, "Usage: stall <filename> <stall_sec>\n");
        exit (1);
    }

    signal (SIGCONT, handle_sigcont);

    fprintf (stdout, "Signal handler for SIGCONT installed\n");

    if ( !(fptr = fopen (argv[1], "w"))) {
        fprintf (stderr, "Error: Can't write to %s\n", argv[1]);
        exit (1);
    }
    fclose (fptr);

    fprintf (stdout, "Sync file created: %s\n", argv[1]);

    if ((stall_sec = atoi (argv[2])) < 0) {
        fprintf (stderr, "Error: stall time (%d) must be > 0!\n", stall_sec);
        exit (1);
    }

    fprintf (stdout, "Will sleep for: %d\n", stall_sec);

    sleep (stall_sec);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
