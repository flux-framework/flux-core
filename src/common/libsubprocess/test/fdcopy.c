/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* fdcopy.c - copy one file descriptor to another */

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int getenv_int (const char *name, int *valp)
{
    const char *s;
    int val = -1;

    if (!(s = getenv (name)))
        return -1;
    errno = 0;
    val = strtoul (s, NULL, 10);
    if (errno != 0)
        return -1;
    *valp = val;
    return 0;
}

void fdcopy (int fd_in, int fd_out)
{
    int rlen;
    int wlen;
    char buf[1024];

    for (;;) {
        rlen = read (fd_in, buf, sizeof (buf));
        if (rlen < 0) {
            perror ("read");
            exit (1);
        }
        if (rlen == 0)
            break;
        wlen = 0;
        while (wlen < rlen) {
            int n = write (fd_out, buf + wlen, rlen - wlen);
            if (n < 0) {
                perror ("write");
                exit (1);
            }
            wlen += n;
        }
    }
}

int main (int argc, char **argv)
{
    int fd_in;
    int fd_out;

    if (argc != 3
        || getenv_int (argv[1], &fd_in) < 0
        || getenv_int (argv[2], &fd_out) < 0) {
        fprintf (stderr,
                 "Usage: fdcopy INCHAN OUTCHAN\n"
                 "  where *CHAN are env vars set to file descriptor numbers\n");
        exit (1);
    }

    fdcopy (fd_in, fd_out);

    exit (0);
}


// vi: ts=4 sw=4 expandtab

