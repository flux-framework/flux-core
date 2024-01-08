/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include "builtin.h"

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>
#include <jansson.h>
#include <archive.h>

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libcontent/content.h"
#include "src/common/libfilemap/filemap.h"
#include "src/common/libutil/fileref.h"

static const char *default_chunksize = "1M";
static const char *default_small_file_threshold = "4K";

static json_t *get_list_option (optparse_t *p,
                                const char *name,
                                const char *default_value)
{
    const char *item;
    json_t *a;
    json_t *o;

    if (!(a = json_array ()))
        log_msg_exit ("out of memory");
    optparse_getopt_iterator_reset (p, name);
    while ((item = optparse_getopt_next (p, name))) {
        if (!(o = json_string (item))
            || json_array_append_new (a, o) < 0)
            log_msg_exit ("out of memory");
    }
    if (json_array_size (a) == 0 && default_value) {
        if (!(o = json_string (default_value))
            || json_array_append_new (a, o) < 0)
            log_msg_exit ("out of memory");
    }
    return a;
}

static char *realpath_nofollow (const char *path)
{
    char *cpy;
    char *cpy2 = NULL;
    char *cdir = NULL;
    char *result = NULL;
    int saved_errno;

    if (!(cpy = strdup (path))
        || !(cpy2 = strdup (path)))
        goto done;
    if (!(cdir = realpath (dirname (cpy), NULL)))
        goto done;
    if (asprintf (&result, "%s/%s", cdir, basename (cpy2)) < 0)
        goto done;
done:
    saved_errno = errno;
    free (cdir);
    free (cpy2);
    free (cpy);
    errno = saved_errno;
    return result;
}

static flux_future_t *mmap_add (flux_t *h,
                                 const char *path,
                                 bool disable_mmap,
                                 int chunksize,
                                 int threshold,
                                 json_t *tags)
{
    flux_future_t *f;
    char *fpath;
    struct stat sb;

    /* N.B. Provide full path to the broker but let the one that goes in
     * the fileref be relative, if that's what is specified.
     * The broker may not be running in the same directory as commands so it
     * needs the full path, but the relative path should be preserved for
     * extraction.
     */
    if (lstat (path, &sb) < 0)
        log_err_exit ("%s", path);
    if (S_ISLNK (sb.st_mode))
        fpath = realpath_nofollow (path);
    else
        fpath = realpath (path, NULL);
    if (!fpath)
        log_err_exit ("%s", path);
    f = flux_rpc_pack (h,
                       "content.mmap-add",
                       FLUX_NODEID_ANY,
                       0,
                       "{s:s s:s s:b s:i s:i s:O}",
                       "path", path,
                       "fullpath", fpath,
                       "disable_mmap", disable_mmap ? 1 : 0,
                       "threshold", threshold,
                       "chunksize", chunksize,
                       "tags", tags);
    ERRNO_SAFE_WRAP (free, fpath);
    return f;
}

static flux_future_t *mmap_remove (flux_t *h, json_t *tags)
{
    flux_future_t *f;
    f = flux_rpc_pack (h, "content.mmap-remove", 0, 0, "{s:O}", "tags", tags);
    return f;
}

struct map_ctx {
    optparse_t *p;
    flux_t *h;
    int verbose;
    int chunksize;
    int threshold;
    bool disable_mmap;
    json_t *tags;
};

static int map_visitor (dirwalk_t *d, void *arg)
{
    struct map_ctx *ctx = arg;
    const char *path = dirwalk_path (d);
    flux_future_t *f;

    if (ctx->verbose > 0)
        printf ("%s\n", path);
    if (!(f = mmap_add (ctx->h,
                        path,
                        ctx->disable_mmap,
                        ctx->chunksize,
                        ctx->threshold,
                        ctx->tags))
        || flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%s: %s", path, future_strerror (f, errno));
    flux_future_destroy (f);

    return 0;
}

static int subcmd_map (optparse_t *p, int ac, char *av[])
{
    struct map_ctx ctx;
    int n = optparse_option_index (p);
    const char *directory = optparse_get_str (p, "directory", NULL);
    int flags = DIRWALK_FIND_DIR | DIRWALK_DEPTH;

    if (n == ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (directory) {
        if (chdir (directory) < 0)
            log_err_exit ("chdir %s", directory);
    }
    ctx.p = p;
    ctx.verbose = optparse_get_int (p, "verbose", 0);
    ctx.chunksize = optparse_get_size_int (p,
                                           "chunksize",
                                           default_chunksize);
    ctx.threshold = optparse_get_size_int (p,
                                           "small-file-threshold",
                                           default_small_file_threshold);
    ctx.disable_mmap = optparse_hasopt (p, "disable-mmap");
    ctx.tags = get_list_option (p, "tags", "main");
    ctx.h = builtin_get_flux_handle (p);
    if (!ctx.h)
        log_err_exit ("flux_open");
    while (n < ac) {
        const char *path = av[n++];
        struct stat sb;

        if (lstat (path, &sb) < 0)
            log_err_exit ("%s", path);
        if (S_ISDIR (sb.st_mode)) {
            if (dirwalk (path, flags, map_visitor, &ctx) < 0)
                log_err_exit ("%s", path);
        }
        else {
            flux_future_t *f;
            if (ctx.verbose > 0)
                printf ("%s\n", path);
            if (!(f = mmap_add (ctx.h,
                                path,
                                ctx.disable_mmap,
                                ctx.chunksize,
                                ctx.threshold,
                                ctx.tags))
                || flux_rpc_get (f, NULL) < 0)
                log_msg_exit ("%s: %s", path, future_strerror (f, errno));
            flux_future_destroy (f);
        }
    }
    json_decref (ctx.tags);
    flux_close (ctx.h);
    return 0;
}

static int subcmd_unmap (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    json_t *tags = get_list_option (p, "tags", "main");
    flux_t *h;
    flux_future_t *f;

    if (n != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = mmap_remove (h, tags))
        || flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%s", future_strerror (f, errno));
    flux_future_destroy (f);
    json_decref (tags);
    flux_close (h);
    return 0;
}

static int subcmd_list (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    json_t *tags = get_list_option (p, "tags", "main");
    bool blobref = optparse_hasopt (p, "blobref");
    bool long_form = optparse_hasopt (p, "long");
    const char *pattern = NULL;
    flux_t *h;
    size_t index;
    json_t *entry;
    flux_future_t *f;

    if (n < ac)
        pattern = av[n++];
    if (n != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = filemap_mmap_list (h, blobref, tags, pattern)))
        log_err_exit ("mmap-list");
    for (;;) {
        json_t *files;

        if (flux_rpc_get_unpack (f, "{s:o}", "files", &files) < 0) {
            if (errno == ENODATA)
                break; // end of stream
            log_msg_exit ("mmap-list: %s", future_strerror (f, errno));
        }
        json_array_foreach (files, index, entry) {
            if (optparse_hasopt (p, "blobref")) {
                printf ("%s\n", json_string_value (entry));
            }
            else if (optparse_hasopt (p, "raw")) {
                if (json_dumpf (entry, stdout, JSON_COMPACT) < 0)
                    log_msg_exit ("error dumping RFC 37 file system object");
            }
            else {
                char buf[1024];
                fileref_pretty_print (entry,
                                      NULL,
                                      long_form,
                                      buf,
                                      sizeof (buf));
                printf ("%s\n", buf);
            }
        }
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    json_decref (tags);
    flux_close (h);
    return 0;
}

static void trace_fn (void *arg,
                      json_t *fileref,
                      const char *path,
                      int mode,
                      int64_t size,
                      int64_t mtime,
                      int64_t ctime,
                      const char *encoding)
{
    optparse_t *p = arg;
    if (optparse_get_int (p, "verbose", 0) > 0)
        fprintf (stderr, "%s\n", path);
}

static void extract (flux_t *h,
                     optparse_t *p,
                     const char *pattern)
{
    json_t *tags = get_list_option (p, "tags", "main");
    bool direct = optparse_hasopt (p, "direct");
    flux_future_t *f;
    flux_error_t error;

    if (!(f = filemap_mmap_list (h, !direct, tags, pattern)))
        log_err_exit ("mmap-list");
    for (;;) {
        json_t *files;

        if (flux_rpc_get_unpack (f, "{s:o}", "files", &files) < 0) {
            if (errno == ENODATA)
                break; // end of stream
            log_msg_exit ("mmap-list: %s", future_strerror (f, errno));
        }
        if (filemap_extract (h, files, direct, 0, &error, trace_fn, p) < 0)
            log_msg_exit ("%s", error.text);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
}

static int subcmd_get (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    const char *directory = optparse_get_str (p, "directory", NULL);
    const char *pattern = NULL;
    flux_t *h;

    if (n < ac)
        pattern = av[n++];
    if (n != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (directory) {
        if (chdir (directory) < 0)
            log_err_exit ("chdir %s", directory);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    extract (h, p, pattern);
    flux_close (h);
    return 0;
}

int cmd_filemap (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-filemap");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option map_opts[] = {
    { .name = "directory", .key = 'C', .has_arg = 1, .arginfo = "DIR",
      .usage = "Change to DIR before mapping", },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Increase output detail.", },
    { .name = "chunksize", .has_arg = 1, .arginfo = "N[KMG]",
      .usage = "Limit blob size to N bytes with 0=unlimited"
               " (default 1M)", },
    { .name = "small-file-threshold", .has_arg = 1, .arginfo = "N[KMG]",
      .usage = "Adjust the maximum size of a \"small file\" in bytes"
               " (default 4K)", },
    { .name = "disable-mmap", .has_arg = 0,
      .usage = "Never mmap(2) files into the content cache", },
    { .name = "tags", .key = 'T', .has_arg = 1, .arginfo = "NAME,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Specify comma-separated tags (default: main)", },
      OPTPARSE_TABLE_END
};

static struct optparse_option unmap_opts[] = {
    { .name = "tags", .key = 'T', .has_arg = 1, .arginfo = "NAME,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Specify comma-separated tags (default: main)", },
      OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] = {
    { .name = "long", .key = 'l', .has_arg = 0,
      .usage = "Show file type, mode, size", },
    { .name = "blobref", .has_arg = 0,
      .usage = "List blobrefs only, do not dereference them", },
    { .name = "raw", .has_arg = 0,
      .usage = "Show raw RFC 37 file system object without decoding", },
    { .name = "tags", .key = 'T', .has_arg = 1, .arginfo = "NAME,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Specify comma-separated tags (default: main)", },
      OPTPARSE_TABLE_END
};

static struct optparse_option get_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Show filenames on stderr", },
    { .name = "directory", .key = 'C', .has_arg = 1, .arginfo = "DIR",
      .usage = "Change to DIR before extracting", },
    { .name = "tags", .key = 'T', .has_arg = 1, .arginfo = "NAME,...",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Specify comma-separated tags (default: main)", },
    { .name = "direct", .has_arg = 0,
      .usage = "Fetch filerefs directly (fastest for single client)", },
      OPTPARSE_TABLE_END
};

static struct optparse_subcommand filemap_subcmds[] = {
    { "map",
      "[--tags=LIST] [--directory=DIR] PATH ...",
      "Map file(s) into the content cache",
      subcmd_map,
      0,
      map_opts,
    },
    { "unmap",
      "[--tags=LIST]",
      "Unmap files from the content cache",
      subcmd_unmap,
      0,
      unmap_opts,
    },
    { "list",
      "[--tags=LIST] [--long] [PATTERN]",
      "List files mapped into the content cache",
      subcmd_list,
      0,
      list_opts,
    },
    { "get",
      "[--tags=LIST] [--directory=DIR] [PATTERN]",
      "Extract files from content cache",
      subcmd_get,
      0,
      get_opts,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_filemap_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
                                 "filemap",
                                 cmd_filemap,
                                 NULL,
                                 "File staging utility", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "filemap"),
                                  filemap_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
