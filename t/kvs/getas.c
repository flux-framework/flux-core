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

/* getas.c - get kvs key as type */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>
#include <inttypes.h>
#include <stdbool.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

void getas (flux_t *h, const char *key, const char *type);
void dirgetas (flux_t *h, const char *dir, const char *key, const char *type);

#define OPTIONS "t:d:"
static const struct option longopts[] = {
    {"type",           required_argument,    0, 't'},
    {"directory",      required_argument,    0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: getas [--type TYPE] [--directory DIR] key\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    const char *type = NULL;
    const char *directory = NULL;
    const char *key;
    flux_t *h;

    log_init ("getas");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 't': /* --type TYPE  */
                type = optarg;
                break;
            case 'd': /* --directory DIR */
                directory = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    key = argv[optind++];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (directory)
        dirgetas (h, directory, key, type);
    else
        getas (h, key, type);
    flux_close (h);
}

void dirgetas (flux_t *h, const char *dirkey, const char *key, const char *type)
{
    flux_future_t *f;
    char *fullkey;
    const flux_kvsdir_t *dir;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, dirkey))
            || flux_kvs_lookup_get_dir (f, &dir) < 0)
        log_err_exit ("flux_kvs_lookup %s", dirkey);

    fullkey = flux_kvsdir_key_at (dir, key);

    getas (h, fullkey, type);

    free (fullkey);
    flux_future_destroy (f);
}

void getas (flux_t *h, const char *key, const char *type)
{
    flux_future_t *f;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        log_err_exit ("flux_kvs_lookup");

    if (type == NULL) {
        const char *value;
        if (flux_kvs_lookup_get (f, &value) < 0)
            log_err_exit ("flux_kvs_lookup_get %s", key);
        printf ("%s\n", value);
    }
    else if (!strcmp (type, "int")) {
        int value;
        if (flux_kvs_lookup_get_unpack (f, "i", &value) < 0)
            log_err_exit ("flux_kvs_lookup_get_unpack(i) %s", key);
        printf ("%d\n", value);
    }
    else if (!strcmp (type, "int64")) {
        int64_t value;
        if (flux_kvs_lookup_get_unpack (f, "I", &value) < 0)
            log_err_exit ("flux_kvs_lookup_get_unpack(I) %s", key);
        printf ("%" PRIi64 "\n", value);
    }
    else if (!strcmp (type, "double")) {
        double value;
        if (flux_kvs_lookup_get_unpack (f, "F", &value) < 0)
            log_err_exit ("flux_kvs_lookup_get_unpack(F) %s", key);
        printf ("%f\n", value);
    }
    else if (!strcmp (type, "string")) {
        const char *value;
        if (flux_kvs_lookup_get_unpack (f, "s", &value) < 0)
            log_err_exit ("flux_kvs_lookup_get_unpack(s) %s", key);
        printf ("%s\n", value);
    }
    else {
        log_msg_exit ("unknown type (use int/int64/double/string)");
    }

    flux_future_destroy (f);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
