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

void dirgetas (flux_t *h, const char *dir, const char *key, const char *type)
{
    kvsdir_t *d;

    if (kvs_get_dir (h, &d, "%s", dir) < 0)
        log_err_exit ("kvs_get_dir %s", dir);

    if (type == NULL) {
        char *value;
        if (kvsdir_get (d, key, &value) < 0)
            log_err_exit ("kvsdir_get %s", key);
        printf ("%s\n", value);
        free (value);
    }
    else if (!strcmp (type, "int")) {
        int value;
        if (kvsdir_get_int (d, key, &value) < 0)
            log_err_exit ("kvsdir_get_int %s", key);
        printf ("%d\n", value);
    }
    else if (!strcmp (type, "int64")) {
        int64_t value;
        if (kvsdir_get_int64 (d, key, &value) < 0)
            log_err_exit ("kvsdir_get_int64 %s", key);
        printf ("%" PRIi64 "\n", value);
    }
    else if (!strcmp (type, "boolean")) {
        bool value;
        if (kvsdir_get_boolean (d, key, &value) < 0)
            log_err_exit ("kvsdir_get_int64 %s", key);
        printf ("%s\n", value ? "true" : "false");
    }
    else if (!strcmp (type, "double")) {
        double value;
        if (kvsdir_get_double (d, key, &value) < 0)
            log_err_exit ("kvsdir_get_int64 %s", key);
        printf ("%f\n", value);
    }
    else if (!strcmp (type, "string")) {
        char *s;
        if (kvsdir_get_string (d, key, &s) < 0)
            log_err_exit ("kvsdir_get_string %s", key);
        printf ("%s\n", s);
        free (s);
    }
    else {
        log_msg_exit ("unknown type (use int/int64/boolean/double/string)");
    }

    kvsdir_destroy (d);
}

void getas (flux_t *h, const char *key, const char *type)
{
    if (type == NULL) {
        char *value;
        if (kvs_get (h, key, &value) < 0)
            log_err_exit ("kvs_get %s", key);
        printf ("%s\n", value);
        free (value);
    }
    else if (!strcmp (type, "int")) {
        int value;
        if (kvs_get_int (h, key, &value) < 0)
            log_err_exit ("kvs_get_int %s", key);
        printf ("%d\n", value);
    }
    else if (!strcmp (type, "int64")) {
        int64_t value;
        if (kvs_get_int64 (h, key, &value) < 0)
            log_err_exit ("kvs_get_int64 %s", key);
        printf ("%" PRIi64 "\n", value);
    }
    else if (!strcmp (type, "boolean")) {
        bool value;
        if (kvs_get_boolean (h, key, &value) < 0)
            log_err_exit ("kvs_get_int64 %s", key);
        printf ("%s\n", value ? "true" : "false");
    }
    else if (!strcmp (type, "double")) {
        double value;
        if (kvs_get_double (h, key, &value) < 0)
            log_err_exit ("kvs_get_int64 %s", key);
        printf ("%f\n", value);
    }
    else if (!strcmp (type, "string")) {
        char *s;
        if (kvs_get_string (h, key, &s) < 0)
            log_err_exit ("kvs_get_string %s", key);
        printf ("%s\n", s);
        free (s);
    }
    else {
        log_msg_exit ("unknown type (use int/int64/boolean/double/string)");
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
