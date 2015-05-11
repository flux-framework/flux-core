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

/* flux-kvstorture.c - flux kvstorture subcommand */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"


#define OPTIONS "hc:s:p:qv"
static const struct option longopts[] = {
    {"help",            no_argument,        0, 'h'},
    {"quiet",           no_argument,        0, 'q'},
    {"verbose",         no_argument,        0, 'v'},
    {"count",           required_argument,  0, 'c'},
    {"size",            required_argument,  0, 's'},
    {"prefix",          required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

static void fill (char *s, int i, int len);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-kvstorture [--quiet|--verbose] [--prefix NAME] [--size BYTES] [--count N]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int i, count = 20;
    int size = 20;
    char *key, *val;
    bool quiet = false;
    struct timespec t0;
    json_object *vo = NULL;
    char *prefix = NULL;
    bool verbose = false;
    const char *s;

    log_init ("flux-kvstorture");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 's': /* --size BYTES */
                size = strtoul (optarg, NULL, 10);
                break;
            case 'c': /* --count */
                count = strtoul (optarg, NULL, 10);
                break;
            case 'p': /* --prefix NAME */
                prefix = xstrdup (optarg);
                break;
            case 'v': /* --verbose */
                verbose = true;
                break;
            case 'q': /* --quiet */
                quiet = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc)
        usage ();
    if (size < 1 || count < 1)
        usage ();

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (!prefix) {
        uint32_t rank = flux_rank (h);
        prefix = xasprintf ("kvstorture-%u", rank);
    }

    if (kvs_unlink (h, prefix) < 0)
        err_exit ("kvs_unlink %s", prefix);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");

    val = xzmalloc (size);

    monotime (&t0);
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        vo = json_object_new_string (val);
        if (kvs_put (h, key, vo) < 0)
            err_exit ("kvs_put %s", key);
        if (verbose)
            msg ("%s = %s", key, val);
        if (vo)
            json_object_put (vo);
        free (key);
    }
    if (!quiet)
        msg ("kvs_put:    time=%0.3f s (%d keys of size %d)",
             monotime_since (t0)/1000, count, size);

    monotime (&t0);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (!quiet)
        msg ("kvs_commit: time=%0.3f s", monotime_since (t0)/1000);

    monotime (&t0);
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        if (kvs_get (h, key, &vo) < 0)
            err_exit ("kvs_get '%s'", key);
        s = json_object_get_string (vo);
        if (verbose)
            msg ("%s = %s", key, s);
        if (strcmp (s, val) != 0)
            msg_exit ("kvs_get: key '%s' wrong value '%s'",
                      key, json_object_get_string (vo));
        if (vo)
            json_object_put (vo);
        free (key);
    }
    if (!quiet)
        msg ("kvs_get:    time=%0.3f s (%d keys of size %d)",
             monotime_since (t0)/1000, count, size);

    if (prefix)
        free (prefix);
    flux_close (h);
    log_fini ();
    return 0;
}

static void fill (char *s, int i, int len)
{
    snprintf (s, len, "%d", i);
    memset (s + strlen (s), 'x', len - strlen (s) - 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
