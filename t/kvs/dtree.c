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


#define OPTIONS "p:w:h:D"
static const struct option longopts[] = {
    {"prefix",          required_argument,  0, 'p'},
    {"width",           required_argument,  0, 'w'},
    {"height",          required_argument,  0, 'h'},
    {"mkdir",           no_argument,        0, 'D'},
    { 0, 0, 0, 0 },
};

void dtree (flux_t *h, const char *prefix, int width, int height);
void dtree_mkdir (flux_t *h, kvsdir_t *dir, int width, int height);

void usage (void)
{
    fprintf (stderr,
"Usage: dtree [--mkdir] [--prefix NAME] [--width N] [--height N]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    int width = 1;
    int height = 1;
    char *prefix = "dtree";
    int Dopt = 0;
    flux_t *h;

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
            case 'D': /* --mkdir */
                Dopt++;
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
    if (Dopt) {
        kvsdir_t *dir;
        if (kvs_mkdir (h, prefix) < 0)
            log_err_exit ("kvs_mkdir %s", prefix);
        if (kvs_commit (h, 0) < 0)
            log_err_exit ("kvs_commit");
        if (kvs_get_dir (h, &dir, "%s", prefix) < 0)
            log_err_exit ("kvs_get_dir %s", prefix);
        dtree_mkdir (h, dir, width, height);
        kvsdir_destroy (dir);
    } else {
        dtree (h, prefix, width, height);
    }
    if (kvs_commit (h, 0) < 0)
       log_err_exit ("kvs_commit");
    flux_close (h);
}

/* This version simply puts keys and values, creating intermediate
 * directories as a side effect.
 */
void dtree (flux_t *h, const char *prefix, int width, int height)
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

/* This version creates intermediate directories and references them
 * using kvsdir_t objects.  This is a less efficient method but provides
 * alternate code coverage.
 */
void dtree_mkdir (flux_t *h, kvsdir_t *dir, int width, int height)
{
    int i;
    char key[16];
    kvsdir_t *ndir;

    for (i = 0; i < width; i++) {
        snprintf (key, sizeof (key), "%.4x", i);
        if (height == 1) {
            if (kvsdir_put_int (dir, key, 1) < 0)
                log_err_exit ("kvsdir_put_int %s", key);
        } else {
            if (kvsdir_mkdir (dir, key) < 0)
                log_err_exit ("kvsdir_mkdir %s", key);
            if (kvs_commit (h, 0) < 0)
                log_err_exit ("kvs_commit");
            if (kvsdir_get_dir (dir, &ndir, "%s", key) < 0)
                log_err_exit ("kvsdir_get_dir");
            dtree_mkdir (h, ndir, width, height - 1);
            kvsdir_destroy (ndir);
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
