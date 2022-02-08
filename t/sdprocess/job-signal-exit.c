/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Simple "job" for testing.  exit success when get SIGINT, exit
 * failure when get SIGTERM, or sleep until exit.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

typedef void (*sighandler_t)(int);

sighandler_t signal(int signum, sighandler_t handler);

static void sig_cb (int signum)
{
    if (signum == SIGINT) {
        printf ("got SIGINT, exit success");
        exit (0);
    }

    if (signum == SIGTERM) {
        printf ("got SIGTERM, exit failure");
        exit (1);
    }
}

int main (int argc, char *argv[])
{
    int secs;

    if (argc != 2) {
        fprintf (stderr, "usage: job-sleep-exit <seconds>\n");
        exit (1);
    }

    if (signal (SIGINT, sig_cb) == SIG_ERR)
        perror ("signal");
    if (signal (SIGTERM, sig_cb) == SIG_ERR)
        perror ("signal");

    secs = atoi (argv[1]);
    sleep (secs);
    exit (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
