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
#include <string.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libmrpc/mrpc.h"


#define OPTIONS "hp:d:c:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"pad-bytes",  required_argument,  0, 'p'},
    {"delay-msec", required_argument,  0, 'd'},
    {"count",      required_argument,  0, 'c'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-mping [--count N] [--pad-bytes N] [--delay-msec N] nodelist\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int msec = 1000;
    int bytes = 0;
    char *pad = NULL;
    int seq;
    struct timespec t0;
    char *nodelist;
    json_object *inarg, *outarg;
    int id;
    flux_mrpc_t f;
    int count = INT_MAX;

    log_init ("flux-mping");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pad-bytes N */
                bytes = strtoul (optarg, NULL, 10);
                pad = xzmalloc (bytes + 1);
                memset (pad, 'p', bytes);
                break;
            case 'd': /* --delay-msec N */
                msec = strtoul (optarg, NULL, 10);
                break;
            case 'c': /* --count N */
                count = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    nodelist = argv[optind];

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    for (seq = 0; seq < count; seq++) {
        monotime (&t0);
        if (!(f = flux_mrpc_create (h, nodelist)))
            err_exit ("flux_mrpc_create");
        inarg = util_json_object_new_object ();
        util_json_object_add_int (inarg, "seq", seq);
        if (pad)
            util_json_object_add_string (inarg, "pad", pad);
        flux_mrpc_put_inarg (f, inarg);
        if (flux_mrpc (f, "mecho") < 0)
            err_exit ("flux_mrpc");
        while ((id = flux_mrpc_next_outarg (f)) != -1) {
            if (flux_mrpc_get_outarg (f, id, &outarg) < 0) {
                msg ("%d: no response", id);
                continue;
            }
            if (!util_json_match (inarg, outarg))
                msg ("%d: mangled response", id);
                json_object_put (outarg);
        }
        json_object_put (inarg);
        flux_mrpc_destroy (f);
        msg ("mecho: pad=%d seq=%d time=%0.3f ms",
             bytes, seq, monotime_since (t0));
        if (seq + 1 < count)
            usleep (msec * 1000);
    }

    flux_close (h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
