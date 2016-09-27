/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

/* dtree.c - create HxW KVS directory tree */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"


#define OPTIONS "p:w:h:"
static const struct option longopts[] = {
    {"prefix",          required_argument,  0, 'p'},
    {"width",           required_argument,  0, 'w'},
    {"height",          required_argument,  0, 'h'},
    { 0, 0, 0, 0 },
};

void dtree (flux_t h, const char *prefix, int width, int height);

void usage (void)
{
    fprintf (stderr,
"Usage: dtree [--prefix NAME] [--width N] [--height N]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    int width = 1;
    int height = 1;
    char *prefix = "dtree";
    flux_t h;

    log_init ("dtree");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'w': /* --width N */
                width = strtoul (optarg, NULL, 10);
                break;
            case 'h': /* --height N */
                height = strtoul (optarg, NULL, 10);
                break;
            case 'p': /* --prefix NAME */
                prefix = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();
    if (width < 1 || height < 1)
        usage ();
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    dtree (h, prefix, width, height);
    if (kvs_commit (h) < 0)
       log_err_exit ("kvs_commit");
    flux_close (h);
}

void dtree (flux_t h, const char *prefix, int width, int height)
{
    int i;
    char *key;

    for (i = 0; i < width; i++) {
        key = xasprintf ("%s.%.4x", prefix, i);
        if (height == 1) {
            if (kvs_put_int (h, key, 1) < 0)
                log_err_exit ("kvs_put %s", key);
        } else
            dtree (h, key, width, height - 1);
        free (key);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
