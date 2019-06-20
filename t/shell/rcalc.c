/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/shell/rcalc.h"

int main (int ac, char **av)
{
    int i, ntasks;
    rcalc_t *r;

    if (ac < 2) {
        fprintf (stderr, "Usage: %s NTASKS\n", av[0]);
        exit (1);
    }
    if (!(r = rcalc_createf (stdin)))  {
        fprintf (stderr, "Unable to create r: %s\n", strerror (errno));
        exit (1);
    }
    if ((ntasks = strtoul (av[1], NULL, 10)) <= 0 || ntasks > 1e20) {
        fprintf (stderr, "Invalid value for ntasks: %s\n", av[1]);
        exit (1);
    }
    printf ("Distributing %d tasks across %d nodes with %d cores\n",
            ntasks, rcalc_total_nodes (r), rcalc_total_cores (r));

    if (rcalc_distribute (r, ntasks) < 0) {
        fprintf (stderr, "rcalc_distribute: %s\n", strerror (errno));
        exit (1);
    }
    printf ("Used %d nodes\n", rcalc_total_nodes_used (r));

    for (i = 0; i < rcalc_total_nodes (r); i++) {
        struct rcalc_rankinfo ri;
        if (rcalc_get_nth (r, i, &ri) < 0) {
            fprintf (stderr, "rcalc_get_rankinfo (rank=%d): %s\n",
                     i, strerror (errno));
            exit (1);
        }
        printf ("%d: rank=%d ntasks=%d basis=%d cores=%s\n",
                ri.nodeid, ri.rank, ri.ntasks, ri.global_basis, ri.cores);
    }
    rcalc_destroy (r);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

