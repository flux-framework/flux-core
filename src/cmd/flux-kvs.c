/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/optparse.h>
#include <unistd.h>
#include <fcntl.h>
#include <jansson.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <ctype.h>
#include <sys/ioctl.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/formatter.h"
#include "ccan/str/str.h"

int cmd_namespace (optparse_t *p, int argc, char **argv);
int cmd_get (optparse_t *p, int argc, char **argv);
int cmd_put (optparse_t *p, int argc, char **argv);
int cmd_unlink (optparse_t *p, int argc, char **argv);
int cmd_link (optparse_t *p, int argc, char **argv);
int cmd_readlink (optparse_t *p, int argc, char **argv);
int cmd_mkdir (optparse_t *p, int argc, char **argv);
int cmd_version (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);
int cmd_dropcache (optparse_t *p, int argc, char **argv);
int cmd_copy (optparse_t *p, int argc, char **argv);
int cmd_move (optparse_t *p, int argc, char **argv);
int cmd_dir (optparse_t *p, int argc, char **argv);
int cmd_ls (optparse_t *p, int argc, char **argv);
int cmd_getroot (optparse_t *p, int argc, char **argv);
int cmd_eventlog (optparse_t *p, int argc, char **argv);

static int get_window_width (optparse_t *p, int fd);
static void dump_kvs_dir (const flux_kvsdir_t *dir, int maxcol,
                          const char *ns, bool Ropt, bool dopt);

#define min(a,b) ((a)<(b)?(a):(b))

static struct optparse_option readlink_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "at", .key = 'a', .has_arg = 1,
      .usage = "Lookup relative to RFC 11 snapshot reference",
    },
    { .name = "namespace-only", .key = 'o', .has_arg = 0,
      .usage = "Output only namespace if link points to a namespace",
    },
    { .name = "key-only", .key = 'k', .has_arg = 0,
      .usage = "Output only key if link points to a namespace",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option get_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
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
    { .name = "waitcreate", .key = 'W', .has_arg = 0,
      .usage = "Wait for creation to occur on watch",
    },
    { .name = "watch", .key = 'w', .has_arg = 0,
      .usage = "Monitor key writes",
    },
    { .name = "uniq", .key = 'u', .has_arg = 0,
      .usage = "Only monitor key writes if values have changed",
    },
    { .name = "append", .key = 'A', .has_arg = 0,
      .usage = "Only monitor key appends",
    },
    { .name = "full", .key = 'f', .has_arg = 0,
      .usage = "Monitor key changes with more complete accuracy",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "Display at most COUNT changes",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option put_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "treeobj-root", .key = 'O', .has_arg = 0,
      .usage = "Output resulting RFC11 root containing puts",
    },
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Output blobref of root containing puts",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Output root sequence of root containing puts",
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
    { .name = "sync", .key = 'S', .has_arg = 0,
      .usage = "Flushes content and checkpoints after completing put",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option dir_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
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
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
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

static struct optparse_option dropcache_opts[] =  {
    { .name = "all", .key = 'a', .has_arg = 0,
      .usage = "Drop KVS across all ranks",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option unlink_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "treeobj-root", .key = 'O', .has_arg = 0,
      .usage = "Output resulting RFC11 root containing unlinks",
    },
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Output blobref of root containing unlinks",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Output root sequence of root containing unlinks",
    },
    { .name = "recursive", .key = 'R', .has_arg = 0,
      .usage = "Remove directory contents recursively",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "ignore nonexistent files",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option getroot_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Show sequence number",
    },
    { .name = "owner", .key = 'o', .has_arg = 0,
      .usage = "Show owner",
    },
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Show blobref",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option copy_opts[] =  {
    { .name = "src-namespace", .key = 'S', .has_arg = 1,
      .usage = "Specify source key's namespace",
    },
    { .name = "dst-namespace", .key = 'D', .has_arg = 1,
      .usage = "Specify destination key's namespace",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option link_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify link's namespace.",
    },
    { .name = "target-namespace", .key = 'T', .has_arg = 1,
      .usage = "Specify target's namespace",
    },
    { .name = "treeobj-root", .key = 'O', .has_arg = 0,
      .usage = "Output resulting RFC11 root containing link",
    },
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Output blobref of root containing link",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Output root sequence of root containing link",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option mkdir_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "treeobj-root", .key = 'O', .has_arg = 0,
      .usage = "Output resulting RFC11 root containing new directory",
    },
    { .name = "blobref", .key = 'b', .has_arg = 0,
      .usage = "Output blobref of root containing new directory",
    },
    { .name = "sequence", .key = 's', .has_arg = 0,
      .usage = "Output root sequence of root containing new directory",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option namespace_opt[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "namespace",
      NULL,
      "Perform KVS namespace operations",
      cmd_namespace,
      OPTPARSE_SUBCMD_SKIP_OPTS,
      NULL,
    },
    { "get",
      "[-N ns] [-r|-t] [-a treeobj] [-l] [-W] [-w] [-u] [-A] [-f] "
        "[-c COUNT] key [key...]",
      "Get value stored under key",
      cmd_get,
      0,
      get_opts
    },
    { "put",
      "[-N ns] [-O|-b|-s] [-r|-t] [-n] [-A] [-S] key=value [key=value...]",
      "Store value under key",
      cmd_put,
      0,
      put_opts
    },
    { "dir",
      "[-N ns] [-R] [-d] [-w COLS] [-a treeobj] [key]",
      "Display all keys under directory",
      cmd_dir,
      0,
      dir_opts
    },
    { "ls",
      "[-N ns] [-R] [-d] [-F] [-w COLS] [-1] [key...]",
      "List directory",
      cmd_ls,
      0,
      ls_opts
    },
    { "unlink",
      "[-N ns] [-O|-b|-s] [-R] [-f] key [key...]",
      "Remove key",
      cmd_unlink,
      0,
      unlink_opts
    },
    { "link",
      "[-N ns] [-T ns] [-O|-b|-s] target linkname",
      "Create a new name for target",
      cmd_link,
      0,
      link_opts
    },
    { "readlink",
      "[-N ns] [-a treeobj] [ -o | -k ] key [key...]",
      "Retrieve the key a link refers to",
      cmd_readlink,
      0,
      readlink_opts
    },
    { "mkdir",
      "[-N ns] [-O|-b|-s] key [key...]",
      "Create a directory",
      cmd_mkdir,
      0,
      mkdir_opts
    },
    { "copy",
      "[-S src-ns] [-D dst-ns] source destination",
      "Copy source key to destination key",
      cmd_copy,
      0,
      copy_opts
    },
    { "move",
      "[-S src-ns] [-D dst-ns] source destination",
      "Move source key to destination key",
      cmd_move,
      0,
      copy_opts
    },
    { "dropcache",
      "[--all]",
      "Tell KVS to drop its cache",
      cmd_dropcache,
      0,
      dropcache_opts
    },
    { "version",
      "[-N ns]",
      "Display current KVS version",
      cmd_version,
      0,
      namespace_opt
    },
    { "wait",
      "[-N ns] version",
      "Block until the KVS reaches version",
      cmd_wait,
      0,
      namespace_opt
    },
    { "getroot",
      "[-N ns] [-s|-o|-b]",
      "Get KVS root treeobj",
      cmd_getroot,
      0,
      getroot_opts
    },
    { "eventlog",
      NULL,
      "Manipulate a KVS eventlog",
      cmd_eventlog,
      OPTPARSE_SUBCMD_SKIP_OPTS,
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
        if (!(s->flags & OPTPARSE_OPT_HIDDEN))
            fprintf (stderr, "   %-15s %s\n", s->name, s->doc);
        s++;
    }
    exit (1);
}

int main (int argc, char *argv[])
{
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
        || !optparse_get_subcommand (p, argv[optindex])) {
        usage (p, NULL, NULL);
        exit (1);
    }

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

int cmd_namespace_create (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex, i;
    uint32_t owner = FLUX_USERID_UNKNOWN;
    const char *str;
    const char *rootref;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if ((str = optparse_get_str (p, "owner", NULL))) {
        char *endptr;
        owner = strtoul (str, &endptr, 10);
        if (*endptr != '\0')
            log_msg_exit ("--owner requires an unsigned integer argument");
    }

    rootref = optparse_get_str (p, "rootref", NULL);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    for (i = optindex; i < argc; i++) {
        const char *name = argv[i];
        int flags = 0;
        if (rootref)
            f = flux_kvs_namespace_create_with (h, name, rootref, owner, flags);
        else
            f = flux_kvs_namespace_create (h, name, owner, flags);
        if (!f)
            log_err_exit ("%s", name);
        if (flux_future_get (f, NULL) < 0)
            log_msg_exit ("%s: %s", name, future_strerror (f, errno));
        flux_future_destroy (f);
    }
    flux_close (h);
    return (0);
}

int cmd_namespace_remove (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex, i;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    for (i = optindex; i < argc; i++) {
        const char *name = argv[i];
        if (!(f = flux_kvs_namespace_remove (h, name))
            || flux_future_get (f, NULL) < 0)
            log_err_exit ("%s", name);
        flux_future_destroy (f);
    }
    flux_close (h);
    return (0);
}

int cmd_namespace_list (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;
    flux_future_t *f;
    json_t *array;
    int i, size;

    optindex = optparse_option_index (p);
    if ((optindex - argc) != 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc (h, "kvs.namespace-list", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get_unpack (f, "{ s:o }", "namespaces", &array) < 0)
        log_err_exit ("flux_rpc_get_unpack");
    if (!json_is_array (array))
        log_msg_exit ("namespaces object is not an array");

    size = json_array_size (array);
    printf ("NAMESPACE                                 OWNER      FLAGS\n");
    for (i = 0; i < size; i++) {
        const char *ns;
        uint32_t owner;
        int flags;
        json_t *o;

        if (!(o = json_array_get (array, i)))
            log_err_exit ("json_array_get");

        if (json_unpack (o,
                         "{ s:s s:i s:i }",
                         "namespace", &ns,
                         "owner", &owner,
                         "flags", &flags) < 0)
            log_err_exit ("json_unpack");

        printf ("%-36s %10u 0x%08X\n", ns, owner, flags);
    }

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static struct optparse_option namespace_create_opts[] =  {
    { .name = "owner", .key = 'o', .has_arg = 1,
      .usage = "Specify alternate namespace owner via userid",
    },
    { .name = "rootref", .key = 'r', .has_arg = 1,
      .usage = "Initialize namespace with specific root reference",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand namespace_subcommands[] = {
    { "create",
      "[-o owner] [-r rootref] name [name...]",
      "Create a KVS namespace",
      cmd_namespace_create,
      0,
      namespace_create_opts
    },
    { "remove",
      "name [name...]",
      "Remove a KVS namespace",
      cmd_namespace_remove,
      0,
      NULL
    },
    { "list",
      "",
      "List namespaces on local rank",
      cmd_namespace_list,
      0,
      NULL
    },
    OPTPARSE_SUBCMD_END
};

int cmd_namespace (optparse_t *p, int argc, char **argv)
{
    int optindex;

    if (optparse_reg_subcommands (p, namespace_subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("namespace: optparse_reg_subcommands failed");

    optindex = optparse_parse_args (p, argc, argv);
    if (optindex < 0)
        log_msg_exit ("namespace: optparse_parse_args failed");

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        log_msg_exit ("namespace: optparse_run_subcommand failed");

    return (0);
}

static void __attribute__ ((format (printf, 3, 4)))
kv_printf (const char *key, int maxcol, const char *fmt, ...)
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

    if (asprintf (&kv,
                  "%s%s%s",
                  key ? key : "",
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
            /* don't add ellipsis if there's nothing after the newline */
            if (*(p + 1) != '\0')
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
    printf ("%s%s\n",
            kv,
            overflow ? "..." : "");

    free (val);
    free (kv);
}

struct lookup_ctx {
    optparse_t *p;
    int maxcount;
    int count;
    const char *ns;
};


void lookup_continuation (flux_future_t *f, void *arg)
{
    struct lookup_ctx *ctx = arg;
    const char *key = flux_kvs_lookup_get_key (f);

    if (optparse_hasopt (ctx->p, "watch")
        && flux_rpc_get (f, NULL) < 0
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
    if (optparse_hasopt (ctx->p, "watch")) {
        flags |= FLUX_KVS_WATCH;
        if (optparse_hasopt (ctx->p, "full"))
            flags |= FLUX_KVS_WATCH_FULL;
        if (optparse_hasopt (ctx->p, "uniq"))
            flags |= FLUX_KVS_WATCH_UNIQ;
        if (optparse_hasopt (ctx->p, "append"))
            flags |= FLUX_KVS_WATCH_APPEND;
    }
    if (optparse_hasopt (ctx->p, "waitcreate"))
        flags |= FLUX_KVS_WAITCREATE;
    if (optparse_hasopt (ctx->p, "at")) {
        const char *reference = optparse_get_str (ctx->p, "at", NULL);
        if (!(f = flux_kvs_lookupat (h, flags, key, reference)))
            log_err_exit ("%s", key);
    }
    else {
        if (!(f = flux_kvs_lookup (h, ctx->ns, flags, key)))
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
    flux_t *h;
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
    ctx.ns = optparse_get_str (p, "namespace", NULL);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

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

    flux_close (h);
    return (0);
}

void commit_finish (flux_future_t *f, optparse_t *p)
{
    if (optparse_hasopt (p, "treeobj-root")) {
        const char *treeobj = NULL;
        if (flux_kvs_commit_get_treeobj (f, &treeobj) < 0)
            log_err_exit ("flux_kvs_commit_get_treeobj");
        printf ("%s\n", treeobj);
    }
    else if (optparse_hasopt (p, "blobref")) {
        const char *blobref = NULL;
        if (flux_kvs_commit_get_rootref (f, &blobref) < 0)
            log_err_exit ("flux_kvs_commit_get_rootref");
        printf ("%s\n", blobref);
    }
    else if (optparse_hasopt (p, "sequence")) {
        int sequence;
        if (flux_kvs_commit_get_sequence (f, &sequence) < 0)
            log_err_exit ("flux_kvs_commit_get_sequence");
        printf ("%d\n", sequence);
    }
    else {
        if (flux_future_get (f, NULL) < 0)
            log_err_exit ("flux_kvs_commit");
    }
}

int cmd_put (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
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
    ns = optparse_get_str (p, "namespace", NULL);

    if (optparse_hasopt (p, "no-merge"))
        commit_flags |= FLUX_KVS_NO_MERGE;

    if (optparse_hasopt (p, "sync"))
        commit_flags |= FLUX_KVS_SYNC;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

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

            if (streq (val, "-")) { // special handling for "--treeobj key=-"
                if ((len = read_all (STDIN_FILENO, (void **)&buf)) < 0)
                    log_err_exit ("stdin");
                val = (char *)buf;
            }
            if (flux_kvs_txn_put_treeobj (txn, 0, key, val) < 0)
                log_err_exit ("%s", key);
            free (buf);
        }
        else if (optparse_hasopt (p, "raw")) {
            int len;
            uint8_t *buf = NULL;

            if (optparse_hasopt (p, "append"))
                put_flags |= FLUX_KVS_APPEND;

            if (streq (val, "-")) { // special handling for "--raw key=-"
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
    if (!(f = flux_kvs_commit (h, ns, commit_flags, txn)))
        log_err_exit ("flux_kvs_commit");
    commit_finish (f, p);
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
    return (0);
}

static int unlink_check_dir_empty (flux_t *h, const char *key, const char *ns)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir = NULL;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READDIR, key)))
        goto done;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        goto done;
    if (flux_kvsdir_get_size (dir) > 0) {
        errno = ENOTEMPTY;
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Some checks prior to unlinking key:
 * - fail if key does not exist (ENOENT) and fopt not specified or
 *   other fatal lookup error
 * - fail if key is a non-empty directory (ENOTEMPTY) and -R was not specified
 * - if key is a link, a val, or a valref, we can always remove it.
 */
static int unlink_safety_check (flux_t *h, const char *key, const char *ns,
                                bool Ropt, bool fopt, bool *unlinkable)
{
    flux_future_t *f;
    const char *json_str;
    json_t *treeobj = NULL;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_TREEOBJ, key)))
        goto done;
    if (flux_kvs_lookup_get_treeobj (f, &json_str) < 0) {
        if (errno == ENOENT && fopt)
            goto out;
        goto done;
    }
    if (!(treeobj = treeobj_decode (json_str)))
        log_err_exit ("%s: metadata decode error", key);
    if (treeobj_is_dir (treeobj)) {
        if (!Ropt && treeobj_get_count (treeobj) > 0) {
            errno = ENOTEMPTY;
            goto done;
        }
    }
    else if (treeobj_is_dirref (treeobj)) {
        if (!Ropt) {
            /* have to do another lookup to resolve this dirref */
            if (unlink_check_dir_empty (h, key, ns) < 0)
                goto done;
        }
    }
    /* else treeobj is symlink, val, or valref */
    *unlinkable = true;
out:
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int cmd_unlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int optindex, i;
    flux_future_t *f;
    flux_kvs_txn_t *txn;
    bool Ropt, fopt;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);
    Ropt = optparse_hasopt (p, "recursive");
    fopt = optparse_hasopt (p, "force");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        bool unlinkable;
        if (unlink_safety_check (h, argv[i], ns, Ropt, fopt, &unlinkable) < 0)
            log_err_exit ("cannot unlink '%s'", argv[i]);
        if (unlinkable) {
            if (flux_kvs_txn_unlink (txn, 0, argv[i]) < 0)
                log_err_exit ("%s", argv[i]);
        }
    }
    if (!(f = flux_kvs_commit (h, ns, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    commit_finish (f, p);
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
    return (0);
}

int cmd_link (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *linkns = NULL;
    const char *targetns = NULL;
    const char *target, *linkname;
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

    linkns = optparse_get_str (p, "namespace", NULL);
    targetns = optparse_get_str (p, "target-namespace", NULL);
    target = argv[optindex];
    linkname = argv[optindex + 1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_symlink (txn, 0, linkname, targetns, target) < 0)
        log_err_exit ("%s", linkname);
    if (!(f = flux_kvs_commit (h, linkns, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    commit_finish (f, p);
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
    return (0);
}

int cmd_readlink (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int optindex, i;
    flux_future_t *f;
    bool ns_only;
    bool key_only;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);
    ns_only = optparse_hasopt (p, "namespace-only");
    key_only = optparse_hasopt (p, "key-only");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    for (i = optindex; i < argc; i++) {
        const char *linkns = NULL;
        const char *target = NULL;
        if (optparse_hasopt (p, "at")) {
            const char *ref = optparse_get_str (p, "at", NULL);
            if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, argv[i], ref)))
                log_err_exit ("%s", argv[i]);
        }
        else {
            if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READLINK, argv[i])))
                log_err_exit ("%s", argv[i]);
        }
        if (flux_kvs_lookup_get_symlink (f, &linkns, &target) < 0)
            log_err_exit ("%s", argv[i]);
        if (ns_only && linkns)
            printf ("%s\n", linkns);
        else if (key_only)
            printf ("%s\n", target);
        else {
            if (linkns)
                printf ("%s::%s\n", linkns, target);
            else
                printf ("%s\n", target);
        }
        flux_future_destroy (f);
    }
    flux_close (h);
    return (0);
}

int cmd_mkdir (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns;
    int optindex, i;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    optindex = optparse_option_index (p);
    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = optindex; i < argc; i++) {
        if (flux_kvs_txn_mkdir (txn, 0, argv[i]) < 0)
            log_err_exit ("%s", argv[i]);
    }
    if (!(f = flux_kvs_commit (h, ns, 0, txn)))
        log_err_exit ("kvs_commit");
    commit_finish (f, p);
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
    return (0);
}

int cmd_version (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int vers;

    ns = optparse_get_str (p, "namespace", NULL);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (flux_kvs_get_version (h, ns, &vers) < 0)
        log_err_exit ("flux_kvs_get_version");
    printf ("%d\n", vers);
    flux_close (h);
    return (0);
}

int cmd_wait (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int vers;
    int optindex;

    optindex = optparse_option_index (p);

    if ((optindex - argc) == 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex != (argc - 1))
        log_msg_exit ("wait: specify a version");

    ns = optparse_get_str (p, "namespace", NULL);

    vers = strtoul (argv[optindex], NULL, 10);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (flux_kvs_wait_version (h, ns, vers) < 0)
        log_err_exit ("flux_kvs_wait_version");
    flux_close (h);
    return (0);
}

int cmd_dropcache (optparse_t *p, int argc, char **argv)
{
    flux_t *h;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

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
    flux_close (h);
    return (0);
}

static char *process_key (const char *key)
{
    char *nkey;

    if (!(nkey = malloc (strlen (key) + 2))) // room for decoration char + null
        log_err_exit ("malloc");
    strcpy (nkey, key);

    return nkey;
}

static void dump_kvs_val (const char *key, int maxcol, const char *value)
{
    if (!value)
        kv_printf (key, maxcol, "%s", "");
    else
        kv_printf (key, maxcol, "%s", value);
}

static void dump_kvs_dir (const flux_kvsdir_t *dir, int maxcol,
                          const char *ns, bool Ropt, bool dopt)
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
            const char *ns = NULL;
            const char *target = NULL;
            if (rootref) {
                if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key,
                                             rootref)))
                    log_err_exit ("%s", key);
            }
            else {
                if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READLINK, key)))
                    log_err_exit ("%s", key);
            }
            if (flux_kvs_lookup_get_symlink (f, &ns, &target) < 0)
                log_err_exit ("%s", key);
            if (ns)
                printf ("%s -> %s::%s\n", key, ns, target);
            else
                printf ("%s -> %s\n", key, target);
            flux_future_destroy (f);
        } else if (flux_kvsdir_isdir (dir, name)) {
            if (Ropt) {
                const flux_kvsdir_t *ndir;
                if (rootref) {
                    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key,
                                                 rootref)))
                        log_err_exit ("%s", key);
                }
                else {
                    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READDIR, key)))
                        log_err_exit ("%s", key);
                }
                if (flux_kvs_lookup_get_dir (f, &ndir) < 0)
                    log_err_exit ("%s", key);
                if (flux_kvsdir_get_size (ndir) == 0)
                    printf ("%s.\n", key);
                else
                    dump_kvs_dir (ndir, maxcol, ns, Ropt, dopt);
                flux_future_destroy (f);
            } else
                printf ("%s.\n", key);
        } else {
            if (!dopt) {
                const char *value;
                const void *buf;
                int len;
                if (rootref) {
                    if (!(f = flux_kvs_lookupat (h, 0, key, rootref)))
                        log_err_exit ("%s", key);
                }
                else {
                    if (!(f = flux_kvs_lookup (h, ns, 0, key)))
                        log_err_exit ("%s", key);
                }
                if (flux_kvs_lookup_get (f, &value) == 0) // null terminated
                    dump_kvs_val (key, maxcol, value);
                else if (flux_kvs_lookup_get_raw  (f, &buf, &len) == 0)
                    kv_printf (key, maxcol, "%.*s", len, (char *)buf);
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
    flux_t *h;
    int maxcol = get_window_width (p, STDOUT_FILENO);
    const char *ns = NULL;
    bool Ropt;
    bool dopt;
    char *key;
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    int optindex;

    optindex = optparse_option_index (p);
    ns = optparse_get_str (p, "namespace", NULL);
    Ropt = optparse_hasopt (p, "recursive");
    dopt = optparse_hasopt (p, "directory");
    if (optindex == argc)
        key = ".";
    else if (optindex == (argc - 1))
        key = argv[optindex];
    else
        log_msg_exit ("dir: specify zero or one directory");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "at")) {
        const char *reference = optparse_get_str (p, "at", NULL);
        if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, reference)))
            log_err_exit ("%s", key);
    }
    else {
        if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READDIR, key)))
            log_err_exit ("%s", key);
    }
    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        log_err_exit ("%s", key);
    dump_kvs_dir (dir, maxcol, ns, Ropt, dopt);
    flux_future_destroy (f);
    flux_close (h);
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
static void list_kvs_dir_single (const flux_kvsdir_t *dir,
                                 int win_width,
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
static void list_kvs_dir (flux_t *h,
                          const char *ns,
                          const char *key,
                          optparse_t *p,
                          int win_width,
                          bool print_label,
                          bool print_vspace)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;

    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_READDIR, key))
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
                list_kvs_dir (h, ns, nkey, p, win_width, print_label, true);
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

/* links are special.  If it points to a value, output the link name.
 * If it points to a dir, output contents of the dir.  If it points to
 * an illegal key, still output the link name. */
static int categorize_link (flux_t *h,
                            const char *ns,
                            char *nkey,
                            zlist_t *dirs,
                            zlist_t *singles)
{
    flux_future_t *f;

    if (!(f = flux_kvs_lookup (h, ns, 0, nkey)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get (f, NULL) < 0) {
        if (errno == ENOENT) {
            if (zlist_append (singles, nkey) < 0)
                log_err_exit ("zlist_append");
        }
        else if (errno == EISDIR) {
            if (zlist_append (dirs, nkey) < 0)
                log_err_exit ("zlist_append");
        }
        else
            log_err_exit ("%s", nkey);
    }
    else {
        if (zlist_append (singles, nkey) < 0)
            log_err_exit ("zlist_append");
    }
    flux_future_destroy (f);
    return 0;
}

/* Put key in 'dirs' or 'singles' list, depending on whether
 * its contents are to be listed or not.  If -F is specified,
 * 'singles' key names are decorated based on their type.
 */
static int categorize_key (flux_t *h,
                           optparse_t *p,
                           const char *ns,
                           const char *key,
                           zlist_t *dirs,
                           zlist_t *singles)
{
    flux_future_t *f;
    const char *json_str;
    char *nkey;
    json_t *treeobj = NULL;
    bool require_directory = false;

    nkey = process_key (key);

    /* If the key has a "." suffix, strip it off, but require
     * that the key be a directory type.
     */
    while (strlen (nkey) > 1 && nkey[strlen (nkey) - 1] == '.') {
        nkey[strlen (nkey) - 1] = '\0';
        require_directory = true;
    }
    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_TREEOBJ, nkey)))
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
        if (zlist_append (singles, nkey) < 0)
            log_err_exit ("zlist_append");
    }
    else if (treeobj_is_symlink (treeobj)) {
        if (require_directory) {
            fprintf (stderr, "%s: Not a directory\n", nkey);
            goto error;
        }
        if (optparse_hasopt (p, "classify"))
            strcat (nkey, "@");
        /* do not follow symlink under several circumstances */
        if (optparse_hasopt (p, "classify")
            || optparse_hasopt (p, "directory")) {
            if (zlist_append (singles, nkey) < 0)
                log_err_exit ("zlist_append");
        }
        else {
            if (categorize_link (h, ns, nkey, dirs, singles) < 0)
                goto error;
        }
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
    flux_t *h;
    const char *ns = NULL;
    int optindex = optparse_option_index (p);
    int win_width = get_window_width (p, STDOUT_FILENO);
    zlist_t *dirs, *singles;
    char *key;
    bool print_label = false;   // print "name:" before directory contents
    bool print_vspace = false;  // print vertical space before label
    int rc = 0;

    ns = optparse_get_str (p, "namespace", NULL);

    if (optparse_hasopt (p, "1"))
        win_width = 1;

    if (!(dirs = zlist_new ()) || !(singles = zlist_new ()))
        log_err_exit ("zlist_new");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optindex == argc) {
        if (categorize_key (h, p, ns, ".", dirs, singles) < 0)
            rc = -1;
    }
    while (optindex < argc) {
        if (categorize_key (h, p, ns, argv[optindex++], dirs, singles) < 0)
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
        list_kvs_dir (h, ns, key, p, win_width, print_label, print_vspace);
        print_vspace = true;
        free (key);
    }

    zlist_destroy (&dirs);
    zlist_destroy (&singles);

    flux_close (h);
    return rc;
}

int cmd_copy (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;
    flux_future_t *f;
    const char *srckey, *dstkey;
    const char *srcns, *dstns;

    optindex = optparse_option_index (p);
    if (optindex != (argc - 2))
        log_msg_exit ("copy: specify srckey dstkey");

    srcns = optparse_get_str (p, "src-namespace", NULL);
    dstns = optparse_get_str (p, "dst-namespace", NULL);

    srckey = argv[optindex];
    dstkey = argv[optindex + 1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_copy (h, srcns, srckey, dstns, dstkey, 0))
            || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_copy");
    flux_future_destroy (f);

    flux_close (h);
    return (0);
}

int cmd_move (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex;
    flux_future_t *f;
    const char *srckey, *dstkey;
    const char *srcns, *dstns;

    optindex = optparse_option_index (p);
    if (optindex != (argc - 2))
        log_msg_exit ("move: specify srckey dstkey");

    srcns = optparse_get_str (p, "src-namespace", NULL);
    dstns = optparse_get_str (p, "dst-namespace", NULL);

    srckey = argv[optindex];
    dstkey = argv[optindex + 1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_move (h, srcns, srckey, dstns, dstkey, 0))
            || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_move");
    flux_future_destroy (f);

    flux_close (h);
    return (0);
}

void getroot_continuation (flux_future_t *f, void *arg)
{
    optparse_t *p = arg;

    if (optparse_hasopt (p, "owner")) {
        uint32_t owner;

        if (flux_kvs_getroot_get_owner (f, &owner) < 0)
            log_err_exit ("flux_kvs_getroot_get_owner");
        printf ("%lu\n", (unsigned long)owner);
    }
    else if (optparse_hasopt (p, "sequence")) {
        int sequence;

        if (flux_kvs_getroot_get_sequence (f, &sequence) < 0)
            log_err_exit ("flux_kvs_getroot_get_sequence");
        printf ("%d\n", sequence);
    }
    else if (optparse_hasopt (p, "blobref")) {
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
    fflush (stdout);
    flux_future_destroy (f);
}

int cmd_getroot (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    const char *ns = NULL;
    flux_future_t *f;
    int flags = 0;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_getroot (h, ns, flags)))
        log_err_exit ("flux_kvs_getroot");
    if (flux_future_then (f, -1., getroot_continuation, p) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");
    flux_close (h);
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
static flux_future_t *eventlog_append_event (flux_t *h,
                                             const char *ns,
                                             const char *key,
                                             double timestamp,
                                             const char *name,
                                             const char *context)
{
    flux_kvs_txn_t *txn;
    json_t *entry;
    char *entrystr;
    flux_future_t *f;

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (!(entry = eventlog_entry_create (timestamp, name, context)))
        log_err_exit ("eventlog_entry_create");
    if (!(entrystr = eventlog_entry_encode (entry)))
        log_err_exit ("eventlog_entry_encode");
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, entrystr) < 0)
        log_err_exit ("flux_kvs_txn_put");
    if (!(f = flux_kvs_commit (h, ns, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    free (entrystr);
    json_decref (entry);
    return f;
}

int cmd_eventlog_append (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int optindex = optparse_option_index (p);
    const char *key;
    const char *name;
    char *context = NULL;
    flux_future_t *f;
    double timestamp = optparse_get_double (p, "timestamp", 0.);

    if (argc - optindex < 2) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);

    key = argv[optindex++];
    name = argv[optindex++];
    if (optindex < argc)
        context = eventlog_context_from_args (argv + optindex);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    f = eventlog_append_event (h, ns, key, timestamp, name, context);
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");

    flux_future_destroy (f);
    free (context);

    flux_close (h);
    return (0);
}

struct eventlog_get_ctx {
    optparse_t *p;
    int maxcount;
    int count;
    struct eventlog_formatter *evf;
};

struct eventlog_wait_event_ctx {
    optparse_t *p;
    const char *key;
    const char *wait_event;
    bool got_event;
    struct eventlog_formatter *evf;
};

static struct eventlog_formatter *formatter_create (optparse_t *p)
{
    struct eventlog_formatter *evf;
    const char *color = optparse_get_str (p, "color", "auto");
    bool human = optparse_hasopt (p, "human");
    bool unformatted = optparse_hasopt (p, "unformatted");

    if (unformatted && (human || streq (color, "always"))) {
        log_msg ("Do not specify --unformatted with --human or --color=always");
        return NULL;
    }
    if (!(evf = eventlog_formatter_create ()))
        return NULL;
    if (unformatted) {
        if (eventlog_formatter_set_format (evf, "json") < 0) {
            log_err ("failed to set json output");
            goto error;
        }
    }
    if (human && eventlog_formatter_set_timestamp_format (evf, "human") < 0) {
        log_err ("failed to set human timestamp format");
        goto error;
    }
    if (eventlog_formatter_colors_init (evf, color ? color : "always") < 0) {
        log_err ("invalid value: --color=%s", color);
        goto error;
    }
    return evf;
error:
    eventlog_formatter_destroy (evf);
    return NULL;
}

void eventlog_get_continuation (flux_future_t *f, void *arg)
{
    struct eventlog_get_ctx *ctx = arg;
    const char *s;
    json_t *a;
    size_t index;
    json_t *value;
    flux_error_t error;
    bool limit_reached = false;

    /* Handle canceled lookup (FLUX_KVS_WATCH flag only).
     * Destroy the future and return (reactor will then terminate).
     * Errors other than ENODATA are handled by the flux_kvs_lookup_get().
     */
    if (optparse_hasopt (ctx->p, "watch")
        && flux_rpc_get (f, NULL) < 0
        && errno == ENODATA) {
        flux_future_destroy (f);
        return;
    }

    if (flux_kvs_lookup_get (f, &s) < 0)
        log_err_exit ("flux_kvs_lookup_get");

    if (!(a = eventlog_decode (s)))
        log_err_exit ("eventlog_decode");

    json_array_foreach (a, index, value) {
        if (ctx->maxcount == 0 || ctx->count < ctx->maxcount) {
            if (eventlog_entry_dumpf (ctx->evf, stdout, &error, value) < 0)
                log_msg ("failed to print eventlog entry: %s", error.text);
        }
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

    json_decref (a);
}

int cmd_eventlog_get (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int optindex = optparse_option_index (p);
    const char *key;
    flux_future_t *f;
    int flags = 0;
    struct eventlog_get_ctx ctx;

    if (argc - optindex != 1) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);

    key = argv[optindex++];
    if (optparse_hasopt (p, "watch")) {
        flags |= FLUX_KVS_WATCH;
        flags |= FLUX_KVS_WATCH_APPEND;
    }
    if (optparse_hasopt (p, "waitcreate"))
        flags |= FLUX_KVS_WAITCREATE;

    ctx.p = p;
    ctx.count = 0;
    ctx.maxcount = optparse_get_int (p, "count", 0);

    if (!(ctx.evf = formatter_create (p)))
        log_msg_exit ("failed to create eventlog formatter");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_lookup (h, ns, flags, key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_future_then (f, -1., eventlog_get_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    eventlog_formatter_destroy (ctx.evf);
    return (0);
}

void eventlog_wait_event_continuation (flux_future_t *f, void *arg)
{
    struct eventlog_wait_event_ctx *ctx = arg;
    const char *s;
    json_t *a;
    size_t index;
    json_t *value;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            log_msg_exit ("eventlog path %s not found", ctx->key);
        }
        else if (errno == ETIMEDOUT) {
            flux_future_destroy (f);
            log_msg_exit ("wait-event timeout on event '%s'",
                          ctx->wait_event);
        }
        else if (errno == ENODATA) {
            flux_future_destroy (f);
            if (!ctx->got_event)
                log_msg_exit ("event '%s' never received",
                              ctx->wait_event);
            return;
        }
        else
            log_err_exit ("flux_kvs_lookup_get");
    }

    if (!(a = eventlog_decode (s)))
        log_err_exit ("eventlog_decode");

    json_array_foreach (a, index, value) {
        const char *name;
        double timestamp;
        flux_error_t error;

        if (eventlog_entry_parse (value, &timestamp, &name, NULL) < 0)
            log_err_exit ("eventlog_entry_parse");

        eventlog_formatter_update_t0 (ctx->evf, timestamp);

        if (streq (name, ctx->wait_event)) {
            if (!optparse_hasopt (ctx->p, "quiet")) {
                if (eventlog_entry_dumpf (ctx->evf, stdout, &error, value) < 0)
                    log_msg ("failed to dump event %s: %s", name, error.text);
            }
            ctx->got_event = true;
            break;
        }
        else if (optparse_hasopt (ctx->p, "verbose")) {
            if (!ctx->got_event) {
                if (eventlog_entry_dumpf (ctx->evf, stdout, &error, value) < 0)
                    log_msg ("failed to dump event %s: %s", name, error.text);
            }
        }
    }

    fflush (stdout);

    if (ctx->got_event) {
        if (flux_kvs_lookup_cancel (f) < 0)
            log_err_exit ("flux_kvs_lookup_cancel");
    }
    flux_future_reset (f);
    json_decref (a);
}

int cmd_eventlog_wait_event (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *ns = NULL;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    double timeout;
    int flags = 0;
    struct eventlog_wait_event_ctx ctx;

    if (argc - optindex != 2) {
        optparse_print_usage (p);
        exit (1);
    }
    ns = optparse_get_str (p, "namespace", NULL);

    ctx.p = p;
    ctx.key = argv[optindex++];
    ctx.wait_event = argv[optindex++];
    ctx.got_event = false;

    if (!(ctx.evf = formatter_create (p)))
        log_msg_exit ("failed to create eventlog formatter");

    timeout = optparse_get_duration (p, "timeout", -1.0);

    flags |= FLUX_KVS_WATCH;
    flags |= FLUX_KVS_WATCH_APPEND;
    if (optparse_hasopt (p, "waitcreate"))
        flags |= FLUX_KVS_WAITCREATE;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_lookup (h, ns, flags, ctx.key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_future_then (f,
                          timeout,
                          eventlog_wait_event_continuation,
                          &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    eventlog_formatter_destroy (ctx.evf);
    return (0);
}

static struct optparse_option eventlog_append_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "timestamp", .key = 't', .has_arg = 1, .arginfo = "SECONDS",
      .usage = "Specify timestamp in seconds since epoch",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option eventlog_get_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "waitcreate", .key = 'W', .has_arg = 0,
      .usage = "Wait for creation to occur on watch",
    },
    { .name = "watch", .key = 'w', .has_arg = 0,
      .usage = "Monitor eventlog",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "Display at most COUNT events",
    },
    { .name = "unformatted", .key = 'u', .has_arg = 0,
      .usage = "Show event in RFC 18 form",
    },
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Display human-readable output",
    },
    { .name = "color", .key = 'L', .has_arg = 2, .arginfo = "WHEN",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)."
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option eventlog_wait_event_opts[] =  {
    { .name = "namespace", .key = 'N', .has_arg = 1,
      .usage = "Specify KVS namespace to use.",
    },
    { .name = "waitcreate", .key = 'W', .has_arg = 0,
      .usage = "Wait for creation to occur on eventlog",
    },
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    { .name = "unformatted", .key = 'u', .has_arg = 0,
      .usage = "Show event in RFC 18 form",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Do not output matched event",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Output all events before matched event",
    },
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Display human-readable output",
    },
    { .name = "color", .key = 'L', .has_arg = 2, .arginfo = "WHEN",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)."
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand eventlog_subcommands[] = {
    { "append",
      "[-N ns] [-t SECONDS] key name [context ...]",
      "Append to eventlog",
      cmd_eventlog_append,
      0,
      eventlog_append_opts,
    },
    { "get",
      "[-N ns] [-u] [-W] [-w] [-c COUNT] [-H] [-L auto|always|never] key",
      "Get eventlog",
      cmd_eventlog_get,
      0,
      eventlog_get_opts,
    },
    { "wait-event",
      "[-N ns] [-t seconds] [-u] [-W] [-q] [-v] [-L auto|always|never] "
      "key event",
      "Wait for an event",
      cmd_eventlog_wait_event,
      0,
      eventlog_wait_event_opts,
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
