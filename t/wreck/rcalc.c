/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/modules/wreck/rcalc.h"

int main (int ac, char **av)
{
    int i, ntasks;
    rcalc_t *r;

    if (ac < 2) {
        fprintf (stderr, "Usage: %s NTASKS\n", av[0]);
        exit (1);
    }
    if (!(r = rcalc_createf (stdin)))  {
        fprintf (stderr, "Unable to create r");
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

    for (i = 0; i < rcalc_total_nodes (r); i++) {
        struct rcalc_rankinfo ri;
        if (rcalc_get_nth (r, i, &ri) < 0) {
            fprintf (stderr, "rcalc_get_rankinfo (rank=%d): %s\n",
                     i, strerror (errno));
            exit (1);
        }
        printf ("%d: rank=%d ntasks=%d basis=%d\n",
                ri.nodeid, ri.rank, ri.ntasks, ri.global_basis);
    }
    rcalc_destroy (r);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

