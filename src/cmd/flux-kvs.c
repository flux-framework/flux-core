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
#include <jansson.h>
#include <czmq.h>
#include <argz.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libkvs/treeobj.h"

int cmd_namespace_create (optparse_t *p, int argc, char **argv);
int cmd_namespace_remove (optparse_t *p, int argc, char **argv);
int cmd_namespace_list (optparse_t *p, int argc, char **argv);
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
int cmd_getroot (optparse_t *p, int argc, char **argv);
int cmd_eventlog (optparse_t *p, int argc, char **argv);

static int get_window_width (optparse_t *p, int fd);
static void dump_kvs_dir (const flux_kvsdir_t *dir, int maxcol,
                          bool Ropt, bool dopt);

#define min(a,b) ((a)<(b)?(a):(b))

static struct optparse_option global_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option namespace_create_opts[] =  {
    { .name = "owner", .key = 'o', .has_arg = 1,
      .usage = "Specify alternate namespace owner via userid",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option readlink_opts[] =  {
    { .name = "at", .key = 'a', .has_arg = 1,
      .usage = "Lookup relative to RFC 11 snapshot reference",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option get_opts[] =  {
    { .name = "json", .key = 'j', .has_arg = 0,
      .usage = "Interpret value(s) as encoded JSON",
    },
    { .name = "raw", .key = 'r', .has_arg = 0,
      .usage = "Interpret value(s) as raw data",
    },
    { .name = "treeobj", .key = 't', .has_arg = 0,
      .usage = "Show RFC 11 object",
    },
    { .name = "at", .key = 'a', .has_arg = 1,
      .usage = "Lookup relative to RFC 11 snapshot reference",
    },
    { .name = "label", .key = 'l', .has_arg = 0,
      .usage = "Print key= before value",
    },
    { .name = "watch", .key = 'w', .has_arg = 0,
      .usage = "Monitor key changes",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "Display at most COUNT changes",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option put_opts[] =  {
    { .name = "json", .key = 'j', .has_arg = 0,
      .usage = "Store value(s) as encoded JSON",
    },
    { .name = "raw", .key = 'r', .has_arg = 0,
      .usage = "Store value(s) as-is without adding NULL termination",
    },
    { .name = "treeobj", .key = 't', .has_arg = 0,
      .usage = "Store RFC 11 object",
    },
    { .name = "no-merge", .key = 'n', .has_arg = 0,
      .usage = "Set the NO_MERGE flag to ensure commit is standalone",
    },
    { .name = "append", .key = 'A', .has_arg = 0,
      .usage = "Append value(s) to key instead of overwriting",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option dir_opts[] =  {
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Recursively display keys under subdirectories",
    },
    { .name = "directory", .key = 'd', .has_arg = 0,
      .usage = "List directory entries and not values",
    },
    { .name = "width", .key = 'w', .has_arg = 1,
      .usage = "Set output width to COLS.  0 means no limit",
    },
    { .name = "at", .key = 'a', .has_arg = 1,
      .usage = "Lookup relative to RFC 11 snapshot reference",
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

static struct optparse_option getroot_opts[] =  {
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Show root as a blobref rather that an RFC 11 dirref",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Show sequence number",
    },
    { .name = "owner", .key = 'o', .has_arg = 0,
      .usage = "Show owner",
    },
    { .name = "watch", .key = 'w', .has_arg = 0,
      .usage = "Monitor root changes",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "Display at most COUNT changes",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "namespace-create",
      "name [name...]",
      "Create a KVS namespace",
      cmd_namespace_create,
      0,
      namespace_create_opts
    },
    { "namespace-remove",
      "name [name...]",
      "Remove a KVS namespace",
      cmd_namespace_remove,
      0,
      NULL
    },
    { "namespace-list",
      "",
      "List namespaces on local rank",
      cmd_namespace_list,
      0,
      NULL
    },
    { "get",
      "[-j|-r|-t] [-a treeobj] [-l] [-w] [-c COUNT] key [key...]",
      "Get value stored under key",
      cmd_get,
      0,
      get_opts
    },
    { "put",
      "[-j|-r|-t] [-n] key=value [key=value...]",
      "Store value under key",
      cmd_put,
      0,
      put_opts
    },
    { "dir",
      "[-R] [-d] [-w COLS] [-a treeobj] [key]",
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
      "[-a treeobj] key [key...]",
      "Retrieve the key a link refers to",
      cmd_readlink,
      0,
      readlink_opts
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
    { "getroot",
      "[-w] [-c COUNT] [-s|-o|-b]",
      "Get KVS root treeobj",
      cmd_getroot,
      0,
      getroot_opts
    },
    { "eventlog",
      NULL,
      "Manipulate a KVS eventlog",
      cmd_eventlog,
      0,
      NULL,
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
    const char *namespace;

    log_init ("flux-kvs");

    p = optparse_create ("flux-kvs");

    if (optparse_add_option_table (p, global_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

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
        || !optparse_get_subcommand (p, argv[optindex])) {
        usage (p, NULL, NULL);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if ((namespace = optparse_get_str (p, "namespace", NULL))) {
        if (flux_kvs_set_namespace (h, namespace) < 0)
            log_msg_exit ("flux_kvs_set_namespace");
    }

    optparse_set_data (p, "flux_handle", h);

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    flux_close (h);
    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

int cmd_namespace_create (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    flux_future_t *f;
    int optindex, i;
    uint32_t owner = FLUX_USERID_UNKNOWN;
    const char *str;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if ((str = optparse_get_str (p, "owner", NULL))) {
        char *endptr;
        owner = strtoul (str, &endptr, 10);
        if (*endptr != '\0')
            log_err_exit ("--owner requires an unsigned integer argument");
    }

    for (i = optindex; i < argc; i++) {
        const char *name = argv[i];
        int flags = 0;
        if (!(f = flux_kvs_namespace_create (h, name, owner, flags))
            || flux_future_get (f, NULL) < 0)
            log_err_exit ("%s", name);
        flux_future_destroy (f);
    }
    return (0);
}

int cmd_namespace_remove (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    flux_future_t *f;
    int optindex, i;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    for (i = optindex; i < argc; i++) {
        const char *name = argv[i];
        if (!(f = flux_kvs_namespace_remove (h, name))
            || flux_future_get (f, NULL) < 0)
            log_err_exit ("%s", name);
        flux_future_destroy (f);
    }
    return (0);
}

int cmd_namespace_list (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    flux_kvs_namespace_itr_t *itr;
    const char *namespace;
    uint32_t owner;
    int optindex, flags;

    optindex = optparse_option_index (p);
    if ((optindex - argc) != 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(itr = flux_kvs_namespace_list (h)))
        log_err_exit ("flux_kvs_namespace_list");
    printf ("NAMESPACE                                 OWNER      FLAGS\n");
    while ((namespace = flux_kvs_namespace_itr_next (itr, &owner, &flags))) {
        printf ("%-36s %10u 0x%08X\n", namespace, owner, flags);
    }
    flux_kvs_namespace_itr_destroy (itr);
    return (0);
}

static void kv_printf (const char *key, int maxcol, const char *fmt, ...)
{
    va_list ap;
    int rc;
    char *val, *kv, *p;
    bool overflow = false;

    if (maxcol != 0 && maxcol <= 3)
        maxcol = 0;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);

    if (rc < 0)
        log_err_exit ("%s", __FUNCTION__);

    if (asprintf (&kv, "%s%s%s", key ? key : "",
                                 key ? " = " : "",
                                 val) < 0)
        log_err_exit ("%s", __FUNCTION__);

    /* There will be no truncation of output if maxcol = 0.
     * If truncation is enabled, ensure that at most maxcol columns are used.
     * Truncate earlier if value contains a convenient newline break.
     * Finally, truncate on first non-printable character.
     */
    if (maxcol != 0) {
        if (strlen (kv) > maxcol - 3) {
            kv[maxcol - 3] = '\0';
            overflow = true;
        }
        if ((p = strchr (kv, '\n'))) {
            *p = '\0';
            overflow = true;
        }
        for (p = kv; *p != '\0'; p++) {
            if (!isprint (*p)) {
                *p = '\0';
                overflow = true;
                break;
            }
        }
    }
    printf ("%s%s\n", kv,
                      overflow ? "..." : "");

    free (val);
    free (kv);
}

static void output_key_json_object (const char *key, json_t *o, int maxcol)
{
    char *s;

    switch (json_typeof (o)) {
    case JSON_NULL:
        kv_printf (key, maxcol, "nil");
        break;
    case JSON_TRUE:
        kv_printf (key, maxcol, "true");
        break;
    case JSON_FALSE:
        kv_printf (key, maxcol, "false");
        break;
    case JSON_REAL:
        kv_printf (key, maxcol, "%f", json_real_value (o));
        break;
    case JSON_INTEGER:
        kv_printf (key, maxcol, "%lld", (long long)json_integer_value (o));
        break;
    case JSON_STRING:
        kv_printf (key, maxcol, "%s", json_string_value (o));
        break;
    case JSON_ARRAY:
    case JSON_OBJECT:
    default:
        if (!(s = json_dumps (o, JSON_SORT_KEYS)))
            log_msg_exit ("json_dumps failed");
        kv_printf (key, maxcol, "%s", s);
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
    output_key_json_object (key, o, 0);
    json_decref (o);
}

struct lookup_ctx {
    optparse_t *p;
    int maxcount;
    int count;
};


void lookup_continuation (flux_future_t *f, void *arg)
{
    struct lookup_ctx *ctx = arg;
    const char *key = flux_kvs_lookup_get_key (f);

    if (optparse_hasopt (ctx->p, "watch") && flux_rpc_get (f, NULL) < 0
                                          && errno == ENODATA) {
        flux_future_destroy (f);
        return; // EOF
    }

    if (optparse_hasopt (ctx->p, "treeobj")) {
        const char *treeobj;
        if (flux_kvs_lookup_get_treeobj (f, &treeobj) < 0)
            log_err_exit ("%s", key);
        if (optparse_hasopt (ctx->p, "label"))
            printf ("%s=", key);
        printf ("%s\n", treeobj);
    }
    else if (optparse_hasopt (ctx->p, "json")) {
        const char *json_str;
        if (flux_kvs_lookup_get (f, &json_str) < 0)
            log_err_exit ("%s", key);
        if (!json_str)
            log_msg_exit ("%s: zero-length value", key);
        if (optparse_hasopt (ctx->p, "label"))
            printf ("%s=", key);
        output_key_json_str (NULL, json_str, key);
    }
    else if (optparse_hasopt (ctx->p, "raw")) {
        const void *data;
        int len;
        if (flux_kvs_lookup_get_raw (f, &data, &len) < 0)
            log_err_exit ("%s", key);
        if (optparse_hasopt (ctx->p, "label"))
            printf ("%s=", key);
        if (write_all (STDOUT_FILENO, data, len) < 0)
            log_err_exit ("%s", key);
    }
    else {
        const char *value;
        if (flux_kvs_lookup_get (f, &value) < 0)
            log_err_exit ("%s", key);
        if (optparse_hasopt (ctx->p, "label"))
            printf ("%s=", key);
        if (value)
            printf ("%s\n", value);
    }
    fflush (stdout);
    if (optparse_hasopt (ctx->p, "watch")) {
        flux_future_reset (f);
        if (ctx->maxcount > 0 && ++ctx->count == ctx->maxcount) {
            if (flux_kvs_lookup_cancel (f) < 0)
                log_err_exit ("flux_kvs_lookup_cancel");
        }
    }
    else
        flux_future_destroy (f);
}

void cmd_get_one (flux_t *h, const char *key, struct lookup_ctx *ctx)
{
    flux_future_t *f;
    int flags = 0;

    if (optparse_hasopt (ctx->p, "treeobj"))
        flags |= FLUX_KVS_TREEOBJ;
    if (optparse_hasopt (ctx->p, "watch"))
        flags |= FLUX_KVS_WATCH;
    if (optparse_hasopt (ctx->p, "at")) {
        const char *reference = optparse_get_str (ctx->p, "at", "");
        if (!(f = flux_kvs_lookupat (h, flags, key, reference)))
            log_err_exit ("%s", key);
    }
    else {
        if (!(f = flux_kvs_lookup (h, flags, key)))
            log_err_exit ("%s", key);
    }
    if (flux_future_then (f, -1., lookup_continuation, ctx) < 0)
        log_err_exit ("flux_future_then");
    if (!optparse_hasopt (ctx->p, "watch")) {
        if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
            log_err_exit ("flux_reactor_run");
    }
}

int cmd_get (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    struct lookup_ctx ctx;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    ctx.p = p;
    ctx.count = 0;
    ctx.maxcount = optparse_get_int (p, "count", 0);
    for (i = optindex; i < argc; i++)
        cmd_get_one (h, argv[i], &ctx);
    /* Unless --watch is specified, cmd_get_one() starts the reactor and
     * waits for it to complete, effectively making each lookup synchronous,
     * so that value output order matches command line order of keys.
     * Otherwise, make all the lookups before running the reactor.
     */
    if (optparse_hasopt (p, "watch")) {
        if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
            log_err_exit ("flux_reactor_run");
    }

    return (0);
}

int cmd_put (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int optindex, i;
    flux_future_t *f;
    flux_kvs_txn_t *txn;
    int commit_flags = 0;
    int put_flags = 0;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "no-merge"))
        commit_flags |= FLUX_KVS_NO_MERGE;
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');
        if (!val)
            log_msg_exit ("put: you must specify a value as key=value");
        *val++ = '\0';

        if (optparse_hasopt (p, "treeobj")) {
            int len;
            uint8_t *buf = NULL;

            if (!strcmp (val, "-")) { // special handling for "--treeobj key=-"
                if ((len = read_all (STDIN_FILENO, (void **)&buf)) < 0)
                    log_err_exit ("stdin");
                val = (char *)buf;
            }
            if (flux_kvs_txn_put_treeobj (txn, 0, key, val) < 0)
                log_err_exit ("%s", key);
            free (buf);
        }
        else if (optparse_hasopt (p, "json")) {
            json_t *obj;
            if ((obj = json_loads (val, JSON_DECODE_ANY, NULL))) {
                if (flux_kvs_txn_put (txn, 0, key, val) < 0)
                    log_err_exit ("%s", key);
                json_decref (obj);
            }
            else { // encode as JSON string if not already valid encoded JSON
                if (flux_kvs_txn_pack (txn, 0, key, "s", val) < 0)
                    log_err_exit ("%s", key);
            }
        }
        else if (optparse_hasopt (p, "raw")) {
            int len;
            uint8_t *buf = NULL;

            if (optparse_hasopt (p, "append"))
                put_flags |= FLUX_KVS_APPEND;

            if (!strcmp (val, "-")) { // special handling for "--raw key=-"
                if ((len = read_all (STDIN_FILENO, (void **)&buf)) < 0)
                    log_err_exit ("stdin");
                val = (char *)buf;
            } else
                len = strlen (val);
            if (flux_kvs_txn_put_raw (txn, put_flags, key, val, len) < 0)
                log_err_exit ("%s", key);
            free (buf);
        }
        else {
            if (optparse_hasopt (p, "append"))
                put_flags |= FLUX_KVS_APPEND;

            if (flux_kvs_txn_put (txn, put_flags, key, val) < 0)
                log_err_exit ("%s", key);
        }
        free (key);
    }
    if (!(f = flux_kvs_commit (h, commit_flags, txn))
                                        || flux_future_get (f, NULL) < 0)
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
    const flux_kvsdir_t *dir = NULL;
    int rc = -1;

    *unlinkable = false;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key)))
        goto done;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && fopt)
            goto out;
        if (errno != ENOTDIR)
            goto done;
    }
    else if (!Ropt) {
        if (flux_kvsdir_get_size (dir) > 0) {
            errno = ENOTEMPTY;
            goto done;
        }
    }
    *unlinkable = true;
out:
    rc = 0;
done:
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
        if (optparse_hasopt (p, "at")) {
            const char *ref = optparse_get_str (p, "at", "");
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, argv[i], ref)))
                log_err_exit ("%s", argv[i]);
        }
        else {
            if (!(f = flux_kvs_lookup (h, FLUX_KVS_READLINK, argv[i])))
                log_err_exit ("%s", argv[i]);
        }
        if (flux_kvs_lookup_get_symlink (f, &target) < 0)
            log_err_exit ("%s", argv[i]);
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

    if (flux_kvs_get_version (h, &vers) < 0)
        log_err_exit ("flux_kvs_get_version");
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
    if (flux_kvs_wait_version (h, vers) < 0)
        log_err_exit ("flux_kvs_get_version");
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

static void watch_dump_kvsdir (flux_kvsdir_t *dir, bool Ropt, bool dopt,
                               const char *arg) {
    if (!dir) {
        output_key_json_str (NULL, NULL, arg);
        printf ("%s\n", WATCH_DIR_SEPARATOR);
        return;
    }

    dump_kvs_dir (dir, 0, Ropt, dopt);
    printf ("%s\n", WATCH_DIR_SEPARATOR);
    fflush (stdout);
}

int cmd_watch (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_kvsdir_t *dir = NULL;
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

    rc = flux_kvs_get (h, key, &json_str);
    if (rc < 0 && (errno != ENOENT && errno != EISDIR))
        log_err_exit ("%s", key);

    /* key is a directory, setup for dir logic appropriately */
    if (rc < 0 && errno == EISDIR) {
        rc = flux_kvs_get_dir (h, &dir, "%s", key);
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
            rc = flux_kvs_watch_once_dir (h, &dir, "%s", key);
            if (rc < 0 && (errno != ENOENT && errno != ENOTDIR)) {
                printf ("%s: %s\n", key, flux_strerror (errno));
                if (dir)
                    flux_kvsdir_destroy (dir);
                dir = NULL;
            }
            else if (rc < 0 && errno == ENOENT) {
                if (dir)
                    flux_kvsdir_destroy (dir);
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
                    flux_kvsdir_destroy (dir);
                dir = NULL;

                rc = flux_kvs_get (h, key, &json_str);
                if (rc < 0 && errno != ENOENT)
                    printf ("%s: %s\n", key, flux_strerror (errno));
                else
                    watch_dump_key (json_str, key, &prev_output_iskey);
            }
        }
        else {
            rc = flux_kvs_watch_once (h, key, &json_str);
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

                rc = flux_kvs_get_dir (h, &dir, "%s", key);
                if (rc < 0 && errno != ENOENT)
                    printf ("%s: %s\n", key, flux_strerror (errno));
                else /* rc == 0 || (rc < 0 && errno == ENOENT) */
                    watch_dump_kvsdir (dir, Ropt, dopt, key);
            }
        }
        count--;
    }
    if (dir)
        flux_kvsdir_destroy (dir);
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
        if (flux_kvs_dropcache (h) < 0)
            log_err_exit ("flux_kvs_dropcache");
    }
    return (0);
}

static char *process_key (const char *key, char **key_suffix)
{
    char *nkey;
    char *ptr;

    if (!(nkey = malloc (strlen (key) + 2))) // room for decoration char + null
        log_err_exit ("malloc");
    strcpy (nkey, key);

    if (!strncmp (nkey, "ns:", 3)
        && (ptr = strchr (nkey + 3, '/'))) {
        /* No key suffix, decorate with default '.' */
        if (*(ptr + 1) == '\0')
            strcat (nkey, ".");
        if (key_suffix)
            (*key_suffix) = ptr + 1;
    }

    return nkey;
}

static void dump_kvs_val (const char *key, int maxcol, const char *value)
{
    json_t *o;

    if (!value) {
        kv_printf (key, maxcol, "");
    }
    else if ((o = json_loads (value, JSON_DECODE_ANY, NULL))) {
        output_key_json_object (key, o, maxcol);
        json_decref (o);
    }
    else {
        kv_printf (key, maxcol, value);
    }
}

static void dump_kvs_dir (const flux_kvsdir_t *dir, int maxcol,
                          bool Ropt, bool dopt)
{
    const char *rootref = flux_kvsdir_rootref (dir);
    flux_t *h = flux_kvsdir_handle (dir);
    flux_future_t *f;
    flux_kvsitr_t *itr;
    const char *name;
    char *key;

    itr = flux_kvsitr_create (dir);
    while ((name = flux_kvsitr_next (itr))) {
        key = flux_kvsdir_key_at (dir, name);
        if (flux_kvsdir_issymlink (dir, name)) {
            const char *link;
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key, rootref))
                    || flux_kvs_lookup_get_symlink (f, &link) < 0)
                log_err_exit ("%s", key);
            printf ("%s -> %s\n", key, link);
            flux_future_destroy (f);

        } else if (flux_kvsdir_isdir (dir, name)) {
            if (Ropt) {
                const flux_kvsdir_t *ndir;
                if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, rootref))
                        || flux_kvs_lookup_get_dir (f, &ndir) < 0)
                    log_err_exit ("%s", key);
                if (flux_kvsdir_get_size (ndir) == 0)
                    printf ("%s.\n", key);
                else
                    dump_kvs_dir (ndir, maxcol, Ropt, dopt);
                flux_future_destroy (f);
            } else
                printf ("%s.\n", key);
        } else {
            if (!dopt) {
                const char *value;
                const void *buf;
                int len;
                if (!(f = flux_kvs_lookupat (h, 0, key, rootref)))
                    log_err_exit ("%s", key);
                if (flux_kvs_lookup_get (f, &value) == 0) // null terminated
                    dump_kvs_val (key, maxcol, value);
                else if (flux_kvs_lookup_get_raw  (f, &buf, &len) == 0)
                    kv_printf (key, maxcol, "%.*s", len, buf);
                else
                    log_err_exit ("%s", key);
                flux_future_destroy (f);
            }
            else
                printf ("%s\n", key);
        }
        free (key);
    }
    flux_kvsitr_destroy (itr);
}

int cmd_dir (optparse_t *p, int argc, char **argv)
{
    flux_t *h = (flux_t *)optparse_get_data (p, "flux_handle");
    int maxcol = get_window_width (p, STDOUT_FILENO);
    bool Ropt;
    bool dopt;
    char *key;
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    int optindex;

    optindex = optparse_option_index (p);
    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");
    if (optindex == argc)
        key = process_key (".", NULL);
    else if (optindex == (argc - 1))
        key = process_key (argv[optindex], NULL);
    else
        log_msg_exit ("dir: specify zero or one directory");

    if (optparse_hasopt (p, "at")) {
        const char *reference = optparse_get_str (p, "at", "");
        if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, reference)))
            log_err_exit ("%s", key);
    }
    else {
        if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key)))
            log_err_exit ("%s", key);
    }
    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        log_err_exit ("%s", key);
    dump_kvs_dir (dir, maxcol, Ropt, dopt);
    flux_future_destroy (f);
    free (key);
    return (0);
}

/* Find longest name in a directory.
 */
static int get_dir_maxname (const flux_kvsdir_t *dir)
{
    flux_kvsitr_t *itr;
    const char *name;
    int max = 0;

    if (!(itr = flux_kvsitr_create (dir)))
        log_err_exit ("flux_kvsitr_create");
    while ((name = flux_kvsitr_next (itr)))
        if (max < strlen (name))
            max = strlen (name);
    flux_kvsitr_destroy (itr);
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
static void list_kvs_dir_single (const flux_kvsdir_t *dir, int win_width,
                                 optparse_t *p)
{
    flux_kvsitr_t *itr;
    const char *name;
    int col_width = get_dir_maxname (dir) + 4;
    int col = 0;
    char *namebuf;
    int last_len = 0;

    if (!(namebuf = malloc (col_width + 2)))
        log_err_exit ("malloc");
    if (!(itr = flux_kvsitr_create (dir)))
        log_err_exit ("flux_kvsitr_create");
    while ((name = flux_kvsitr_next (itr))) {
        if (need_newline (col, col_width, win_width)) {
            printf ("\n");
            col = 0;
        }
        else if (last_len > 0) // pad out last entry to col_width
            printf ("%*s", col_width - last_len, "");
        strcpy (namebuf, name);
        if (optparse_hasopt (p, "classify")) {
            if (flux_kvsdir_isdir (dir, name))
                strcat (namebuf, ".");
            else if (flux_kvsdir_issymlink (dir, name))
                strcat (namebuf, "@");
        }
        printf ("%s", namebuf);
        last_len = strlen (namebuf);
        col += col_width;
    }
    if (col > 0)
        printf ("\n");
    flux_kvsitr_destroy (itr);
    free (namebuf);
}

/* List contents of directory pointed to by 'key', descending into subdirs
 * if -R was specified.  First the directory is listed, then its subdirs.
 */
static void list_kvs_dir (flux_t *h, const char *key, optparse_t *p,
                          int win_width, bool print_label, bool print_vspace)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key))
                || flux_kvs_lookup_get_dir (f, &dir) < 0) {
        log_err_exit ("%s", key);
        goto done;
    }
    if (print_label)
        printf ("%s%s:\n", print_vspace ? "\n" : "", key);
    list_kvs_dir_single (dir, win_width, p);

    if (optparse_hasopt (p, "recursive")) {
        if (!(itr = flux_kvsitr_create (dir)))
            log_err_exit ("flux_kvsitr_create");
        while ((name = flux_kvsitr_next (itr))) {
            if (flux_kvsdir_isdir (dir, name)) {
                char *nkey;
                if (!(nkey = flux_kvsdir_key_at (dir, name))) {
                    log_err ("%s: flux_kvsdir_key_at failed", name);
                    continue;
                }
                list_kvs_dir (h, nkey, p, win_width, print_label, true);
                free (nkey);
            }
        }
        flux_kvsitr_destroy (itr);
    }
done:
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
    char *key_ptr = NULL;

    nkey = process_key (key, &key_ptr);

    if (!key_ptr)
        key_ptr = nkey;

    /* If the key has a "." suffix, strip it off, but require
     * that the key be a directory type.
     */
    while (strlen (key_ptr) > 1 && key_ptr[strlen (key_ptr) - 1] == '.') {
        key_ptr[strlen (key_ptr) - 1] = '\0';
        require_directory = true;
    }
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, nkey)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get_treeobj (f, &json_str) < 0) {
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

    if (optparse_hasopt (p, "1"))
        win_width = 1;

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
            || flux_kvs_lookup_get_treeobj (f, &json_str) < 0)
        log_err_exit ("flux_kvs_lookup %s", srckey);
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put_treeobj (txn, 0, dstkey, json_str) < 0)
        log_err_exit( "flux_kvs_txn_put_treeobj");
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
            || flux_kvs_lookup_get_treeobj (f, &json_str) < 0)
        log_err_exit ("flux_kvs_lookup %s", srckey);
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put_treeobj (txn, 0, dstkey, json_str) < 0)
        log_err_exit( "flux_kvs_txn_put_treeobj");
    if (flux_kvs_txn_unlink (txn, 0, srckey) < 0)
        log_err_exit( "flux_kvs_txn_unlink");
    flux_future_destroy (f);

    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return (0);
}

struct getroot_ctx {
    optparse_t *p;
    int count;
    int maxcount;
};

void getroot_continuation (flux_future_t *f, void *arg)
{
    struct getroot_ctx *ctx = arg;

    if (optparse_hasopt (ctx->p, "watch") && flux_rpc_get (f, NULL) < 0
                                          && errno == ENODATA) {
        flux_future_destroy (f);
        return; // EOF
    }
    if (ctx->maxcount == 0 || ctx->count < ctx->maxcount) {
        if (optparse_hasopt (ctx->p, "owner")) {
            uint32_t owner;

            if (flux_kvs_getroot_get_owner (f, &owner) < 0)
                log_err_exit ("flux_kvs_getroot_get_owner");
            printf ("%lu\n", (unsigned long)owner);
        }
        else if (optparse_hasopt (ctx->p, "sequence")) {
            int sequence;

            if (flux_kvs_getroot_get_sequence (f, &sequence) < 0)
                log_err_exit ("flux_kvs_getroot_get_sequence");
            printf ("%d\n", sequence);
        }
        else if (optparse_hasopt (ctx->p, "blobref")) {
            const char *blobref;

            if (flux_kvs_getroot_get_blobref (f, &blobref) < 0)
                log_err_exit ("flux_kvs_getroot_get_blobref");
            printf ("%s\n", blobref);
        }
        else {
            const char *treeobj;

            if (flux_kvs_getroot_get_treeobj (f, &treeobj) < 0)
                log_err_exit ("flux_kvs_getroot_get_treeobj");
            printf ("%s\n", treeobj);
        }
    }
    fflush (stdout);
    if (optparse_hasopt (ctx->p, "watch")) {
        flux_future_reset (f);
        if (ctx->maxcount > 0 && ++ctx->count == ctx->maxcount) {
            if (flux_kvs_getroot_cancel (f) < 0)
                log_err_exit ("flux_kvs_getroot_cancel");
        }
    }
    else
        flux_future_destroy (f);
}

int cmd_getroot (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "flux_handle");
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    int flags = 0;
    struct getroot_ctx ctx;

    ctx.p = p;
    ctx.count = 0;
    ctx.maxcount = optparse_get_int (p, "count", 0);
    if (ctx.maxcount < 0)
        log_msg_exit ("count value must be >= 0");
    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "watch"))
        flags |= FLUX_KVS_WATCH;
    if (!(f = flux_kvs_getroot (h, NULL, flags)))
        log_err_exit ("flux_kvs_getroot");
    if (flux_future_then (f, -1., getroot_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");
    return (0);
}

/* combine 'argv' elements into one space-separated string (caller must free).
 * assumes 'argv' is NULL terminated.
 */
static char *eventlog_context_from_args (char **argv)
{
    size_t context_len;
    char *context;
    error_t e;

    if ((e = argz_create (argv, &context, &context_len)) != 0)
        log_errn_exit (e, "argz_create");
    argz_stringify (context, context_len, ' ');
    return context;
}

/* Encode event from broken out args.
 * If timestamp < 0, use wall clock.
 * Enclose event in a KVS transaction and send the commit request.
 */
static flux_future_t *eventlog_append_event (flux_t *h, const char *key,
                                             double timestamp,
                                             const char *name,
                                             const char *context)
{
    flux_kvs_txn_t *txn;
    char *event;
    flux_future_t *f;

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (timestamp < 0.)
        event = flux_kvs_event_encode (name, context);
    else
        event = flux_kvs_event_encode_timestamp  (timestamp, name, context);
    if (!event)
        log_err_exit ("flux_kvs_event_encode");
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, event) < 0)
        log_err_exit ("flux_kvs_txn_put");
    if (!(f = flux_kvs_commit (h, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    return f;
}

int cmd_eventlog_append (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "flux_handle");
    int optindex = optparse_option_index (p);
    const char *key;
    const char *name;
    char *context = NULL;
    flux_future_t *f;
    double timestamp = optparse_get_double (p, "timestamp", -1.);

    if (argc - optindex < 2) {
        optparse_print_usage (p);
        exit (1);
    }
    key = argv[optindex++];
    name = argv[optindex++];
    if (optindex < argc)
        context = eventlog_context_from_args (argv + optindex);

    f = eventlog_append_event (h, key, timestamp, name, context);
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");

    flux_future_destroy (f);
    free (context);

    return (0);
}

struct eventlog_get_ctx {
    struct flux_kvs_eventlog *log;
    optparse_t *p;
    int maxcount;
    int count;
};

/* convert floating point timestamp (UNIX epoch, UTC) to ISO 8601 string,
 * with microsecond precision
 */
static int eventlog_timestr (double timestamp, char *buf, size_t size)
{
    time_t sec = timestamp;
    unsigned long usec = (timestamp - sec)*1E6;
    struct tm tm;

    if (!gmtime_r (&sec, &tm))
        return -1;
    if (strftime (buf, size, "%FT%T", &tm) == 0)
        return -1;
    size -= strlen (buf);
    buf += strlen (buf);
    if (snprintf (buf, size, ".%.6luZ", usec) >= size)
        return -1;
    return 0;
}

/* print event with human-readable time
 */
static void eventlog_prettyprint (FILE *f, const char *s)
{
    double timestamp;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    char buf[64];

    if (flux_kvs_event_decode (s, &timestamp, name, sizeof (name),
                                              context, sizeof (context)) < 0)
        log_err_exit ("flux_kvs_event_decode");
    if (eventlog_timestr (timestamp, buf, sizeof (buf)) < 0)
        log_msg_exit ("error converting timestamp to ISO 8601");

    fprintf (f, "%s %s%s%s\n", buf, name, *context ? " " : "", context);
}

void eventlog_get_continuation (flux_future_t *f, void *arg)
{
    struct eventlog_get_ctx *ctx = arg;
    const char *s;
    const char *event;
    bool limit_reached = false;

    /* Handle cancelled lookup (FLUX_KVS_WATCH flag only).
     * Destroy the future and return (reactor will then terminate).
     * Errors other than ENODATA are handled by the flux_kvs_lookup_get().
     */
    if (optparse_hasopt (ctx->p, "watch") && flux_rpc_get (f, NULL) < 0
                                          && errno == ENODATA) {
        flux_future_destroy (f);
        return;
    }

    /* Each KVS get response returns a new snapshot of the eventlog.
     * Pass it to eventlog_update() which ensures the shapshot is consistent
     * with any existing events in the log, and makes new events available to
     * the iterator.
     */
    if (flux_kvs_lookup_get (f, &s) < 0)
        log_err_exit ("flux_kvs_lookup_get");
    if (flux_kvs_eventlog_update (ctx->log, s) < 0)
        log_err_exit ("flux_kvs_eventlog_update");

    /* Display any new events.
     */
    while ((event = flux_kvs_eventlog_next (ctx->log)) && !limit_reached) {
        if (optparse_hasopt (ctx->p, "unformatted"))
            printf ("%s", event);
        else
            eventlog_prettyprint (stdout, event);
        if (ctx->maxcount > 0 && ++ctx->count == ctx->maxcount)
            limit_reached = true;
    }
    fflush (stdout);

    /* If watching, re-arm future for next response.
     * If --count limit has been reached, send a cancel request.
     * The resulting ENODATA error response is handled above.
     */
    if (optparse_hasopt (ctx->p, "watch")) {
        flux_future_reset (f);
        if (limit_reached) {
            if (flux_kvs_lookup_cancel (f) < 0)
                log_err_exit ("flux_kvs_lookup_cancel");
        }
    }
    else
        flux_future_destroy (f);
}

int cmd_eventlog_get (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "flux_handle");
    int optindex = optparse_option_index (p);
    const char *key;
    flux_future_t *f;
    int flags = 0;
    struct eventlog_get_ctx ctx;

    if (argc - optindex != 1) {
        optparse_print_usage (p);
        exit (1);
    }
    key = argv[optindex++];
    if (optparse_hasopt (p, "watch"))
        flags |= FLUX_KVS_WATCH;

    if (!(ctx.log = flux_kvs_eventlog_create()))
        log_err_exit ("flux_kvs_eventlog_create");
    ctx.p = p;
    ctx.count = 0;
    ctx.maxcount = optparse_get_int (p, "count", 0);

    if (!(f = flux_kvs_lookup (h, flags, key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_future_then (f, -1., eventlog_get_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_kvs_eventlog_destroy (ctx.log);

    return (0);
}

static struct optparse_option eventlog_append_opts[] =  {
    { .name = "timestamp", .key = 't', .has_arg = 1, .arginfo = "SECONDS",
      .usage = "Specify timestamp in seconds since epoch",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option eventlog_get_opts[] =  {
    { .name = "watch", .key = 'w', .has_arg = 0,
      .usage = "Monitor eventlog",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "Display at most COUNT events",
    },
    { .name = "unformatted", .key = 'u', .has_arg = 0,
      .usage = "Show event in RFC 18 form",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand eventlog_subcommands[] = {
    { "append",
      "[-t SECONDS] key name [context ...]",
      "Append to eventlog",
      cmd_eventlog_append,
      0,
      eventlog_append_opts,
    },
    { "get",
      "[-u] [-w] [-c COUNT] key",
      "Get eventlog",
      cmd_eventlog_get,
      0,
      eventlog_get_opts,
    },
    OPTPARSE_SUBCMD_END
};

int cmd_eventlog (optparse_t *p, int argc, char **argv)
{
    int optindex;

    if (optparse_reg_subcommands (p, eventlog_subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("eventlog: optparse_reg_subcommands failed");

    optindex = optparse_parse_args (p, argc, argv);
    if (optindex < 0)
        log_msg_exit ("eventlog: optparse_parse_args failed");

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        log_msg_exit ("eventlog: optparse_run_subcommand failed");

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
