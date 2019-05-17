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
#    include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zsecurity.h"

#define OPTIONS "hfpd:"
static const struct option longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"force", no_argument, 0, 'f'},
    {"plain", no_argument, 0, 'p'},
    {"secdir", required_argument, 0, 'd'},
    {0, 0, 0, 0},
};

void usage (void)
{
    fprintf (stderr, "Usage: flux-keygen [--secdir DIR] [--force] [--plain]\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    zsecurity_t *sec;
    int typemask = ZSECURITY_TYPE_CURVE | ZSECURITY_VERBOSE;
    const char *secdir = getenv ("FLUX_SEC_DIRECTORY");

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'h': /* --help */
            usage ();
            break;
        case 'f': /* --force */
            typemask |= ZSECURITY_KEYGEN_FORCE;
            break;
        case 'p': /* --plain */
            typemask |= ZSECURITY_TYPE_PLAIN;
            typemask &= ~ZSECURITY_TYPE_CURVE;
            break;
        case 'd': /* --secdir */
            secdir = optarg;
            break;
        default:
            usage ();
            break;
        }
    }
    if (optind < argc)
        usage ();

    if (!(sec = zsecurity_create (typemask, secdir)))
        log_err_exit ("zsecurity_create");
    if (zsecurity_keygen (sec) < 0)
        log_msg_exit ("%s", zsecurity_errstr (sec));
    zsecurity_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
