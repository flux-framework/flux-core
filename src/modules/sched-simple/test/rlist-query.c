/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <flux/core.h>
#include "src/modules/sched-simple/rlist.h"

int main (int ac, char *av[])
{
    const char *s = NULL;
    char *result = NULL;
    struct rlist *rl = NULL;
    flux_future_t *f = NULL;
    flux_t *h = flux_open (NULL, 0);

    if (h == NULL) {
        fprintf (stderr, "flux_open: %s\n", strerror (errno));
        exit (1);
    }
    if (!(f = flux_rpc (h, "sched-simple.status", "{}", 0, 0))) {
        fprintf (stderr, "flux_rpc: %s\n", strerror (errno));
        exit (1);
    }
    if (flux_rpc_get (f, &s) < 0) {
        fprintf (stderr, "sched-simple.status: %s", strerror (errno));
        exit (1);
    }
    if (!(rl = rlist_from_R (s))) {
        fprintf (stderr, "unable to read R: %s", strerror (errno));
        exit (1);
    }
    flux_future_destroy (f);
    result = rlist_dumps (rl);
    printf ("%s\n", result);
    free (result);
    flux_close (h);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
