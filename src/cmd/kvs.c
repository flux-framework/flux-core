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

/* flux-kvs.c - flux kvs subcommand */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "xzmalloc.h"
#include "log.h"

#include "flux.h"
#include "kvs.h"

#define OPTIONS "hdCDNqm:s:r:vV:"
static const struct option longopts[] = {
    {"help",       no_argument,  0, 'h'},
    {"no-commit",  no_argument,  0, 'C'},
    {"dropcache",  no_argument,  0, 'd'},
    {"null-noerror", no_argument,  0, 'N'},
    {"dropcache-all",  no_argument,  0, 'D'},
    {"quiet",      no_argument,  0, 'q'},
    {"mkdir",      required_argument,  0, 'm'},
    {"symlink",    required_argument,  0, 's'},
    {"readlink",   required_argument,  0, 'r'},
    {"wait-version", required_argument,  0, 'V'},
    {"get-version", no_argument,  0, 'v'},
    { 0, 0, 0, 0 },
};

void get (flux_t h, const char *key, bool null_noerror, bool quiet);
void del (flux_t h, const char *key, bool quiet);
void put (flux_t h, const char *key, const char *val, bool quiet);
void commit (flux_t h);

void usage (void)
{
    fprintf (stderr, "Usage: flux-kvs key[=val] [key[=val]] [^] ...\n"
"where the arguments are one or more of:\n"
"    key         displays value of key\n"
"    key=        unlinks key\n"
"    key=val     sets value of key (with commit unless --no-commit)\n"
"    ^           commit\n"
"and 'val' has the form:\n"
"    4           json int\n"
"    4.2         json double\n"
"    true|false  json boolean\n"
"    [1,2,3]     json array (of int, but may be any type)\n"
"    \"string\"    json string\n"
"    {...}       json object\n"
"remember to escape any characters that are interpted by your shell\n"
"Use --dropcache to drop the local slave cache\n."
"Use --dropcache-all to drop slave caches across the session\n."
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    bool need_commit = false;
    int i, ch;
    bool dopt = false;
    bool Dopt = false;
    bool Copt = false;
    bool Nopt = false;
    bool qopt = false;
    char *mkdir_name = NULL;
    char *symlink_name = NULL;
    char *readlink_name = NULL;
    bool vopt = false;
    bool Vopt = false;
    int version;

    log_init ("flux-kvs");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'C': /* --no-commit */
                Copt = true;
                break;
            case 'd': /* --dropcache */
                dopt = true;
                break;
            case 'D': /* --dropcache-all */
                Dopt = true;
                break;
            case 'N': /* --null-noerror */
                Nopt = true;
                break;
            case 'q': /* --quiet */
                qopt = true;
                break;
            case 'm': /* --mkdir DIR */
                mkdir_name = optarg;
                break;
            case 's': /* --symlink name=target */
                symlink_name = optarg;
                break;
            case 'r': /* --readlink name */
                readlink_name = optarg;
                break;
            case 'v': /* --get-version */
                vopt = true;
                break;
            case 'V': /* --get-version */
                Vopt = true;
                version = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc && !(dopt || Dopt || mkdir_name || symlink_name || readlink_name || vopt || Vopt))
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (dopt) {
        if (kvs_dropcache (h) < 0)
            err_exit ("kvs_dropcache");
    }
    if (Dopt) {
        if (flux_event_send (h, NULL, "event.kvs.dropcache") < 0)
            err_exit ("flux_event_send");
    }
    if (mkdir_name) {
        if (kvs_mkdir (h, mkdir_name) < 0)
            err_exit ("flux_mkdir %s", mkdir_name);
        if (!Copt)
            need_commit = true;
    }
    if (symlink_name) {
        char *cpy = xstrdup (symlink_name);
        char *val = strchr (cpy, '=');
        if (!val)
            msg_exit ("--symlink requires a name=target argument");
        *val++ = '\0';
        if (kvs_symlink (h, cpy, val) < 0)
            err_exit ("flux_symlink %s %s", cpy, val);
        free (cpy);
        if (!Copt)
            need_commit = true;
    }
    if (readlink_name) {
        char *val;
        if (kvs_get_symlink (h, readlink_name, &val) < 0)
            err_exit ("kvs_get_symlink %s", readlink_name);
        if (qopt)
            printf ("%s\n", val);
        else
            printf ("%s=%s\n", readlink_name, val);
        free (val);
    }
    if (vopt) {
        if (kvs_get_version (h, &version) < 0)
            err_exit ("kvs_get_version");
        printf ("%d\n", version);
    }
    if (Vopt) {
        if (kvs_wait_version (h, version) < 0)
            err_exit ("kvs_wait_version");
    }

    for (i = optind; i < argc; i++) {
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');

        if (val) {
            *val++ = '\0';
            if (*val)
                put (h, key, val, qopt);
            else
                del (h, key, qopt);
            if (!Copt)
                need_commit = true;
        } else {
            if (!strcmp (key, "^"))
                commit (h);
            else
                get (h, key, Nopt, qopt);
        }
        free (key);
    }
    if (need_commit)
        commit (h);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

void get (flux_t h, const char *key, bool null_noerror, bool quiet)
{
    json_object *val;

    if (kvs_get (h, key, &val) < 0) {
        if (errno == ENOENT && null_noerror) {
            if (quiet)
                printf ("null\n");
            else
                printf ("%s=null\n", key);
        } else
            err_exit ("%s", key);
    } else {
        if (quiet)
            printf ("%s\n", json_object_to_json_string (val));
        else
            printf ("%s=%s\n", key, json_object_to_json_string (val));
        json_object_put (val);
    }
}

void put (flux_t h, const char *key, const char *valstr, bool quiet)
{
    json_object *val = NULL;

    assert (valstr != NULL);
    val = json_tokener_parse (valstr);
    if (kvs_put (h, key, val) < 0)
        err_exit ("%s", key);
    else if (!quiet)
        printf ("%s=%s\n", key, json_object_to_json_string (val));
    if (val)
        json_object_put (val);
}

void del (flux_t h, const char *key, bool quiet)
{
    if (kvs_unlink (h, key) < 0)
        err_exit ("%s", key);
    else if (!quiet)
        printf ("%s=\n", key);
}

void commit (flux_t h)
{
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
