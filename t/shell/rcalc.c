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

#include <flux/optparse.h>

#include "src/shell/rcalc.h"

const char usage[] = "[OPTIONS] NTASKS";

static struct optparse_option opts[] = {
    { .name = "per-resource", .key = 'R',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Assign tasks per-resource instead of distributing. "
               "NAME is name of resource (node or core)",
    },
    { .name = "cores-per-slot", .key = 'c',
      .has_arg = 1, .arginfo = "N",
      .usage = "Explicitly set the number of cores per task",
    },
    OPTPARSE_TABLE_END
};

int main (int ac, char **av)
{
    int i, ntasks, optindex;
    const char *rname = NULL;
    rcalc_t *r = NULL;

    optparse_t *p = optparse_create ("rcalc");
    if (p == NULL
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS
        || optparse_set (p, OPTPARSE_USAGE, usage) != OPTPARSE_SUCCESS
        || optparse_parse_args (p, ac, av) < 0)
        exit (1);
    optindex = optparse_option_index (p);

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    optparse_getopt (p, "per-resource", &rname);

    if (!(r = rcalc_createf (stdin)))  {
        fprintf (stderr, "Unable to create r: %s\n", strerror (errno));
        exit (1);
    }
    if ((ntasks = strtoul (av[optindex], NULL, 10)) <= 0 || ntasks > 1e20) {
        fprintf (stderr, "Invalid value for ntasks: %s\n", av[1]);
        exit (1);
    }
    printf ("Distributing %d tasks%s%s across %d nodes with %d cores",
            ntasks,
            rname ? " per-" : "",
            rname ? rname : "",
            rcalc_total_nodes (r),
            rcalc_total_cores (r));
    if (rcalc_total_gpus (r))
        printf (" %d gpus", rcalc_total_gpus (r));
    printf ("\n");

    if (rname) {
        if (rcalc_distribute_per_resource (r, rname, ntasks) < 0) {
            fprintf (stderr,
                     "rcalc_distribute_per_resource: %s",
                     strerror (errno));
            exit (1);
        }
    }
    else if (rcalc_distribute (r,
                               ntasks,
                               optparse_get_int (p, "cores-per-slot", 0)) < 0) {
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
        printf ("%d: rank=%d ntasks=%d cores=%s",
                ri.nodeid, ri.rank, ri.ntasks, ri.cores);
        if (strlen(ri.gpus))
            printf (" gpus=%s", ri.gpus);
        printf ("\n");
    }
    rcalc_destroy (r);
    optparse_destroy (p);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

