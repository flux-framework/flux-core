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
#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/read_all.h"


#define OPTIONS "hs:n:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"severity",   required_argument,  0, 's'},
    {"appname",    required_argument,  0, 'n'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, "Usage: flux-logger [--severity LEVEL] [--appname NAME] message ...\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    char *message = NULL;
    size_t len = 0;
    int severity = LOG_NOTICE;
    char *appname = "logger";
    int e;

    log_init ("flux-logger");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 's': /* --severity LEVEL */
                if ((severity = stdlog_string_to_severity (optarg)) < 0)
                    log_msg_exit ("invalid severity: Use emerg|alert|crit|err|warning|notice|info|debug");
                break;
            case 'n': /* --appname NAME */
                appname = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc) {
        len = read_all (STDIN_FILENO, (uint8_t **)&message);
    } else {
        if ((e = argz_create (argv + optind, &message, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (message, len, ' ');
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    flux_log_set_appname (h, appname);
    if (flux_log (h, severity | FLUX_LOG_CHECK, "%s", message) < 0)
        log_err_exit ("flux_log");

    flux_close (h);

    free (message);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
