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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/base64.h"
#include "src/common/libutil/readall.h"


#define OPTIONS "+h"
static const struct option longopts[] = {
    {"help",       no_argument,  0, 'h'},
    { 0, 0, 0, 0 },
};

void cmd_get (flux_t *h, int argc, char **argv);
void cmd_type (flux_t *h, int argc, char **argv);
void cmd_put (flux_t *h, int argc, char **argv);
void cmd_put_no_merge (flux_t *h, int argc, char **argv);
void cmd_unlink (flux_t *h, int argc, char **argv);
void cmd_link (flux_t *h, int argc, char **argv);
void cmd_readlink (flux_t *h, int argc, char **argv);
void cmd_mkdir (flux_t *h, int argc, char **argv);
void cmd_exists (flux_t *h, int argc, char **argv);
void cmd_version (flux_t *h, int argc, char **argv);
void cmd_wait (flux_t *h, int argc, char **argv);
void cmd_watch (flux_t *h, int argc, char **argv);
void cmd_watch_dir (flux_t *h, int argc, char **argv);
void cmd_dropcache (flux_t *h, int argc, char **argv);
void cmd_dropcache_all (flux_t *h, int argc, char **argv);
void cmd_copy_tokvs (flux_t *h, int argc, char **argv);
void cmd_copy_fromkvs (flux_t *h, int argc, char **argv);
void cmd_copy (flux_t *h, int argc, char **argv);
void cmd_move (flux_t *h, int argc, char **argv);
void cmd_dir (flux_t *h, int argc, char **argv);
void cmd_dirsize (flux_t *h, int argc, char **argv);
void cmd_get_treeobj (flux_t *h, int argc, char **argv);
void cmd_put_treeobj (flux_t *h, int argc, char **argv);
void cmd_getat (flux_t *h, int argc, char **argv);
void cmd_dirat (flux_t *h, int argc, char **argv);
void cmd_readlinkat (flux_t *h, int argc, char **argv);


void usage (void)
{
    fprintf (stderr,
"Usage: basic get                 key\n"
"       basic type                key\n"
"       basic put                 key=val\n"
"       basic put-no-merge        key=val\n"
"       basic unlink              key\n"
"       basic link                target link_name\n"
"       basic readlink            key\n"
"       basic mkdir               key\n"
"       basic exists              key\n"
"       basic watch               [count] key\n"
"       basic watch-dir [-r]      [count] key\n"
"       basic copy-tokvs          key file\n"
"       basic copy-fromkvs        key file\n"
"       basic copy                srckey dstkey\n"
"       basic move                srckey dstkey\n"
"       basic dir [-r]            [key]\n"
"       basic dirsize             key\n"
"       basic version\n"
"       basic wait                version\n"
"       basic dropcache\n"
"       basic dropcache-all\n"
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

    if (!strcmp (cmd, "get"))
        cmd_get (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "type"))
        cmd_type (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put"))
        cmd_put (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put-no-merge"))
        cmd_put_no_merge (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "unlink"))
        cmd_unlink (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "link"))
        cmd_link (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "readlink"))
        cmd_readlink (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "mkdir"))
        cmd_mkdir (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "exists"))
        cmd_exists (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "version"))
        cmd_version (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "wait"))
        cmd_wait (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "watch"))
        cmd_watch (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "watch-dir"))
        cmd_watch_dir (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dropcache"))
        cmd_dropcache (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dropcache-all"))
        cmd_dropcache_all (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy-tokvs"))
        cmd_copy_tokvs (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy-fromkvs"))
        cmd_copy_fromkvs (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "copy"))
        cmd_copy (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "move"))
        cmd_move (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dir"))
        cmd_dir (h, argc - optind, argv + optind);
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
    char *json_str;
    json_object *o;

    if (argc != 1)
        log_msg_exit ("get-type: specify key");
    if (kvs_get (h, argv[0], &json_str) < 0)
        log_err_exit ("%s", argv[0]);
    if (!(o = Jfromstr (json_str)))
        log_msg_exit ("%s: malformed JSON", argv[0]);
    const char *type = "unknown";
    switch (json_object_get_type (o)) {
    case json_type_null:
        type = "null";
        break;
    case json_type_boolean:
        type = "boolean";
        break;
    case json_type_double:
        type = "double";
                break;
    case json_type_int:
        type = "int";
        break;
    case json_type_object:
        type = "object";
        break;
    case json_type_array:
        type = "array";
        break;
    case json_type_string:
        type = "string";
        break;
    }
    printf ("%s\n", type);
    Jput (o);
    free (json_str);
}

static void output_key_json_object (const char *key, json_object *o)
{
    if (key)
        printf ("%s = ", key);

    switch (json_object_get_type (o)) {
    case json_type_null:
        printf ("nil\n");
        break;
    case json_type_boolean:
        printf ("%s\n", json_object_get_boolean (o) ? "true" : "false");
        break;
    case json_type_double:
        printf ("%f\n", json_object_get_double (o));
        break;
    case json_type_int:
        printf ("%d\n", json_object_get_int (o));
        break;
    case json_type_string:
        printf ("%s\n", json_object_get_string (o));
        break;
    case json_type_array:
    case json_type_object:
    default:
        printf ("%s\n", Jtostr (o));
        break;
    }
}

static void output_key_json_str (const char *key,
                                 const char *json_str,
                                 const char *arg)
{
    json_object *o;

    if (!json_str) {
        output_key_json_object (key, NULL); 
        return;
    }

    if (!(o = Jfromstr (json_str)))
        log_msg_exit ("%s: malformed JSON", arg);
    output_key_json_object (key, o);
    Jput (o);
}

void cmd_get (flux_t *h, int argc, char **argv)
{
    char *json_str;

    if (argc == 0)
        log_msg_exit ("get: specify one or more keys");
    if (kvs_get (h, argv[0], &json_str) < 0)
        log_err_exit ("%s", argv[0]);
    output_key_json_str (NULL, json_str, argv[0]);
    free (json_str);
}

void cmd_put_common (flux_t *h, int argc, char **argv, bool mergeable)
{
    if (argc == 0)
        log_msg_exit ("put: specify one key=value pair");
    char *key = xstrdup (argv[0]);
    char *val = strchr (key, '=');
    if (!val)
        log_msg_exit ("put: you must specify a value as key=value");
    *val++ = '\0';
    if (kvs_put (h, key, val) < 0) {
        if (errno != EINVAL || kvs_put_string (h, key, val) < 0)
            log_err_exit ("%s", key);
    }
    free (key);
    if (kvs_commit (h, mergeable ? 0 : KVS_NO_MERGE) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_put (flux_t *h, int argc, char **argv)
{
    cmd_put_common (h, argc, argv, true);
}

void cmd_put_no_merge (flux_t *h, int argc, char **argv)
{
    cmd_put_common (h, argc, argv, false);
}

void cmd_unlink (flux_t *h, int argc, char **argv)
{
    if (argc != 1)
        log_msg_exit ("unlink: specify key");
    if (kvs_unlink (h, argv[0]) < 0)
        log_err_exit ("%s", argv[0]);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_link (flux_t *h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("link: specify target and link_name");
    if (kvs_symlink (h, argv[1], argv[0]) < 0)
        log_err_exit ("%s", argv[1]);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_readlink (flux_t *h, int argc, char **argv)
{
    const char *target;
    flux_future_t *f;

    if (argc != 1)
        log_msg_exit ("readlink: specify key"); 
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READLINK, argv[0]))
            || flux_kvs_lookup_getf (f, "s", &target) < 0)
        log_err_exit ("%s", argv[0]);
    else
        printf ("%s\n", target);
    flux_future_destroy (f);
}

void cmd_mkdir (flux_t *h, int argc, char **argv)
{
    if (argc != 1)
        log_msg_exit ("mkdir: specify directory");
    if (kvs_mkdir (h, argv[0]) < 0)
        log_err_exit ("%s", argv[0]);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
}

bool key_exists (flux_t *h, const char *key)
{
    char *json_str = NULL;
    kvsdir_t *dir = NULL;

    if (kvs_get (h, key, &json_str) == 0) {
        free (json_str);
        return true;
    }
    if (errno == EISDIR && kvs_get_dir (h, &dir, "%s", key) == 0) {
        kvsdir_destroy (dir);
        return true;
    }
    return false;
}

void cmd_exists (flux_t *h, int argc, char **argv)
{
    if (argc != 1)
        log_msg_exit ("exist: specify key");
    if (!key_exists (h, argv[0]))
        exit (1);
}

void cmd_version (flux_t *h, int argc, char **argv)
{
    int vers;
    if (argc != 0)
        log_msg_exit ("version: takes no arguments");
    if (kvs_get_version (h, &vers) < 0)
        log_err_exit ("kvs_get_version");
    printf ("%d\n", vers);
}

void cmd_wait (flux_t *h, int argc, char **argv)
{
    int vers;
    if (argc != 1)
        log_msg_exit ("wait: specify a version");
    vers = strtoul (argv[0], NULL, 10);
    if (kvs_wait_version (h, vers) < 0)
        log_err_exit ("kvs_get_version");
    //printf ("%d\n", vers);
}

void cmd_watch (flux_t *h, int argc, char **argv)
{
    char *json_str = NULL;
    char *key;
    int count = -1;

    if (argc == 2) {
        count = strtoul (argv[0], NULL, 10);
        argc--;
        argv++;
    }
    if (argc != 1)
        log_msg_exit ("watch: specify one key");
    key = argv[0];
    if (kvs_get (h, key, &json_str) < 0 && errno != ENOENT) 
        log_err_exit ("%s", key);
    do {
        output_key_json_str (NULL, json_str, key);
        if (--count == 0)
            break;
        if (kvs_watch_once (h, argv[0], &json_str) < 0 && errno != ENOENT)
            log_err_exit ("%s", argv[0]);
    } while (true);
    free (json_str);
}

void cmd_dropcache (flux_t *h, int argc, char **argv)
{
    if (argc != 0)
        log_msg_exit ("dropcache: takes no arguments");
    if (kvs_dropcache (h) < 0)
        log_err_exit ("kvs_dropcache");
}

void cmd_dropcache_all (flux_t *h, int argc, char **argv)
{
    if (argc != 0)
        log_msg_exit ("dropcache-all: takes no arguments");
    flux_msg_t *msg = flux_event_encode ("kvs.dropcache", NULL);
    if (!msg || flux_send (h, msg, 0) < 0)
        log_err_exit ("flux_send");
    flux_msg_destroy (msg);
}

void cmd_copy_tokvs (flux_t *h, int argc, char **argv)
{
    char *file, *key;
    int fd, len;
    uint8_t *buf;
    json_object *o;

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
    int s_len = base64_encode_length (len);
    char *s_buf = xzmalloc (s_len);
    if (base64_encode_block (s_buf, &s_len, buf, len) < 0)
        log_err_exit ("base64_encode_block error");
    o = Jnew ();
    Jadd_str (o, "data", s_buf);
    if (kvs_put (h, key, Jtostr (o)) < 0)
        log_err_exit ("%s", key);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
    Jput (o);
    free (buf);
    free (s_buf);
}

void cmd_copy_fromkvs (flux_t *h, int argc, char **argv)
{
    char *file, *key;
    int fd, len, s_len;
    uint8_t *buf;
    const char *s_buf;
    json_object *o;
    char *json_str;

    if (argc != 2)
        log_msg_exit ("copy-fromkvs: specify key and filename");
    key = argv[0];
    file = argv[1];
    if (kvs_get (h, key, &json_str) < 0)
        log_err_exit ("%s", key);
    if (!(o = Jfromstr (json_str)))
        log_msg_exit ("%s: invalid JSON", key);
    if (!Jget_str (o, "data", &s_buf))
        log_msg_exit ("%s: JSON decode error", key);
    s_len = strlen (s_buf);
    len = base64_decode_length (s_len);
    buf = xzmalloc (len);
    if (base64_decode_block (buf, &len, s_buf, s_len) < 0)
        log_err_exit ("%s: base64 decode error", key);
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
    free (buf);
    Jput (o);
    free (json_str);
}


static void dump_kvs_val (const char *key, const char *json_str)
{
    json_object *o = Jfromstr (json_str);
    if (!o) {
        printf ("%s: invalid JSON", key);
        return;
    }
    output_key_json_object (key, o);
    Jput (o);
}

static void dump_kvs_dir (kvsdir_t *dir, bool ropt)
{
    flux_future_t *f;
    kvsitr_t *itr;
    const char *name;
    flux_t *h = kvsdir_handle (dir);
    const char *rootref = kvsdir_rootref (dir);
    char *key;

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            const char *link;
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key, rootref))
                    || flux_kvs_lookup_getf (f, "s", &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            flux_future_destroy (f);

        } else if (kvsdir_isdir (dir, name)) {
            if (ropt) {
                kvsdir_t *ndir;
                if (kvsdir_get_dir (dir, &ndir, "%s", name) < 0)
                    log_err_exit ("%s", key);
                dump_kvs_dir (ndir, ropt);
                kvsdir_destroy (ndir);
            } else
                printf ("%s.\n", key);
        } else {
            char *json_str;
            if (kvsdir_get (dir, name, &json_str) < 0)
                log_err_exit ("%s", key);
            dump_kvs_val (key, json_str);
            free (json_str);
        }
        free (key);
    }
    kvsitr_destroy (itr);
}

void cmd_watch_dir (flux_t *h, int argc, char **argv)
{
    bool ropt = false;
    char *key;
    kvsdir_t *dir = NULL;
    int rc;
    int count = -1;

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
    if (argc == 2) {
        count = strtoul (argv[0], NULL, 10);
        argc--;
        argv++;
    }
    if (argc != 1)
        log_msg_exit ("watchdir: specify one directory");
    key = argv[0];

    rc = kvs_get_dir (h, &dir, "%s", key);
    while (rc == 0 || (rc < 0 && errno == ENOENT)) {
        if (rc < 0) {
            printf ("%s: %s\n", key, flux_strerror (errno));
            if (dir)
                kvsdir_destroy (dir);
            dir = NULL;
        } else {
            dump_kvs_dir (dir, ropt);
            printf ("======================\n");
            fflush (stdout);
        }
        if (--count == 0)
            goto done;
        rc = kvs_watch_once_dir (h, &dir, "%s", key);
    }
    log_err_exit ("%s", key);
done:
    kvsdir_destroy (dir);
}

void cmd_dir (flux_t *h, int argc, char **argv)
{
    bool ropt = false;
    kvsdir_t *dir;

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
    if (argc != 1)
        log_msg_exit ("dir: specify directory");
    if (kvs_get_dir (h, &dir, "%s", argv[0]) < 0)
        log_err_exit ("%s", argv[0]);
    dump_kvs_dir (dir, ropt);
    kvsdir_destroy (dir);
}

void cmd_dirat (flux_t *h, int argc, char **argv)
{
    bool ropt = false;
    kvsdir_t *dir = NULL;
    const char *json_str;
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
            || flux_kvs_lookup_get (f, &json_str) < 0
            || !(dir = kvsdir_create (h, argv[0], argv[1], json_str)))
        log_err_exit ("%s", argv[1]);
    dump_kvs_dir (dir, ropt);
    kvsdir_destroy (dir);
    flux_future_destroy (f);
}

void cmd_dirsize (flux_t *h, int argc, char **argv)
{
    kvsdir_t *dir = NULL;
    if (argc != 1)
        log_msg_exit ("dirsize: specify one directory");
    if (kvs_get_dir (h, &dir, "%s", argv[0]) < 0)
        log_err_exit ("%s", argv[0]);
    printf ("%d\n", kvsdir_get_size (dir));
    kvsdir_destroy (dir);
}

void cmd_copy (flux_t *h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("copy: specify srckey dstkey");
    if (kvs_copy (h, argv[0], argv[1]) < 0)
        log_err_exit ("kvs_copy %s %s", argv[0], argv[1]);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_move (flux_t *h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("move: specify srckey dstkey");
    if (kvs_move (h, argv[0], argv[1]) < 0)
        log_err_exit ("kvs_move %s %s", argv[0], argv[1]);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
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
    if (argc != 1)
        log_msg_exit ("put-treeobj: specify key=val");
    char *key = xstrdup (argv[0]);
    char *val = strchr (key, '=');
    if (!val)
        log_msg_exit ("put-treeobj: you must specify a value as key=val");
    *val++ = '\0';
    if (kvs_put_treeobj (h, key, val) < 0)
        log_err_exit ("kvs_put_treeobj %s=%s", key, val);
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
    free (key);
}

void cmd_readlinkat (flux_t *h, int argc, char **argv)
{
    const char *target;
    flux_future_t *f;

    if (argc != 2)
        log_msg_exit ("readlink: specify treeobj and key");
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, argv[1], argv[0]))
            || flux_kvs_lookup_getf (f, "s", &target) < 0)
        log_err_exit ("%s", argv[1]);
    else
        printf ("%s\n", target);
    flux_future_destroy (f);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
