/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <syslog.h>
#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    int begin, end;

    begin = fdcount ();

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_builtin_get ("connector_path",
                                         FLUX_CONF_INTREE), 0);

    if (!(h = flux_open ("loop://", 0))) {
        perror ("flux_open");
        exit (1);
    }

    flux_close (h);

    end = fdcount ();

    if (begin != end) {
        fprintf (stderr, "begin and end fd count don't match: %d != %d\n",
                 begin, end);
        exit (1);
    }

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

