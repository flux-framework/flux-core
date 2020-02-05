/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

struct bad_aux {
    flux_t *h;
} bad_aux;

void bad_aux_destroy (void *arg)
{
    struct bad_aux *ba = arg;
    flux_future_t *f = flux_rpc (ba->h, "foo", NULL, FLUX_NODEID_ANY, 0);
    if (f != NULL) {
        fprintf (stderr, "flux_send during flux_close not blocked!\n");
        exit (1);
    }
    if (errno != ENOSYS) {
        fprintf (stderr, "unexpected error from flux_rpc: %s\n",
                         strerror (errno));
        exit (1);
    }
    fprintf (stderr, "flux_rpc: got expected error: %s\n", strerror (errno));
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;

    /*  Test that flux_send disabled during flux_close() */
    if (!(h = flux_open ("loop://", 0))) {
        perror ("flux_open");
        exit (1);
    }
    bad_aux.h = h;
    if (flux_aux_set (h, "bad_aux", &bad_aux, bad_aux_destroy) < 0) {
        perror ("flux_aux_set");
        exit (1);
    }

    flux_close (h);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

