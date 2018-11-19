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
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zsecurity.h"


#define OPTIONS "hfpd:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"force",      no_argument,        0, 'f'},
    {"plain",      no_argument,        0, 'p'},
    {"secdir",     required_argument,  0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-keygen [--secdir DIR] [--force] [--plain]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    flux_sec_t *sec;
    int typemask = FLUX_SEC_TYPE_CURVE | FLUX_SEC_VERBOSE;
    const char *secdir = getenv ("FLUX_SEC_DIRECTORY");

    log_init ("flux-keygen");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'f': /* --force */
                typemask |= FLUX_SEC_KEYGEN_FORCE;
                break;
            case 'p': /* --plain */
                typemask |= FLUX_SEC_TYPE_PLAIN;
                typemask &= ~FLUX_SEC_TYPE_CURVE;
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

     if (!(sec = flux_sec_create (typemask, secdir)))
        log_err_exit ("flux_sec_create");
    if (flux_sec_keygen (sec) < 0)
        log_msg_exit ("%s", flux_sec_errstr (sec));
    flux_sec_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
