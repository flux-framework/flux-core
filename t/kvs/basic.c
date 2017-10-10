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
#include <getopt.h>
#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/readall.h"


#define OPTIONS "+h"
static const struct option longopts[] = {
    {"help",       no_argument,  0, 'h'},
    { 0, 0, 0, 0 },
};

void cmd_type (flux_t *h, int argc, char **argv);
void cmd_put_no_merge (flux_t *h, int argc, char **argv);
void cmd_copy_tokvs (flux_t *h, int argc, char **argv);
void cmd_copy_fromkvs (flux_t *h, int argc, char **argv);
void cmd_dirsize (flux_t *h, int argc, char **argv);
void cmd_get_treeobj (flux_t *h, int argc, char **argv);
void cmd_put_treeobj (flux_t *h, int argc, char **argv);
void cmd_getat (flux_t *h, int argc, char **argv);
void cmd_dirat (flux_t *h, int argc, char **argv);
void cmd_readlinkat (flux_t *h, int argc, char **argv);


void usage (void)
{
    fprintf (stderr,
"Usage: basic type                key\n"
"       basic put-no-merge        key=val\n"
"       basic copy-tokvs          key file\n"
"       basic copy-fromkvs        key file\n"
"       basic dirsize             key\n"
"       basic get-treeobj         key\n"
"       basic put-treeobj         key=treeobj\n"
"       basic getat               treeobj key\n"
"       basic dirat [-r]          treeobj [key]\n"
"       basic readlinkat          treeobj key\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    char *cmd;

    log_init ("basic");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!strcmp (cmd, "type"))
        cmd_type (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put-no-merge"))
        cmd_put_no_merge (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy-tokvs"))
        cmd_copy_tokvs (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy-fromkvs"))
        cmd_copy_fromkvs (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dirsize"))
        cmd_dirsize (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "get-treeobj"))
        cmd_get_treeobj (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put-treeobj"))
        cmd_put_treeobj (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "getat"))
        cmd_getat (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dirat"))
        cmd_dirat (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "readlinkat"))
        cmd_readlinkat (h, argc - optind, argv + optind);
    else
        usage ();

    flux_close (h);
    log_fini ();
    return 0;
}

void cmd_type (flux_t *h, int argc, char **argv)
{
    const char *json_str;
    json_t *o;
    json_error_t error;
    flux_future_t *f;

    if (argc != 1)
        log_msg_exit ("get-type: specify key");
    if (!(f = flux_kvs_lookup (h, 0, argv[0])))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        log_err_exit ("%s", argv[0]);
    if (!(o = json_loads (json_str, JSON_DECODE_ANY, &error))) {
        log_msg_exit ("%s: %s (line %d column %d)", argv[0],
                      error.text, error.line, error.column);
    }
    switch (json_typeof (o)) {
    case JSON_NULL:
        printf ("null\n");
        break;
    case JSON_TRUE:
    case JSON_FALSE:
        printf ("boolean\n");
        break;
    case JSON_REAL:
        printf ("double\n");
        break;
    case JSON_INTEGER:
        printf ("int\n");
        break;
    case JSON_OBJECT:
        printf ("object\n");
        break;
    case JSON_ARRAY:
        printf ("array\n");
        break;
    case JSON_STRING:
        printf ("string\n");
        break;
    }
    json_decref (o);
    flux_future_destroy (f);
}

static void output_key_json_object (const char *key, json_t *o)
{
    char *s;
    if (key)
        printf ("%s = ", key);

    switch (json_typeof (o)) {
    case JSON_NULL:
        printf ("nil\n");
        break;
    case JSON_TRUE:
        printf ("true\n");
        break;
    case JSON_FALSE:
        printf ("false\n");
        break;
    case JSON_REAL:
        printf ("%f\n", json_real_value (o));
        break;
    case JSON_INTEGER:
        printf ("%lld\n", (long long)json_integer_value (o));
        break;
    case JSON_STRING:
        printf ("%s\n", json_string_value (o));
        break;
    case JSON_ARRAY:
    case JSON_OBJECT:
    default:
        if (!(s = json_dumps (o, JSON_SORT_KEYS)))
            log_msg_exit ("json_dumps failed");
        printf ("%s\n", s);
        free (s);
        break;
    }
}

static void output_key_json_str (const char *key,
                                 const char *json_str,
                                 const char *arg)
{
    json_t *o;
    json_error_t error;

    if (!json_str)
        json_str = "null";
    if (!(o = json_loads (json_str, JSON_DECODE_ANY, &error)))
        log_msg_exit ("%s: %s (line %d column %d)",
                      arg, error.text, error.line, error.column);
    output_key_json_object (key, o);
    json_decref (o);
}

void cmd_put_no_merge (flux_t *h, int argc, char **argv)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (argc == 0)
        log_msg_exit ("put: specify one key=value pair");
    char *key = xstrdup (argv[0]);
    char *val = strchr (key, '=');
    if (!val)
        log_msg_exit ("put: you must specify a value as key=value");
    *val++ = '\0';

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit( "flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, 0, key, val) < 0) {
        if (errno != EINVAL)
            log_err_exit ("%s", key);
        if (flux_kvs_txn_pack (txn, 0, key, "s", val) < 0)
            log_err_exit ("%s", key);
    }
    free (key);
    if (!(f = flux_kvs_commit (h, FLUX_KVS_NO_MERGE, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
}

void cmd_copy_tokvs (flux_t *h, int argc, char **argv)
{
    char *file, *key;
    int fd, len;
    uint8_t *buf = NULL;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (argc != 2)
        log_msg_exit ("copy-tokvs: specify key and filename");
    key = argv[0];
    file = argv[1];
    if (!strcmp (file, "-")) {
        if ((len = read_all (STDIN_FILENO, &buf)) < 0)
            log_err_exit ("stdin");
    } else {
        if ((fd = open (file, O_RDONLY)) < 0)
            log_err_exit ("%s", file);
        if ((len = read_all (fd, &buf)) < 0)
            log_err_exit ("%s", file);
        (void)close (fd);
    }
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put_raw (txn, 0, key, buf, len) < 0)
        log_err_exit ("flux_kvs_txn_put_raw");
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_kvs_txn_destroy (txn);
    free (buf);
}

void cmd_copy_fromkvs (flux_t *h, int argc, char **argv)
{
    char *file, *key;
    int fd, len;
    const uint8_t *buf = NULL;
    flux_future_t *f;

    if (argc != 2)
        log_msg_exit ("copy-fromkvs: specify key and filename");
    key = argv[0];
    file = argv[1];
    if (!(f = flux_kvs_lookup (h, 0, key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get_raw (f, (const void **)&buf, &len) < 0)
        log_err_exit ("%s", key);
    if (!strcmp (file, "-")) {
        if (write_all (STDOUT_FILENO, buf, len) < 0)
            log_err_exit ("stdout");
    } else {
        if ((fd = creat (file, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
            log_err_exit ("%s", file);
        if (write_all (fd, buf, len) < 0)
            log_err_exit ("%s", file);
        if (close (fd) < 0)
            log_err_exit ("%s", file);
    }
    flux_future_destroy (f);
}

static void dump_kvs_val (const char *key, const char *json_str)
{
    json_t *o;
    json_error_t error;

    if (!json_str)
        json_str = "null";
    if (!(o = json_loads (json_str, JSON_DECODE_ANY, &error))) {
        printf ("%s: %s (line %d column %d)\n",
                key, error.text, error.line, error.column);
        return;
    }
    output_key_json_object (key, o);
    json_decref (o);
}

static void dump_kvs_dir (const flux_kvsdir_t *dir, bool ropt)
{
    flux_future_t *f;
    flux_kvsitr_t *itr;
    const char *name;
    flux_t *h = flux_kvsdir_handle (dir);
    const char *rootref = flux_kvsdir_rootref (dir);
    char *key;

    itr = flux_kvsitr_create (dir);
    while ((name = flux_kvsitr_next (itr))) {
        key = flux_kvsdir_key_at (dir, name);
        if (flux_kvsdir_issymlink (dir, name)) {
            const char *link;
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key, rootref))
                    || flux_kvs_lookup_get (f, &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            flux_future_destroy (f);

        } else if (flux_kvsdir_isdir (dir, name)) {
            if (ropt) {
                const flux_kvsdir_t *ndir;
                if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, rootref))
                        || flux_kvs_lookup_get_dir (f, &ndir) < 0)
                    log_err_exit ("%s", key);
                dump_kvs_dir (ndir, ropt);
                flux_future_destroy (f);
            } else
                printf ("%s.\n", key);
        } else {
            const char *json_str;
            if (!(f = flux_kvs_lookupat (h, 0, key, rootref))
                    || flux_kvs_lookup_get (f, &json_str) < 0)
                log_err_exit ("%s", key);
            dump_kvs_val (key, json_str);
            flux_future_destroy (f);
        }
        free (key);
    }
    flux_kvsitr_destroy (itr);
}

void cmd_dirat (flux_t *h, int argc, char **argv)
{
    bool ropt = false;
    const flux_kvsdir_t *dir;
    flux_future_t *f;

    if (argc > 0) {
        while (argc) {
            if (!strcmp (argv[0], "-r")) {
                ropt = true;
                argc--;
                argv++;
            }
            else
                break;
        }
    }
    if (argc != 2)
        log_msg_exit ("dir: specify treeobj and directory");
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, argv[1], argv[0]))
            || flux_kvs_lookup_get_dir (f, &dir) < 0)
        log_err_exit ("%s", argv[1]);
    dump_kvs_dir (dir, ropt);
    flux_future_destroy (f);
}

void cmd_dirsize (flux_t *h, int argc, char **argv)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir = NULL;

    if (argc != 1)
        log_msg_exit ("dirsize: specify one directory");
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, argv[0])))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        log_err_exit ("%s", argv[0]);
    printf ("%d\n", flux_kvsdir_get_size (dir));
    flux_future_destroy (f);
}

void cmd_get_treeobj (flux_t *h, int argc, char **argv)
{
    const char *treeobj;
    flux_future_t *f;

    if (argc != 1)
        log_msg_exit ("get-treeobj: specify key");
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, argv[0]))
            || flux_kvs_lookup_get (f, &treeobj) < 0)
        log_err_exit ("kvs_get_treeobj %s", argv[0]);
    printf ("%s\n", treeobj);
    flux_future_destroy (f);
}

void cmd_getat (flux_t *h, int argc, char **argv)
{
    const char *json_str;
    flux_future_t *f;

    if (argc != 2)
        log_msg_exit ("getat: specify treeobj and key");
    if (!(f = flux_kvs_lookupat (h, 0, argv[1], argv[0]))
        || flux_kvs_lookup_get (f, &json_str) < 0)
        log_err_exit ("flux_kvs_lookupat %s %s", argv[0], argv[1]);
    output_key_json_str (NULL, json_str, argv[1]);
    flux_future_destroy (f);
}

void cmd_put_treeobj (flux_t *h, int argc, char **argv)
{
    flux_future_t *f;
    flux_kvs_txn_t *txn;

    if (argc != 1)
        log_msg_exit ("put-treeobj: specify key=val");
    char *key = xstrdup (argv[0]);
    char *val = strchr (key, '=');
    if (!val)
        log_msg_exit ("put-treeobj: you must specify a value as key=val");
    *val++ = '\0';

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, FLUX_KVS_TREEOBJ, key, val) < 0)
        log_err_exit ("flux_kvs_txn_put %s=%s", key, val);
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (key);
}

void cmd_readlinkat (flux_t *h, int argc, char **argv)
{
    const char *target;
    flux_future_t *f;

    if (argc != 2)
        log_msg_exit ("readlink: specify treeobj and key");
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, argv[1], argv[0]))
            || flux_kvs_lookup_get (f, &target) < 0)
        log_err_exit ("%s", argv[1]);
    else
        printf ("%s\n", target);
    flux_future_destroy (f);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
