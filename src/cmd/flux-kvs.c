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
#include <flux/core.h>
#include <flux/optparse.h>
#include <unistd.h>
#include <fcntl.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/base64_json.h"
#include "src/common/libutil/readall.h"

int cmd_get (optparse_t *p, int argc, char **argv);
int cmd_put (optparse_t *p, int argc, char **argv);
int cmd_unlink (optparse_t *p, int argc, char **argv);
int cmd_link (optparse_t *p, int argc, char **argv);
int cmd_readlink (optparse_t *p, int argc, char **argv);
int cmd_mkdir (optparse_t *p, int argc, char **argv);
int cmd_version (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);
int cmd_watch (optparse_t *p, int argc, char **argv);
int cmd_watch_dir (optparse_t *p, int argc, char **argv);
int cmd_dropcache (optparse_t *p, int argc, char **argv);
int cmd_dropcache_all (optparse_t *p, int argc, char **argv);
int cmd_copy (optparse_t *p, int argc, char **argv);
int cmd_move (optparse_t *p, int argc, char **argv);
int cmd_dir (optparse_t *p, int argc, char **argv);

static struct optparse_option dir_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Recursively display keys under subdirectories",
    },
    { .name = "directory", .key = 'd', .has_arg = 0,
      .usage = "List directory entries and not values",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option watch_opts[] =  {
    { .name = "current", .key = 'o', .has_arg = 0,
      .usage = "Output current value before changes",
    },
    { .name = "count", .key = 'c', .has_arg = 1,
      .usage = "Display at most count changes",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option watch_dir_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Recursively display keys under subdirectories",
    },
    { .name = "directory", .key = 'd', .has_arg = 0,
      .usage = "List directory entries and not values",
    },
    { .name = "current", .key = 'o', .has_arg = 0,
      .usage = "Output current value before changes",
    },
    { .name = "count", .key = 'c', .has_arg = 1,
      .usage = "Display at most count changes",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "get",
      "key [key...]",
      "Get value stored under key",
      cmd_get,
      0,
      NULL
    },
    { "put",
      "key=value [key=value...]",
      "Store value under key",
      cmd_put,
      0,
      NULL
    },
    { "dir",
      "[-R] [-d] [key]",
      "Display all keys under directory",
      cmd_dir,
      0,
      dir_opts
    },
    { "unlink",
      "key [key...]",
      "Remove key",
      cmd_unlink,
      0,
      NULL
    },
    { "link",
      "target linkname",
      "Create a new name for target",
      cmd_link,
      0,
      NULL
    },
    { "readlink",
      "key [key...]",
      "Retrieve the key a link refers to",
      cmd_readlink,
      0,
      NULL
    },
    { "mkdir",
      "key [key...]",
      "Create a directory",
      cmd_mkdir,
      0,
      NULL
    },
    { "copy",
      "source destination",
      "Copy source key to destination key",
      cmd_copy,
      0,
      NULL
    },
    { "move",
      "source destination",
      "Move source key to destination key",
      cmd_move,
      0,
      NULL
    },
    { "dropcache",
      "",
      "Tell local KVS to drop its cache",
      cmd_dropcache,
      0,
      NULL
    },
    { "dropcache-all",
      "",
      "Instruct all KVS to drop its cache",
      cmd_dropcache_all,
      0,
      NULL
    },
    { "watch",
      "[-o] [-c count] key",
      "Watch value specified by key",
      cmd_watch,
      0,
      watch_opts
    },
    { "watch-dir",
      "[-R] [-d] [-o] [-c count] key",
      "Watch directory specified by key",
      cmd_watch_dir,
      0,
      watch_dir_opts
    },
    { "version",
      "",
      "Display curent KVS version",
      cmd_version,
      0,
      NULL
    },
    { "wait",
      "version",
      "Block until the KVS reaches version",
      cmd_wait,
      0,
      NULL
    },
    OPTPARSE_SUBCMD_END
};

int usage (optparse_t *p, struct optparse_option *o, const char *optarg)
{
    struct optparse_subcommand *s;
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "Common commands from flux-kvs:\n");
    s = subcommands;
    while (s->name) {
        fprintf (stderr, "   %-15s %s\n", s->name, s->doc);
        s++;
    }
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    char *cmdusage = "[OPTIONS] COMMAND ARGS";
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("flux-kvs");

    p = optparse_create ("flux-kvs");

    /* Override help option for our own */
    if (optparse_set (p, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");

    /* Override --help callback in favor of our own above */
    if (optparse_set (p, OPTPARSE_OPTION_CB, "help", usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set() failed");

    /* Don't print internal subcommands, we do it ourselves */
    if (optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (PRINT_SUBCMDS)");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optind])) {
        usage (p, NULL, NULL);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    optparse_set_data (p, "flux_handle", h);

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    flux_close (h);
    optparse_destroy (p);
    log_fini ();
    return (exitval);
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

int cmd_get (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    char *json_str;
    int optindex, i;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        if (kvs_get (h, argv[i], &json_str) < 0)
            log_err_exit ("%s", argv[i]);
        output_key_json_str (NULL, json_str, argv[i]);
        free (json_str);
    }
    return (0);
}

int cmd_put (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex, i;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
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
    return (0);
}

int cmd_unlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex, i;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        /* FIXME: unlink nonexistent silently fails */
        /* FIXME: unlink directory silently succeeds */
        if (kvs_unlink (h, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    return (0);
}

int cmd_link (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 2))
        log_msg_exit ("link: specify target and link_name");
    if (kvs_symlink (h, argv[optindex + 1], argv[optindex]) < 0)
        log_err_exit ("%s", argv[optindex + 1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    return (0);
}

int cmd_readlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex, i;
    char *target;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        if (kvs_get_symlink (h, argv[i], &target) < 0)
            log_err_exit ("%s", argv[i]);
        else
            printf ("%s\n", target);
        free (target);
    }
    return (0);
}

int cmd_mkdir (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex, i;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        if (kvs_mkdir (h, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    return (0);
}

int cmd_version (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int vers;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    if (kvs_get_version (h, &vers) < 0)
        log_err_exit ("kvs_get_version");
    printf ("%d\n", vers);
    return (0);
}

int cmd_wait (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int vers;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 1))
        log_msg_exit ("wait: specify a version");
    vers = strtoul (argv[optindex], NULL, 10);
    if (kvs_wait_version (h, vers) < 0)
        log_err_exit ("kvs_get_version");
    return (0);
}

int cmd_watch (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    char *json_str = NULL;
    char *key;
    int count;
    bool oopt;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 1))
        log_msg_exit ("watch: specify one key");

    oopt = optparse_hasopt (p, "current");
    count = optparse_get_int (p, "count", -1);

    key = argv[optindex];
    if (kvs_get (h, key, &json_str) < 0 && errno != ENOENT) 
        log_err_exit ("%s", key);
    if (oopt)
        output_key_json_str (NULL, json_str, key);
    while (count) {
        if (kvs_watch_once (h, argv[optindex], &json_str) < 0 && errno != ENOENT)
            log_err_exit ("%s", argv[optindex]);
        output_key_json_str (NULL, json_str, key);
        count--;
    }
    free (json_str);
    return (0);
}

int cmd_dropcache (optparse_t *p, int argc, char **argv)
{
    flux_t *h;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    if (kvs_dropcache (h) < 0)
        log_err_exit ("kvs_dropcache");
    return (0);
}

int cmd_dropcache_all (optparse_t *p, int argc, char **argv)
{
    flux_t *h;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    flux_msg_t *msg = flux_event_encode ("kvs.dropcache", NULL);
    if (!msg || flux_send (h, msg, 0) < 0)
        log_err_exit ("flux_send");
    flux_msg_destroy (msg);
    return (0);
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

static void dump_kvs_dir (kvsdir_t *dir, bool Ropt, bool dopt)
{
    kvsitr_t *itr;
    const char *name;
    char *key;

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            char *link;
            if (kvsdir_get_symlink (dir, name, &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            free (link);

        } else if (kvsdir_isdir (dir, name)) {
            if (Ropt) {
                kvsdir_t *ndir;
                if (kvsdir_get_dir (dir, &ndir, "%s", name) < 0)
                    log_err_exit ("%s", key);
                dump_kvs_dir (ndir, Ropt, dopt);
                kvsdir_destroy (ndir);
            } else
                printf ("%s.\n", key);
        } else {
            if (!dopt) {
                char *json_str;
                if (kvsdir_get (dir, name, &json_str) < 0)
                    log_err_exit ("%s", key);
                dump_kvs_val (key, json_str);
                free (json_str);
            }
            else
                printf ("%s\n", key);
        }
        free (key);
    }
    kvsitr_destroy (itr);
}

int cmd_watch_dir (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    bool Ropt;
    bool dopt;
    bool oopt;
    char *key;
    kvsdir_t *dir = NULL;
    int rc;
    int count;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 1))
        log_msg_exit ("watchdir: specify one directory");

    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");
    oopt = optparse_hasopt (p, "current");
    count = optparse_get_int (p, "count", -1);

    key = argv[optindex];

    rc = kvs_get_dir (h, &dir, "%s", key);
    if (oopt) {
        dump_kvs_dir (dir, Ropt, dopt);
        printf ("======================\n");
        fflush (stdout);
    }
    while (rc == 0 || (rc < 0 && errno == ENOENT)) {
        rc = kvs_watch_once_dir (h, &dir, "%s", key);
        if (rc < 0) {
            printf ("%s: %s\n", key, flux_strerror (errno));
            if (dir)
                kvsdir_destroy (dir);
            dir = NULL;
        } else {
            dump_kvs_dir (dir, Ropt, dopt);
            printf ("======================\n");
            fflush (stdout);
        }
        if (--count == 0)
            goto done;
    }
    log_err_exit ("%s", key);
done:
    kvsdir_destroy (dir);
    return (0);
}

int cmd_dir (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    bool Ropt;
    bool dopt;
    char *key;
    kvsdir_t *dir;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");

    if (optindex == argc)
        key = ".";
    else if (optindex == (argc - 1))
        key = argv[optindex];
    else
        log_msg_exit ("dir: specify zero or one directory");
    if (kvs_get_dir (h, &dir, "%s", key) < 0)
        log_err_exit ("%s", key);
    dump_kvs_dir (dir, Ropt, dopt);
    kvsdir_destroy (dir);
    return (0);
}

int cmd_copy (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 2))
        log_msg_exit ("copy: specify srckey dstkey");
    if (kvs_copy (h, argv[optindex], argv[optindex + 1]) < 0)
        log_err_exit ("kvs_copy %s %s", argv[optindex], argv[optindex + 1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    return (0);
}

int cmd_move (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 2))
        log_msg_exit ("move: specify srckey dstkey");
    if (kvs_move (h, argv[optindex], argv[optindex + 1]) < 0)
        log_err_exit ("kvs_move %s %s", argv[optindex], argv[optindex + 1]);
    if (kvs_commit (h) < 0)
        log_err_exit ("kvs_commit");
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
