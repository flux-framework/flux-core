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
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"

#define OPTIONS "hqn:t:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"quiet",      no_argument,        0, 'q'},
    {"early-exit", no_argument,        0, 'E'},
    {"nprocs",     required_argument,  0, 'n'},
    {"test-iterations", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};


void usage (void)
{
    fprintf (stderr,
"Usage: tbarrier [-q] [-n NPROCS] [-t ITER] [-E] [name]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;
    int ch;
    struct timespec t0;
    char *name = NULL;
    int quiet = 0;
    int nprocs = 1;
    int iter = 1;
    int i;
    bool Eopt = false;

    log_init ("tbarrier");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'q': /* --quiet */
                quiet = 1;
                break;
            case 'n': /* --nprocs N */
                nprocs = strtoul (optarg, NULL, 10);
                break;
            case 't': /* --test-iterations N */
                iter = strtoul (optarg, NULL, 10);
                break;
            case 'E': /* --early-exit */
                Eopt = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc - 1)
        usage ();
    if (optind < argc)
        name = argv[optind++];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    for (i = 0; i < iter; i++) {
        char *tname = NULL;
        monotime (&t0);
        if (name)
            tname = xasprintf ("%s.%d", name, i);
        if (!(f = flux_barrier (h, tname, nprocs))) {
            if (errno == EINVAL && tname == NULL)
                log_msg_exit ("%s", "provide barrier name if not running in job");
            else
                log_err_exit ("flux_barrier");
        }
        if (!Eopt) {
            if (flux_future_get (f, NULL) < 0)
                log_err_exit ("barrier completion failed");
        }
        if (!quiet)
            printf ("barrier name=%s nprocs=%d time=%0.3f ms\n",
                    tname ? tname : "NULL", nprocs, monotime_since (t0));
        free (tname);
        flux_future_destroy (f);
    }

    flux_close (h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
