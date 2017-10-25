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
void cmd_copy_tokvs (flux_t *h, int argc, char **argv);
void cmd_copy_fromkvs (flux_t *h, int argc, char **argv);


void usage (void)
{
    fprintf (stderr,
"Usage: basic type                key\n"
"       basic copy-tokvs          key file\n"
"       basic copy-fromkvs        key file\n"
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
    else if (!strcmp (cmd, "copy-tokvs"))
        cmd_copy_tokvs (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy-fromkvs"))
        cmd_copy_fromkvs (h, argc - optind, argv + optind);
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
