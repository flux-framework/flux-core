/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int
main (int argc, char *argv[])
{
    int len = 30;
    pid_t pid;

    if (argv[1] != NULL) {
        len = atoi (argv[1]);
        if (len <= 0) {
            fprintf (stderr, "sleep length must be > 0");
            exit (1);
        }
    }

    if ((pid = fork ()) == 0) {
        sleep (len);
        exit (0);
    }
    else {
        printf ("%d\n", getpid ());
        printf ("%d\n", pid);
        fflush (stdout);
        wait (NULL);
    }

    exit (0);
}
