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
#include <sys/ioctl.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <unistd.h>
#include <fcntl.h>
#include <jansson.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/readall.h"
#include "src/common/libkvs/treeobj.h"

int cmd_get (optparse_t *p, int argc, char **argv);
int cmd_put (optparse_t *p, int argc, char **argv);
int cmd_unlink (optparse_t *p, int argc, char **argv);
int cmd_link (optparse_t *p, int argc, char **argv);
int cmd_readlink (optparse_t *p, int argc, char **argv);
int cmd_mkdir (optparse_t *p, int argc, char **argv);
int cmd_version (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);
int cmd_watch (optparse_t *p, int argc, char **argv);
int cmd_dropcache (optparse_t *p, int argc, char **argv);
int cmd_copy (optparse_t *p, int argc, char **argv);
int cmd_move (optparse_t *p, int argc, char **argv);
int cmd_dir (optparse_t *p, int argc, char **argv);
int cmd_ls (optparse_t *p, int argc, char **argv);

static void dump_kvs_dir (kvsdir_t *dir, bool Ropt, bool dopt);

#define min(a,b) ((a)<(b)?(a):(b))

static struct optparse_option dir_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Recursively display keys under subdirectories",
    },
    { .name = "directory", .key = 'd', .has_arg = 0,
      .usage = "List directory entries and not values",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option ls_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "List directory recursively",
    },
    { .name = "directory", .key = 'd', .has_arg = 0,
      .usage = "List directory instead of contents",
    },
    { .name = "width", .key = 'w', .has_arg = 1,
      .usage = "Set output width to COLS.  0 means no limit",
    },
    { .name = "1", .key = '1', .has_arg = 0,
      .usage = "Force one entry per line",
    },
    { .name = "classify", .key = 'F', .has_arg = 0,
      .usage = "Append indicator (one of .@) to entries",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option watch_opts[] =  {
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

static struct optparse_option dropcache_opts[] =  {
    { .name = "all", .key = 'a', .has_arg = 0,
      .usage = "Drop KVS across all ranks",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option unlink_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Remove directory contents recursively",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "ignore nonexistent files",
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
    { "ls",
      "[-R] [-d] [-F] [-w COLS] [-1] [key...]",
      "List directory",
      cmd_ls,
      0,
      ls_opts
    },
    { "unlink",
      "key [key...]",
      "Remove key",
      cmd_unlink,
      0,
      unlink_opts
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
      "[--all]",
      "Tell KVS to drop its cache",
      cmd_dropcache,
      0,
      dropcache_opts
    },
    { "watch",
      "[-R] [-d] [-o] [-c count] key",
      "Watch key and output changes",
      cmd_watch,
      0,
      watch_opts
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

int cmd_get (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    const char *key, *json_str;
    flux_future_t *f;
    int optindex, i;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        key = argv[i];
        if (!(f = flux_kvs_lookup (h, 0, key))
                || flux_kvs_lookup_get (f, &json_str) < 0)
            log_err_exit ("%s", key);
        output_key_json_str (NULL, json_str, key);
        flux_future_destroy (f);
    }
    return (0);
}

int cmd_put (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    flux_future_t *f;
    flux_kvs_txn_t *txn;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');
        if (!val)
            log_msg_exit ("put: you must specify a value as key=value");
        *val++ = '\0';

        if (flux_kvs_txn_put (txn, 0, key, val) < 0) {
            if (errno != EINVAL)
                log_err_exit ("%s", key);
            if (flux_kvs_txn_pack (txn, 0, key, "s", val) < 0)
                log_err_exit ("%s", key);
        }
        free (key);
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return (0);
}

/* Some checks prior to unlinking key:
 * - fail if key does not exist (ENOENT) and fopt not specified or
 *   other fatal lookup error
 * - fail if key is a non-empty directory (ENOTEMPTY) and -R was not specified
 */
static int unlink_safety_check (flux_t *h, const char *key, bool Ropt,
                                bool fopt, bool *unlinkable)
{
    flux_future_t *f;
    kvsdir_t *dir = NULL;
    const char *json_str;
    int rc = -1;

    *unlinkable = false;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key)))
        goto done;
    if (flux_kvs_lookup_get (f, &json_str) < 0) {
        if (errno == ENOENT && fopt)
            goto out;
        if (errno != ENOTDIR)
            goto done;
    }
    else if (!Ropt) {
        if (!(dir = kvsdir_create (h, NULL, key, json_str)))
            goto done;
        if (kvsdir_get_size (dir) > 0) {
            errno = ENOTEMPTY;
            goto done;
        }
    }
    *unlinkable = true;
out:
    rc = 0;
done:
    if (dir)
        kvsdir_destroy (dir);
    flux_future_destroy (f);
    return rc;
}

int cmd_unlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    flux_future_t *f;
    flux_kvs_txn_t *txn;
    bool Ropt, fopt;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    Ropt = optparse_hasopt (p, "recursive");
    fopt = optparse_hasopt (p, "force");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        bool unlinkable;
        if (unlink_safety_check (h, argv[i], Ropt, fopt, &unlinkable) < 0)
            log_err_exit ("cannot unlink '%s'", argv[i]);
        if (unlinkable) {
            if (flux_kvs_txn_unlink (txn, 0, argv[i]) < 0)
                log_err_exit ("%s", argv[i]);
        }
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return (0);
}

int cmd_link (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 2))
        log_msg_exit ("link: specify target and link_name");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_symlink (txn, 0, argv[optindex + 1], argv[optindex]) < 0)
        log_err_exit ("%s", argv[optindex + 1]);
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return (0);
}

int cmd_readlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    const char *target;
    flux_future_t *f;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    for (i = optindex; i < argc; i++) {
        if (!(f = flux_kvs_lookup (h, FLUX_KVS_READLINK, argv[i]))
                || flux_kvs_lookup_get (f, &target) < 0)
            log_err_exit ("%s", argv[i]);
        else
            printf ("%s\n", target);
        flux_future_destroy (f);
    }
    return (0);
}

int cmd_mkdir (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        if (flux_kvs_txn_mkdir (txn, 0, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
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

#define WATCH_DIR_SEPARATOR "======================"

static void watch_dump_key (const char *json_str,
                            const char *arg,
                            bool *prev_output_iskey)
{
    output_key_json_str (NULL, json_str, arg);
    fflush (stdout);
    *prev_output_iskey = true;
}

static void watch_dump_kvsdir (kvsdir_t *dir, bool Ropt, bool dopt,
                               const char *arg) {
    if (!dir) {
        output_key_json_str (NULL, NULL, arg);
        printf ("%s\n", WATCH_DIR_SEPARATOR);
        return;
    }

    dump_kvs_dir (dir, Ropt, dopt);
    printf ("%s\n", WATCH_DIR_SEPARATOR);
    fflush (stdout);
}

int cmd_watch (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    kvsdir_t *dir = NULL;
    char *json_str = NULL;
    char *key;
    int count;
    bool Ropt;
    bool dopt;
    bool oopt;
    bool isdir = false;
    bool prev_output_iskey = false;
    int optindex;
    int rc;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 1))
        log_msg_exit ("watch: specify one key");

    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");
    oopt = optparse_hasopt (p, "current");
    count = optparse_get_int (p, "count", -1);

    key = argv[optindex];

    rc = kvs_get (h, key, &json_str);
    if (rc < 0 && (errno != ENOENT && errno != EISDIR))
        log_err_exit ("%s", key);

    /* key is a directory, setup for dir logic appropriately */
    if (rc < 0 && errno == EISDIR) {
        rc = kvs_get_dir (h, &dir, "%s", key);
        if (rc < 0 && errno != ENOENT)
            log_err_exit ("%s", key);
        isdir = true;
        free (json_str);
        json_str = NULL;
    }

    if (oopt) {
        if (isdir)
            watch_dump_kvsdir (dir, Ropt, dopt, key);
        else
            watch_dump_key (json_str, key, &prev_output_iskey);
    }

    while (count && (rc == 0 || (rc < 0 && errno == ENOENT))) {
        if (isdir) {
            rc = kvs_watch_once_dir (h, &dir, "%s", key);
            if (rc < 0 && (errno != ENOENT && errno != ENOTDIR)) {
                printf ("%s: %s\n", key, flux_strerror (errno));
                if (dir)
                    kvsdir_destroy (dir);
                dir = NULL;
            }
            else if (rc < 0 && errno == ENOENT) {
                if (dir)
                    kvsdir_destroy (dir);
                dir = NULL;
                watch_dump_kvsdir (dir, Ropt, dopt, key);
            }
            else if (!rc) {
                watch_dump_kvsdir (dir, Ropt, dopt, key);
            }
            else { /* rc < 0 && errno == ENOTDIR */
                /* We were watching a dir that is now a key, need to
                 * reset logic to the 'key' part of this loop */
                isdir = false;
                if (dir)
                    kvsdir_destroy (dir);
                dir = NULL;

                rc = kvs_get (h, key, &json_str);
                if (rc < 0 && errno != ENOENT)
                    printf ("%s: %s\n", key, flux_strerror (errno));
                else
                    watch_dump_key (json_str, key, &prev_output_iskey);
            }
        }
        else {
            rc = kvs_watch_once (h, key, &json_str);
            if (rc < 0 && (errno != ENOENT && errno != EISDIR)) {
                printf ("%s: %s\n", key, flux_strerror (errno));
                free (json_str);
                json_str = NULL;
            }
            else if (rc < 0 && errno == ENOENT) {
                free (json_str);
                json_str = NULL;
                watch_dump_key (NULL, key, &prev_output_iskey);
            }
            else if (!rc) {
                watch_dump_key (json_str, key, &prev_output_iskey);
            }
            else { /* rc < 0 && errno == EISDIR */
                /* We were watching a key that is now a dir.  So we
                 * have to move to the directory branch of this loop.
                 */
                isdir = true;
                free (json_str);
                json_str = NULL;

                /* Output dir separator from prior key */
                if (prev_output_iskey) {
                    printf ("%s\n", WATCH_DIR_SEPARATOR);
                    prev_output_iskey = false;
                }

                rc = kvs_get_dir (h, &dir, "%s", key);
                if (rc < 0 && errno != ENOENT)
                    printf ("%s: %s\n", key, flux_strerror (errno));
                else /* rc == 0 || (rc < 0 && errno == ENOENT) */
                    watch_dump_kvsdir (dir, Ropt, dopt, key);
            }
        }
        count--;
    }
    if (dir)
        kvsdir_destroy (dir);
    free (json_str);
    return (0);
}

int cmd_dropcache (optparse_t *p, int argc, char **argv)
{
    flux_t *h;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    if (optparse_hasopt (p, "all")) {
        flux_msg_t *msg = flux_event_encode ("kvs.dropcache", NULL);
        if (!msg || flux_send (h, msg, 0) < 0)
            log_err_exit ("flux_send");
        flux_msg_destroy (msg);
    }
    else {
        if (kvs_dropcache (h) < 0)
            log_err_exit ("kvs_dropcache");
    }
    return (0);
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

static void dump_kvs_dir (kvsdir_t *dir, bool Ropt, bool dopt)
{
    const char *rootref = kvsdir_rootref (dir);
    flux_t *h = kvsdir_handle (dir);
    flux_future_t *f;
    kvsitr_t *itr;
    const char *name;
    char *key;

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_issymlink (dir, name)) {
            const char *link;
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key, rootref))
                    || flux_kvs_lookup_get (f, &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            flux_future_destroy (f);

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

int cmd_dir (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    bool Ropt;
    bool dopt;
    const char *key, *json_str;
    flux_future_t *f;
    kvsdir_t *dir;
    int optindex;

    optindex = optparse_option_index (p);
    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");
    if (optindex == argc)
        key = ".";
    else if (optindex == (argc - 1))
        key = argv[optindex];
    else
        log_msg_exit ("dir: specify zero or one directory");

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key))
                || flux_kvs_lookup_get (f, &json_str) < 0)
        log_err_exit ("%s", key);
    if (!(dir = kvsdir_create (h, NULL, key, json_str)))
        log_err_exit ("kvsdir_create");
    dump_kvs_dir (dir, Ropt, dopt);
    kvsdir_destroy (dir);
    flux_future_destroy (f);
    return (0);
}

/* Find longest name in a directory.
 */
static int get_dir_maxname (kvsdir_t *dir)
{
    kvsitr_t *itr;
    const char *name;
    int max = 0;

    if (!(itr = kvsitr_create (dir)))
        log_err_exit ("kvsitr_create");
    while ((name = kvsitr_next (itr)))
        if (max < strlen (name))
            max = strlen (name);
    kvsitr_destroy (itr);
    return max;
}

/* Find longest name in a list of names.
 */
static int get_list_maxname (zlist_t *names)
{
    const char *name;
    int max = 0;

    name = zlist_first (names);
    while (name) {
        if (max < strlen (name))
            max = strlen (name);
        name = zlist_next (names);
    }
    return max;
}

/* Determine appropriate terminal window width for columnar output.
 * The options -w COLS or -1 force the width to COLS or 1, respectively.
 * If not forced, ask the tty how wide it is.  If not on a tty, try the
 * COLUMNS environment variable.  If not set, assume 80.
 * N.B. A width of 0 means "unlimited", while a width of 1 effectively
 * forces one entry per line since entries are never broken across lines.
 */
static int get_window_width (optparse_t *p, int fd)
{
    struct winsize w;
    int width;
    const char *s;

    if (optparse_hasopt (p, "1"))
        return 1;
    if ((width = optparse_get_int (p, "width", -1)) >= 0)
        return width;
    if (ioctl (fd, TIOCGWINSZ, &w) == 0)
        return w.ws_col;
    if ((s = getenv ("COLUMNS")) && (width = strtol (s, NULL, 10)) >= 0)
        return width;
    return 80;
}

/* Return true if at character position 'col', there is insufficient
 * room for one more column of 'col_width' without exceeding 'win_width'.
 * Exceptions: 1) win_width of 0 means "unlimited"; 2) col must be > 0.
 * This is a helper for list_kvs_dir_single() and list_kvs_keys().
 */
static bool need_newline (int col, int col_width, int win_width)
{
    return (win_width != 0 && col + col_width > win_width && col > 0);
}

/* List the content of 'dir', arranging output in columns that fit 'win_width',
 * and using a custom column width selected based on the longest entry name.
 */
static void list_kvs_dir_single (kvsdir_t *dir, int win_width, optparse_t *p)
{
    kvsitr_t *itr;
    const char *name;
    int col_width = get_dir_maxname (dir) + 4;
    int col = 0;
    char *namebuf;
    int last_len = 0;

    if (!(namebuf = malloc (col_width + 2)))
        log_err_exit ("malloc");
    if (!(itr = kvsitr_create (dir)))
        log_err_exit ("kvsitr_create");
    while ((name = kvsitr_next (itr))) {
        if (need_newline (col, col_width, win_width)) {
            printf ("\n");
            col = 0;
        }
        else if (last_len > 0) // pad out last entry to col_width
            printf ("%*s", col_width - last_len, "");
        strcpy (namebuf, name);
        if (optparse_hasopt (p, "classify")) {
            if (kvsdir_isdir (dir, name))
                strcat (namebuf, ".");
            else if (kvsdir_issymlink (dir, name))
                strcat (namebuf, "@");
        }
        printf ("%s", namebuf);
        last_len = strlen (namebuf);
        col += col_width;
    }
    if (col > 0)
        printf ("\n");
    kvsitr_destroy (itr);
    free (namebuf);
}

/* List contents of directory pointed to by 'key', descending into subdirs
 * if -R was specified.  First the directory is listed, then its subdirs.
 */
static void list_kvs_dir (flux_t *h, const char *key, optparse_t *p,
                          int win_width, bool print_label, bool print_vspace)
{
    flux_future_t *f;
    kvsdir_t *dir = NULL;
    kvsitr_t *itr;
    const char *name, *json_str;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key))
                || flux_kvs_lookup_get (f, &json_str) < 0) {
        log_err_exit ("%s", key);
        goto done;
    }
    if (!(dir = kvsdir_create (h, NULL, key, json_str)))
        log_err_exit ("kvsdir_create");

    if (print_label)
        printf ("%s%s:\n", print_vspace ? "\n" : "", key);
    list_kvs_dir_single (dir, win_width, p);

    if (optparse_hasopt (p, "recursive")) {
        if (!(itr = kvsitr_create (dir)))
            log_err_exit ("kvsitr_create");
        while ((name = kvsitr_next (itr))) {
            if (kvsdir_isdir (dir, name)) {
                char *nkey;
                if (!(nkey = kvsdir_key_at (dir, name))) {
                    log_err ("%s: kvsdir_key_at failed", name);
                    continue;
                }
                list_kvs_dir (h, nkey, p, win_width, print_label, true);
                free (nkey);
            }
        }
        kvsitr_destroy (itr);
    }
done:
    kvsdir_destroy (dir);
    flux_future_destroy (f);
}

/* List keys, arranging in columns so that they fit within 'win_width',
 * and using a custom column width selected based on the longest entry name.
 * The keys are popped off the list and freed.
 */
static void list_kvs_keys (zlist_t *singles, int win_width)
{
    char *name;
    int col_width = get_list_maxname (singles) + 4;
    int col = 0;
    int last_len = 0;

    while ((name = zlist_pop (singles))) {
        if (need_newline (col, col_width, win_width)) {
            printf ("\n");
            col = 0;
        }
        else if (last_len > 0) // pad out last entry to col_width
            printf ("%*s", col_width - last_len, "");
        printf ("%s", name);
        last_len = strlen (name);
        col += col_width;
        free (name);
    }
    if (col > 0)
        printf ("\n");
}

/* for zlist_sort()
 */
static int sort_cmp (void *item1, void *item2)
{
    if (!item1 && item2)
        return -1;
    if (!item1 && !item2)
        return 0;
    if (item1 && !item2)
        return 1;
    return strcmp (item1, item2);
}

/* Put key in 'dirs' or 'singles' list, depending on whether
 * its contents are to be listed or not.  If -F is specified,
 * 'singles' key names are decorated based on their type.
 */
static int categorize_key (optparse_t *p, const char *key,
                            zlist_t *dirs, zlist_t *singles)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    flux_future_t *f;
    const char *json_str;
    char *nkey;
    json_t *treeobj = NULL;
    bool require_directory = false;

    if (!(nkey = malloc (strlen (key) + 2))) // room for decoration char + null
        log_err_exit ("malloc");
    strcpy (nkey, key);

    /* If the key has a "." suffix, strip it off, but require
     * that the key be a directory type.
     */
    while (strlen (nkey) > 1 && nkey[strlen (nkey) - 1] == '.') {
        nkey[strlen (nkey) - 1] = '\0';
        require_directory = true;
    }
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, nkey)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get (f, &json_str) < 0) {
        fprintf (stderr, "%s: %s\n", nkey, flux_strerror (errno));
        goto error;
    }
    if (!(treeobj = treeobj_decode (json_str)))
        log_err_exit ("%s: metadata decode error", key);
    if (treeobj_is_dir (treeobj) || treeobj_is_dirref (treeobj)) {
        if (optparse_hasopt (p, "directory")) {
            if (optparse_hasopt (p, "classify"))
                strcat (nkey, ".");
            if (zlist_append (singles, nkey) < 0)
                log_err_exit ("zlist_append");
        }
        else
            if (zlist_append (dirs, nkey) < 0)
                log_err_exit ("zlist_append");
    }
    else if (treeobj_is_val (treeobj) || treeobj_is_valref (treeobj)) {
        if (require_directory) {
            fprintf (stderr, "%s: Not a directory\n", nkey);
            goto error;
        }
        if (zlist_append (singles, xstrdup (key)) < 0)
            log_err_exit ("zlist_append");
    }
    else if (treeobj_is_symlink (treeobj)) {
        if (require_directory) {
            fprintf (stderr, "%s: Not a directory\n", nkey);
            goto error;
        }
        if (optparse_hasopt (p, "classify"))
            strcat (nkey, "@");
        if (zlist_append (singles, nkey) < 0)
            log_err_exit ("zlist_append");
    }
    zlist_sort (singles, sort_cmp);
    zlist_sort (dirs, sort_cmp);
    json_decref (treeobj);
    flux_future_destroy (f);
    return 0;
error:
    free (nkey);
    json_decref (treeobj);
    flux_future_destroy (f);
    return -1;
}

/* Mimic ls(1).
 * Strategy: sort keys from command line into two lists: 'dirs' for
 * directories to be expanded, and 'singles' for others.
 * First display the singles, then display directories, optionally recursing.
 * Assume "." if no keys were specified on the command line.
 */
int cmd_ls (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex = optparse_option_index (p);
    int win_width = get_window_width (p, STDOUT_FILENO);
    zlist_t *dirs, *singles;
    char *key;
    bool print_label = false;   // print "name:" before directory contents
    bool print_vspace = false;  // print vertical space before label
    int rc = 0;

    if (!(dirs = zlist_new ()) || !(singles = zlist_new ()))
        log_err_exit ("zlist_new");

    if (optindex == argc) {
        if (categorize_key (p, ".", dirs, singles) < 0)
            rc = -1;
    }
    while (optindex < argc) {
        if (categorize_key (p, argv[optindex++], dirs, singles) < 0)
            rc = -1;
    }
    if (zlist_size (singles) > 0) {
        list_kvs_keys (singles, win_width);
        print_label = true;
        print_vspace = true;
    }

    if (optparse_hasopt (p, "recursive") || zlist_size (dirs) > 1)
        print_label = true;
    while ((key = zlist_pop (dirs))) {
        list_kvs_dir (h, key, p, win_width, print_label, print_vspace);
        print_vspace = true;
        free (key);
    }

    zlist_destroy (&dirs);
    zlist_destroy (&singles);

    return rc;
}

int cmd_copy (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex;
    flux_future_t *f;
    const char *srckey, *dstkey, *json_str;
    flux_kvs_txn_t *txn;

    optindex = optparse_option_index (p);
    if (optindex != (argc - 2))
        log_msg_exit ("copy: specify srckey dstkey");
    srckey = argv[optindex];
    dstkey = argv[optindex + 1];

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, srckey))
            || flux_kvs_lookup_get (f, &json_str) < 0)
        log_err_exit ("flux_kvs_lookup %s", srckey);
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, FLUX_KVS_TREEOBJ, dstkey, json_str) < 0)
        log_err_exit( "flux_kvs_txn_put");
    flux_future_destroy (f);

    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return (0);
}

int cmd_move (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;
    flux_future_t *f;
    const char *srckey, *dstkey, *json_str;
    flux_kvs_txn_t *txn;

    h = (flux_t *)optparse_get_data (p, "flux_handle");

    optindex = optparse_option_index (p);
    if (optindex != (argc - 2))
        log_msg_exit ("move: specify srckey dstkey");
    srckey = argv[optindex];
    dstkey = argv[optindex + 1];

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, srckey))
            || flux_kvs_lookup_get (f, &json_str) < 0)
        log_err_exit ("flux_kvs_lookup %s", srckey);
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, FLUX_KVS_TREEOBJ, dstkey, json_str) < 0)
        log_err_exit( "flux_kvs_txn_put");
    if (flux_kvs_txn_unlink (txn, 0, srckey) < 0)
        log_err_exit( "flux_kvs_txn_unlink");
    flux_future_destroy (f);

    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
