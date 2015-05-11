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
#include <getopt.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"


#define OPTIONS "hp:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"priority",   required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

int parse_logstr (char *s, int *lp, char **fp);

void usage (void)
{
    fprintf (stderr, "Usage: flux-logger [--priority facility.level] message ...\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *message = NULL;
    size_t len = 0;
    char *priority = "user.notice";
    int level;
    char *facility;

    log_init ("flux-logger");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --priority facility.level */
                priority = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();

    if (argz_create (argv + optind, &message, &len) < 0)
        oom ();
    argz_stringify (message, len, ' ');

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    if (parse_logstr (priority, &level, &facility) < 0)
        msg_exit ("bad priority argument");
    flux_log_set_facility (h, facility);
    if (flux_log (h, level, "%s", message) < 0)
        err_exit ("flux_log");

    flux_close (h);

    free (message);
    free (facility);
    log_fini ();
    return 0;
}

int parse_logstr (char *s, int *lp, char **fp)
{
    char *p, *fac = xstrdup (s);
    int lev = LOG_INFO;

    if ((p = strchr (fac, '.'))) {
        *p++ = '\0';
        lev = log_strtolevel (p);
        if (lev < 0)
            return -1;
    }
    *lp = lev;
    *fp = fac;
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
