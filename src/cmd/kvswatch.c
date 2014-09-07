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

/* flux-kvswatch.c - flux kvswatch subcommand */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "kvs.h"

#define OPTIONS "hd"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"directory",  no_argument,        0, 'd'},
    { 0, 0, 0, 0 },
};

void watchdir (flux_t h, const char *key);
void watchval (flux_t h, const char *key);

void usage (void)
{
    fprintf (stderr, "Usage: flux-kvswatch [--dir] key\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *key;
    bool dopt;

    log_init ("flux-kvswatch");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'd': /* --directory */
                dopt = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    key = argv[optind];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (dopt)
        watchdir (h, key);
    else 
        watchval (h, key);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

void watchval (flux_t h, const char *key)
{
    int rc;
    json_object *val = NULL;

    rc = kvs_get (h, key, &val);
    while (rc == 0 || (rc < 0 && errno == ENOENT)) {
        if (rc < 0) {
            printf ("%s: %s\n", key, strerror (errno));
            if (val)
                json_object_put (val);
            val = NULL;
        } else
            printf ("%s=%s\n", key, json_object_to_json_string_ext (val,
                    JSON_C_TO_STRING_PLAIN));
        rc = kvs_watch_once (h, key, &val);
    }
    err_exit ("%s", key);
}

static void dump_kvs_dir (flux_t h, const char *path)
{
    kvsdir_t dir;
    kvsitr_t itr;
    const char *name, *js;
    char *key;

    if (kvs_get_dir (h, &dir, "%s", path) < 0) {
        printf ("%s: %s\n", path, strerror (errno));
        return;
    }

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            char *link;

            if (kvs_get_symlink (h, key, &link) < 0) {
                printf ("%s: %s\n", key, strerror (errno));
                continue;
            }
            printf ("%s -> %s\n", key, link);
            free (link);

        } else if (kvsdir_isdir (dir, name)) {
            dump_kvs_dir (h, key);

        } else {
            json_object *o;
            int len, max;

            if (kvs_get (h, key, &o) < 0) {
                printf ("%s: %s\n", key, strerror (errno));
                continue;
            }
            js = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
            len = strlen (js);
            max = 80 - strlen (key) - 4;
            if (len > max)
                printf ("%s = %.*s ...\n", key, max - 4, js);
            else
                printf ("%s = %s\n", key, js);
            json_object_put (o);
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}

void watchdir (flux_t h, const char *key)
{
    kvsdir_t dir = NULL;
    int rc;

    rc = kvs_get_dir (h, &dir, "%s", key);
    while (rc == 0 || (rc < 0 && errno == ENOENT)) {
        if (rc < 0) {
            printf ("%s: %s\n", key, strerror (errno));
            if (dir)
                kvsdir_destroy (dir);
            dir = NULL;
        } else {
            dump_kvs_dir (h, key);
            printf ("======================\n");
        }
        rc = kvs_watch_once_dir (h, &dir, "%s", key);
    }
    err_exit ("%s", key);

}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
