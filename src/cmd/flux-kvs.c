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
#include <json.h>
#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/base64_json.h"
#include "src/common/libutil/readall.h"


#define OPTIONS "+h"
static const struct option longopts[] = {
    {"help",       no_argument,  0, 'h'},
    { 0, 0, 0, 0 },
};

void cmd_get (flux_t h, int argc, char **argv);
void cmd_type (flux_t h, int argc, char **argv);
void cmd_put (flux_t h, int argc, char **argv);
void cmd_unlink (flux_t h, int argc, char **argv);
void cmd_link (flux_t h, int argc, char **argv);
void cmd_readlink (flux_t h, int argc, char **argv);
void cmd_mkdir (flux_t h, int argc, char **argv);
void cmd_exists (flux_t h, int argc, char **argv);
void cmd_version (flux_t h, int argc, char **argv);
void cmd_wait (flux_t h, int argc, char **argv);
void cmd_watch (flux_t h, int argc, char **argv);
void cmd_watch_dir (flux_t h, int argc, char **argv);
void cmd_dropcache (flux_t h, int argc, char **argv);
void cmd_dropcache_all (flux_t h, int argc, char **argv);
void cmd_copy_tokvs (flux_t h, int argc, char **argv);
void cmd_copy_fromkvs (flux_t h, int argc, char **argv);
void cmd_copy (flux_t h, int argc, char **argv);
void cmd_move (flux_t h, int argc, char **argv);
void cmd_dir (flux_t h, int argc, char **argv);
void cmd_dirsize (flux_t h, int argc, char **argv);
void cmd_get_treeobj (flux_t h, int argc, char **argv);
void cmd_put_treeobj (flux_t h, int argc, char **argv);
void cmd_getat (flux_t h, int argc, char **argv);


void usage (void)
{
    fprintf (stderr,
"Usage: flux-kvs get             key [key...]\n"
"       flux-kvs type            key [key...]\n"
"       flux-kvs put             key=val [key=val...]\n"
"       flux-kvs unlink          key [key...]\n"
"       flux-kvs link            target link_name\n"
"       flux-kvs readlink        key\n"
"       flux-kvs mkdir           key [key...]\n"
"       flux-kvs exists          key\n"
"       flux-kvs watch           key\n"
"       flux-kvs watch-dir [-r]  key\n"
"       flux-kvs copy-tokvs      key file\n"
"       flux-kvs copy-fromkvs    key file\n"
"       flux-kvs copy            srckey dstkey\n"
"       flux-kvs move            srckey dstkey\n"
"       flux-kvs dir [-r]        [key]\n"
"       flux-kvs dirsize         key\n"
"       flux-kvs version\n"
"       flux-kvs wait            version\n"
"       flux-kvs dropcache\n"
"       flux-kvs dropcache-all\n"
"       flux-kvs get-treeobj     key\n"
"       flux-kvs put-treeobj     key=treeobj\n"
"       flux-kvs getat           treeobj key\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *cmd;

    log_init ("flux-kvs");

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
    else
        usage ();

    flux_close (h);
    log_fini ();
    return 0;
}

void cmd_type (flux_t h, int argc, char **argv)
{
    char *json_str;
    JSON o;
    int i;

    if (argc == 0)
        log_msg_exit ("get-type: specify one or more keys");
    for (i = 0; i < argc; i++) {
        if (kvs_get (h, argv[i], &json_str) < 0)
            log_err_exit ("%s", argv[i]);
        if (!(o = Jfromstr (json_str)))
            log_msg_exit ("%s: malformed JSON", argv[i]);
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
}

void cmd_get (flux_t h, int argc, char **argv)
{
    char *json_str;
    JSON o;
    int i;

    if (argc == 0)
        log_msg_exit ("get: specify one or more keys");
    for (i = 0; i < argc; i++) {
        if (kvs_get (h, argv[i], &json_str) < 0)
            log_err_exit ("%s", argv[i]);
        if (!(o = Jfromstr (json_str)))
            log_msg_exit ("%s: malformed JSON", argv[i]);
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
        Jput (o);
        free (json_str);
    }
}

void cmd_put (flux_t h, int argc, char **argv)
{
    int i;

    if (argc == 0)
        log_msg_exit ("put: specify one or more key=value pairs");
    for (i = 0; i < argc; i++) {
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');
        if (!val)
            log_msg_exit ("put: you must specify a value as key=value");
        *val++ = '\0';
        if (kvs_put (h, key, val) < 0) {
            if (errno != EINVAL || kvs_put_string (h, key, val) < 0)
                log_err_exit ("%s", key);
        }
        free (key);
    }
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_unlink (flux_t h, int argc, char **argv)
{
    int i;

    if (argc == 0)
        log_msg_exit ("unlink: specify one or more keys");
    for (i = 0; i < argc; i++) {
        /* FIXME: unlink nonexistent silently fails */
        /* FIXME: unlink directory silently succedes */
        if (kvs_unlink (h, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_link (flux_t h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("link: specify target and link_name");
    if (kvs_symlink (h, argv[1], argv[0]) < 0)
        log_err_exit ("%s", argv[1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_readlink (flux_t h, int argc, char **argv)
{
    int i;
    char *target;

    if (argc == 0)
        log_msg_exit ("readlink: specify one or more keys"); 
    for (i = 0; i < argc; i++) {
        if (kvs_get_symlink (h, argv[i], &target) < 0)
            log_err_exit ("%s", argv[i]);
        else
            printf ("%s\n", target);
        free (target);
    }
}

void cmd_mkdir (flux_t h, int argc, char **argv)
{
    int i;

    if (argc == 0)
        log_msg_exit ("mkdir: specify one or more directories");
    for (i = 0; i < argc; i++) {
        if (kvs_mkdir (h, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

bool key_exists (flux_t h, const char *key)
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

void cmd_exists (flux_t h, int argc, char **argv)
{
    int i;
    if (argc == 0)
        log_msg_exit ("exist: specify one or more keys");
    for (i = 0; i < argc; i++) {
        if (!key_exists (h, argv[i]))
            exit (1);
    }
}

void cmd_version (flux_t h, int argc, char **argv)
{
    int vers;
    if (argc != 0)
        log_msg_exit ("version: takes no arguments");
    if (kvs_get_version (h, &vers) < 0)
        log_err_exit ("kvs_get_version");
    printf ("%d\n", vers);
}

void cmd_wait (flux_t h, int argc, char **argv)
{
    int vers;
    if (argc != 1)
        log_msg_exit ("wait: specify a version");
    vers = strtoul (argv[0], NULL, 10);
    if (kvs_wait_version (h, vers) < 0)
        log_err_exit ("kvs_get_version");
    //printf ("%d\n", vers);
}

void cmd_watch (flux_t h, int argc, char **argv)
{
    char *json_str = NULL;
    char *key;

    if (argc != 1)
        log_msg_exit ("watch: specify one key");
    key = argv[0];
    if (kvs_get (h, key, &json_str) < 0 && errno != ENOENT) 
        log_err_exit ("%s", key);
    do {
        printf ("%s\n", json_str ? json_str : "NULL");
        if (kvs_watch_once (h, argv[0], &json_str) < 0 && errno != ENOENT)
            log_err_exit ("%s", argv[0]);
    } while (true);
}

void cmd_dropcache (flux_t h, int argc, char **argv)
{
    if (argc != 0)
        log_msg_exit ("dropcache: takes no arguments");
    if (kvs_dropcache (h) < 0)
        log_err_exit ("kvs_dropcache");
}

void cmd_dropcache_all (flux_t h, int argc, char **argv)
{
    if (argc != 0)
        log_msg_exit ("dropcache-all: takes no arguments");
    flux_msg_t *msg = flux_event_encode ("kvs.dropcache", NULL);
    if (!msg || flux_send (h, msg, 0) < 0)
        log_err_exit ("flux_send");
    flux_msg_destroy (msg);
}

void cmd_copy_tokvs (flux_t h, int argc, char **argv)
{
    char *file, *key;
    int fd, len;
    uint8_t *buf;
    JSON o;

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
    o = Jnew ();
    json_object_object_add (o, "data", base64_json_encode (buf, len));
    if (kvs_put (h, key, Jtostr (o)) < 0)
        log_err_exit ("%s", key);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    Jput (o);
    free (buf);
}

void cmd_copy_fromkvs (flux_t h, int argc, char **argv)
{
    char *file, *key;
    int fd, len;
    uint8_t *buf;
    JSON o;
    char *json_str;

    if (argc != 2)
        log_msg_exit ("copy-fromkvs: specify key and filename");
    key = argv[0];
    file = argv[1];
    if (kvs_get (h, key, &json_str) < 0)
        log_err_exit ("%s", key);
    if (!(o = Jfromstr (json_str)))
        log_msg_exit ("%s: invalid JSON", key);
    if (base64_json_decode (Jobj_get (o, "data"), &buf, &len) < 0)
        log_err_exit ("%s: decode error", key);
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
    JSON o = Jfromstr (json_str);
    if (!o) {
        printf ("%s: invalid JSON", key);
        return;
    }
    switch (json_object_get_type (o)) {
        case json_type_null:
            printf ("%s = nil\n", key);
            break;
        case json_type_boolean:
            printf ("%s = %s\n", key, json_object_get_boolean (o)
                                      ? "true" : "false");
            break;
        case json_type_double:
            printf ("%s = %f\n", key, json_object_get_double (o));
            break;
        case json_type_int:
            printf ("%s = %d\n", key, json_object_get_int (o));
            break;
        case json_type_string:
            printf ("%s = %s\n", key, json_object_get_string (o));
            break;
        case json_type_array:
        case json_type_object:
        default:
            printf ("%s = %s\n", key, Jtostr (o));
            break;
    }
    Jput (o);
}

static void dump_kvs_dir (flux_t h, bool ropt, const char *path)
{
    kvsdir_t *dir;
    kvsitr_t *itr;
    const char *name;
    char *key;

    if (kvs_get_dir (h, &dir, "%s", path) < 0)
        log_err_exit ("%s", path);

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            char *link;
            if (kvs_get_symlink (h, key, &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            free (link);

        } else if (kvsdir_isdir (dir, name)) {
            if (ropt)
                dump_kvs_dir (h, ropt, key);
            else
                printf ("%s.\n", key);
        } else {
            char *json_str;
            if (kvs_get (h, key, &json_str) < 0)
                log_err_exit ("%s", key);
            dump_kvs_val (key, json_str);
            free (json_str);
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}

void cmd_watch_dir (flux_t h, int argc, char **argv)
{
    bool ropt = false;
    char *key;
    kvsdir_t *dir = NULL;
    int rc;

    if (argc > 0 && !strcmp (argv[0], "-r")) {
        ropt = true;
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
            dump_kvs_dir (h, ropt, key);
            printf ("======================\n");
        }
        rc = kvs_watch_once_dir (h, &dir, "%s", key);
    }
    log_err_exit ("%s", key);

}

void cmd_dir (flux_t h, int argc, char **argv)
{
    bool ropt = false;

    if (argc > 0 && !strcmp (argv[0], "-r")) {
        ropt = true;
        argc--;
        argv++;
    }
    if (argc == 0)
        dump_kvs_dir (h, ropt, ".");
    else if (argc == 1)
        dump_kvs_dir (h, ropt, argv[0]);
    else
        log_msg_exit ("dir: specify zero or one directory");
}

void cmd_dirsize (flux_t h, int argc, char **argv)
{
    kvsdir_t *dir = NULL;
    if (argc != 1)
        log_msg_exit ("dirsize: specify one directory");
    if (kvs_get_dir (h, &dir, "%s", argv[0]) < 0)
        log_err_exit ("%s", argv[0]);
    printf ("%d\n", kvsdir_get_size (dir));
    kvsdir_destroy (dir);
}

void cmd_copy (flux_t h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("copy: specify srckey dstkey");
    if (kvs_copy (h, argv[0], argv[1]) < 0)
        log_err_exit ("kvs_copy %s %s", argv[0], argv[1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_move (flux_t h, int argc, char **argv)
{
    if (argc != 2)
        log_msg_exit ("move: specify srckey dstkey");
    if (kvs_move (h, argv[0], argv[1]) < 0)
        log_err_exit ("kvs_move %s %s", argv[0], argv[1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
}

void cmd_get_treeobj (flux_t h, int argc, char **argv)
{
    char *treeobj = NULL;
    if (argc != 1)
        log_msg_exit ("get-treeobj: specify key");
    if (kvs_get_treeobj (h, argv[0], &treeobj) < 0)
        log_err_exit ("kvs_get_treeobj %s", argv[0]);
    printf ("%s\n", treeobj);
    free (treeobj);
}

void cmd_getat (flux_t h, int argc, char **argv)
{
    char *val = NULL;
    if (argc != 2)
        log_msg_exit ("getat: specify treeobj and key");
    if (kvs_getat (h, argv[0], argv[1], &val) < 0)
        log_err_exit ("kvs_getat %s %s", argv[0], argv[1]);
    printf ("%s\n", val);
    free (val);
}

void cmd_put_treeobj (flux_t h, int argc, char **argv)
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
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");

}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
