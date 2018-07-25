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
    {"nprocs",     required_argument,  0, 'n'},
    {"test-iterations", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};


void usage (void)
{
    fprintf (stderr,
"Usage: tbarrier [--quiet] [--nprocs N] [--test-iterations N] [name]\n"
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
                log_msg_exit ("%s", "provide barrier name if not running as LWJ");
            else
                log_err_exit ("flux_barrier");
        }
        if (flux_rpc_get (f, NULL) < 0)
            log_err_exit ("barrier completion failed");
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
