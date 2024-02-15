/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <fnmatch.h>
#include <sys/mman.h>

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libcontent/content.h"
#include "src/common/libfilemap/filemap.h"
#include "src/common/libfilemap/fileref.h"

static void unlink_archive (flux_t *h,
                            const char *namespace,
                            const char *name,
                            bool force);
static void unmap_archive (flux_t *h, const char *name);

static const char *default_chunksize = "1M";
static const char *default_small_file_threshold = "1K";
const char *default_archive_hashtype = "sha1";
const char *default_name = "main";

/* Return true if RFC 37 fileref has blobref encoding
 */
static bool is_blobvec_encoding (json_t *fileref)
{
    const char *encoding;
    if (json_unpack (fileref, "{s:s}", "encoding", &encoding) < 0
        || !streq (encoding, "blobvec"))
        return false;
    return true;
}

struct create_ctx {
    optparse_t *p;
    flux_t *h;
    const char *name;
    const char *namespace;
    int verbose;
    struct blobvec_param param;
    json_t *archive;
    flux_kvs_txn_t *txn;
    int preserve_seq;
};

/* Request that the content module mmap(2) the file at 'path', providing
 * the same 'chunksize' as was used to create the RFC 37 fileref below,
 * so that all the same blobrefs are created and made available in the cache.
 */
static void mmap_fileref_data (struct create_ctx *ctx, const char *path)
{
    char *fullpath;
    flux_future_t *f;

    // relative path is preserved in the archive, but broker needs full path
    if (!(fullpath = realpath (path, NULL)))
        log_err_exit ("%s", path);

    if (!(f = flux_rpc_pack (ctx->h,
                             "content.mmap-add",
                             0,
                             0,
                             "{s:s s:i s:s}",
                             "path", fullpath,
                             "chunksize", ctx->param.chunksize,
                             "tag", ctx->name))
        || flux_rpc_get (f, NULL))
        log_msg_exit ("%s: %s", path, future_strerror (f, errno));
    flux_future_destroy (f);
    free (fullpath);
}

/* Store the blobs of an RFC 37 blobvec-encoded fileref to the content store.
 * If the --preserve option was specified, create a KVS reference to each
 * blob (added to the pending KVS transaction).
 */
static void store_fileref_data (struct create_ctx *ctx,
                                const char *path,
                                json_t *fileref,
                                struct blobvec_mapinfo *mapinfo)
{
    json_t *data;

    if (json_unpack (fileref, "{s:o}", "data", &data) == 0) {
        size_t index;
        json_t *entry;

        // iterate over blobs in blobvec
        json_array_foreach (data, index, entry) {
            const char *blobref;
            json_int_t size;
            json_int_t offset;
            flux_future_t *f;

            if (json_unpack (entry, "[I,I,s]", &offset, &size, &blobref) < 0)
                log_msg_exit ("%s: error decoding fileref object data", path);
            if (offset + size > mapinfo->size)
                log_msg_exit ("%s: fileref offset exceeds file size", path);

            // store blob (synchronously)
            if (!(f = content_store (ctx->h, mapinfo->base + offset, size, 0))
                || flux_rpc_get (f, NULL) < 0)
                log_msg_exit ("%s: error storing blob: %s",
                              path,
                              future_strerror (f, errno));
            flux_future_destroy (f);

            /* Optionally store a KVS key that references blob for --preserve.
             * N.B. we don't attempt to combine blobrefs that belong to the
             * same file to conserve metadata because dump/restore might not
             * use the same chunksize, rendering archive blobrefs invalid.
             */
            if (optparse_hasopt (ctx->p, "preserve")) {
                json_t *valref;
                char *s;
                char *key;

                if (!(valref = treeobj_create_valref (blobref))
                    || !(s = treeobj_encode (valref))
                    || asprintf (&key,
                                 "archive.%s_blobs.%d",
                                 ctx->name,
                                 ctx->preserve_seq++) < 0
                    || flux_kvs_txn_put_treeobj (ctx->txn, 0, key, s) < 0)
                    log_err_exit ("%s: error preserving blobrefs", path);
                free (key);
                free (s);
                json_decref (valref);
            }
        }
    }
}

/* Create an RFC 37 fileref object for 'path', and append it to ctx->archive.
 * Then synchronously store any blobs to the content store if the file is not
 * fully contained in the fileref.
 */
static void add_archive_file (struct create_ctx *ctx, const char *path)
{
    struct blobvec_mapinfo mapinfo;
    flux_error_t error;
    json_t *fileref;

    if (!(fileref = fileref_create_ex (path,
                                       &ctx->param,
                                       &mapinfo,
                                       &error)))
        log_msg_exit ("%s", error.text); // error text includes path
    if (json_array_append_new (ctx->archive, fileref) < 0)
        log_msg_exit ("%s: out of memory", path);
    if (is_blobvec_encoding (fileref)) {
        if (optparse_hasopt (ctx->p, "mmap"))
            mmap_fileref_data (ctx, path);
        else
            store_fileref_data (ctx, path, fileref, &mapinfo);
    }
    (void)munmap (mapinfo.base, mapinfo.size);
}

// dirwalk visitor
static int archive_visitor (dirwalk_t *d, void *arg)
{
    struct create_ctx *ctx = arg;
    const char *path = dirwalk_path (d);

    if (streq (path, "."))
        return 0;
    if (ctx->verbose > 0)
        printf ("%s\n", path);
    add_archive_file (ctx, path);
    return 0;
}

static int subcmd_create (optparse_t *p, int ac, char *av[])
{
    struct create_ctx ctx;
    int n = optparse_option_index (p);
    const char *directory = optparse_get_str (p, "directory", NULL);
    int flags = DIRWALK_FIND_DIR | DIRWALK_DEPTH;
    const char *s;
    char *hashtype;
    char *key;

    memset (&ctx, 0, sizeof (ctx));

    if (n == ac) {
        optparse_print_usage (p);
        exit (1);
    }

    ctx.p = p;
    ctx.name = optparse_get_str (p, "name", default_name);
    ctx.namespace = "primary";
    if (optparse_hasopt (p, "no-force-primary"))
        ctx.namespace = NULL;
    ctx.verbose = optparse_get_int (p, "verbose", 0);
    ctx.param.chunksize = optparse_get_size_int (p,
                                                 "chunksize",
                                                 default_chunksize);
    ctx.param.small_file_threshold = optparse_get_size_int (p,
                                                 "small-file-threshold",
                                                 default_small_file_threshold);
    if (!(ctx.h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    /* --mmap lets large files be represented in the content cache without
     * being copied.  It is efficient for broadcasting large files such as
     * VM images that are not practical to copy into the KVS, but it has
     * several caveats:
     * - it is only supported on the rank 0 broker (via content.mmap-* RPCs)
     * - the files must not change while they are mapped
     * - when the files are unmapped, references (blobrefs) become invalid
     */
    if (optparse_hasopt (p, "mmap")) {
        uint32_t rank;
        if (flux_get_rank (ctx.h, &rank) < 0)
            log_err_exit ("error fetching broker rank");
        if (rank > 0)
            log_msg_exit ("--mmap only works on the rank 0 broker");
        if (optparse_hasopt (p, "preserve"))
            log_msg_exit ("--mmap cannot work with --preserve");
        if (optparse_hasopt (p, "no-force-primary"))
            log_msg_exit ("--mmap cannot work with --no-force-primary");
    }
    if (optparse_hasopt (p, "overwrite") && optparse_hasopt (p, "append"))
        log_msg_exit ("--overwrite and --append cannot be used together");

    if (!(s = flux_attr_get (ctx.h, "content.hash")))
        s = default_archive_hashtype;
    if (!(hashtype = strdup (s))) // 's' will not remain valid for long so copy
        log_msg_exit ("out of memory");
    ctx.param.hashtype = hashtype;
    if (asprintf (&key, "archive.%s", ctx.name) < 0)
        log_msg_exit ("out of memory");

    if (directory) {
        if (chdir (directory) < 0)
            log_err_exit ("chdir %s", directory);
    }

    /* Deal with pre-existing key.
     */
    if (optparse_hasopt (p, "overwrite")) {
        unlink_archive (ctx.h, ctx.namespace, ctx.name, true);
        unmap_archive (ctx.h, ctx.name);
    }
    else {
        flux_future_t *f;
        json_t *archive;

        if ((f = flux_kvs_lookup (ctx.h, ctx.namespace, 0, key))
            && flux_kvs_lookup_get_unpack (f, "o", &archive) == 0) {
            if (optparse_hasopt (p, "append")) {
                ctx.archive = json_incref (archive);
            }
            else
                log_msg_exit ("%s: key exists", key);
        }
        flux_future_destroy (f);
    }
    if (!ctx.archive) {
        if (!(ctx.archive = json_array ()))
            log_msg_exit ("out of memory");
    }

    /* Prepare KVS transaction.
     */
    if (!(ctx.txn = flux_kvs_txn_create ()))
        log_err_exit ("could not prepare KVS transaction");

    /* Iterate over PATHs and (recursively) their contents, building the
     * RFC 37 archive in 'ctx.archive'.
     */
    while (n < ac) {
        const char *path = av[n++];
        struct stat sb;

        if (lstat (path, &sb) < 0)
            log_err_exit ("%s", path);
        if (S_ISDIR (sb.st_mode)) {
            // archive_visitor() calls add_archive_file() for files under path
            if (dirwalk (path, flags, archive_visitor, &ctx) < 0)
                log_err_exit ("%s", path);
        }
        else {
            if (ctx.verbose > 0)
                printf ("%s\n", path);
            add_archive_file (&ctx, path);
        }
    }

    /* commit ctx.archive object to KVS
     */
    flux_future_t *f = NULL;
    if (flux_kvs_txn_pack (ctx.txn, 0, key, "O", ctx.archive) < 0
        || !(f = flux_kvs_commit (ctx.h, ctx.namespace, 0, ctx.txn))
        || flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("kvs commit: %s", future_strerror (f, errno));
    flux_future_destroy (f);

    flux_close (ctx.h);

    free (key);
    flux_kvs_txn_destroy (ctx.txn);
    free (hashtype);

    return 0;
}

static bool key_exists (flux_t *h, const char *namespace, const char *key)
{
    flux_future_t *f;
    bool exists = true;

    if (!(f = flux_kvs_lookup (h, namespace, 0, key))
        || flux_kvs_lookup_get (f, NULL) < 0)
        exists = false;
    flux_future_destroy (f);
    return exists;
}

/* Unlink archive.name and archive.name_blobs from the KVS.
 * If force is true, it is not an error if the keys do not exist.
 */
static void unlink_archive (flux_t *h,
                            const char *namespace,
                            const char *name,
                            bool force)
{
    flux_kvs_txn_t *txn;
    char *key_blobs = NULL;
    char *key = NULL;
    flux_future_t *f = NULL;

    if (asprintf (&key, "archive.%s", name) < 0)
        log_msg_exit ("out of memory");

    if (!force && !key_exists (h, namespace, key))
        log_msg_exit ("%s does not exist", key);

    if (!(txn = flux_kvs_txn_create ())
        || flux_kvs_txn_unlink (txn, 0, key) < 0
        || asprintf (&key_blobs, "archive.%s_blobs", name) < 0
        || flux_kvs_txn_unlink (txn, 0, key_blobs) < 0
        || !(f = flux_kvs_commit (h, namespace, 0, txn))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg ("unlink %s,%s: %s",
                 key,
                 key_blobs,
                 future_strerror (f, errno));
    }

    flux_future_destroy (f);
    free (key_blobs);
    free (key);
    flux_kvs_txn_destroy (txn);
}

/* Unmap files from the rank 0 content service.
 * It is not an error if the tag does not match any files.
 */
static void unmap_archive (flux_t *h, const char *name)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "content.mmap-remove",
                             0,
                             0,
                             "{s:s}",
                             "tag", name))
        || flux_rpc_get (f, NULL) < 0) {
        log_msg ("unmap %s: %s", name, future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

static int subcmd_remove (optparse_t *p, int ac, char *av[])
{
    const char *namespace = "primary";
    const char *name = optparse_get_str (p, "name", default_name);
    int n = optparse_option_index (p);
    flux_t *h;

    if (n < ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "no-force-primary"))
        namespace = NULL;
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    unlink_archive (h, namespace, name, optparse_hasopt (p, "force"));
    unmap_archive (h, name);

    flux_close (h);
    return 0;
}

/* Filter out archive entries that don't match 'pattern'.
 * This presumes the RFC 37 archive was stored in array form.  If this
 * is extended to support extracting files from jobspec, dictionary
 * support must be added.
 */
static void apply_glob (json_t *archive, const char *pattern)
{
    size_t index = 0;
    while (index < json_array_size (archive)) {
        json_t *entry;
        const char *path;

        if (!(entry = json_array_get (archive, index))
            || json_unpack (entry, "{s:s}", "path", &path) < 0
            || fnmatch (pattern, path, 0) != 0) {
            json_array_remove (archive, index);
            continue;
        }
        index++;
    }
    if (json_array_size (archive) == 0)
        log_msg ("No files matched pattern '%s'", pattern);
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
    int level = *(int *)arg;
    if (level > 0)
        fprintf (stderr, "%s\n", path);
}

static int subcmd_extract (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    const char *directory = optparse_get_str (p, "directory", NULL);
    const char *name = optparse_get_str (p, "name", default_name);
    const char *namespace = "primary";
    const char *pattern = NULL;
    int opts = 0;
    char *key;
    int kvs_flags = 0;
    flux_t *h;
    flux_future_t *f;
    double timeout = -1;
    json_t *archive;
    flux_error_t error;

    if (n < ac)
        pattern = av[n++];
    if (n < ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (asprintf (&key, "archive.%s", name) < 0)
        log_msg_exit ("out of memory");
    if (optparse_hasopt (p, "no-force-primary"))
        namespace = NULL;
    if (!optparse_hasopt (p, "overwrite"))
        opts |= ARCHIVE_EXTRACT_NO_OVERWRITE;
    if (optparse_hasopt (p, "waitcreate")) {
        kvs_flags |= FLUX_KVS_WAITCREATE;
        const char *arg = optparse_get_str (p, "waitcreate", NULL);
        if (arg && fsd_parse_duration (arg, &timeout) < 0)
            log_err_exit ("could not parse --waitcreate timeout");
    }
    if (directory) {
        if (chdir (directory) < 0)
            log_err_exit ("chdir %s", directory);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    /* Fetch the archive from KVS.
     * If --waitcreate, block until the key appears, or timeout is reached.
     */
    if (!(f = flux_kvs_lookup (h, namespace, kvs_flags, key)))
        log_err_exit ("error sending KVS lookup request");
    if (flux_future_wait_for (f, timeout) < 0 && errno == ETIMEDOUT)
        log_msg_exit ("%s: key was not created within timeout window", key);
    if (flux_kvs_lookup_get_unpack (f, "o", &archive) < 0)
        log_msg_exit ("KVS lookup %s: %s", key, future_strerror (f, errno));
    if (pattern)
        apply_glob (archive, pattern);

    /* List files (no extraction).
     */
    if (optparse_hasopt (p, "list-only")) {
        size_t index;
        json_t *entry;
        char buf[80];

        json_array_foreach (archive, index, entry) {
            fileref_pretty_print (entry,
                                  NULL,
                                  optparse_hasopt (p, "verbose"),
                                  buf,
                                  sizeof (buf));
            printf ("%s\n", buf);
        }
    }
    /* Extract files.
     * filemap_extract() fetches any content blobs referenced by large files.
     * This can fail if the instance was restarted and the archive was not
     * created with --preserve.
     */
    else {
        int level = optparse_get_int (p, "verbose", 0);
        if (filemap_extract (h, archive, opts, &error, trace_fn, &level) < 0)
            log_msg_exit ("%s", error.text);
    }

    free (key);
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

static int subcmd_list (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    const char *name = optparse_get_str (p, "name", default_name);
    const char *namespace = "primary";
    const char *pattern = NULL;
    char *key;
    flux_t *h;
    flux_future_t *f;
    json_t *archive;
    size_t index;
    json_t *entry;

    if (n < ac)
        pattern = av[n++];
    if (n < ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (asprintf (&key, "archive.%s", name) < 0)
        log_msg_exit ("out of memory");
    if (optparse_hasopt (p, "no-force-primary"))
        namespace = NULL;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_kvs_lookup (h, namespace, 0, key)))
        log_err_exit ("error sending KVS lookup request");
    if (flux_kvs_lookup_get_unpack (f, "o", &archive) < 0)
        log_msg_exit ("KVS lookup %s: %s", key, future_strerror (f, errno));
    if (pattern)
        apply_glob (archive, pattern);
    json_array_foreach (archive, index, entry) {
        if (optparse_hasopt (p, "raw")) {
            if (json_dumpf (entry, stdout, JSON_COMPACT) < 0)
                log_msg_exit ("error dumping RFC 37 file system object");
        }
        else {
            char buf[120];
            fileref_pretty_print (entry,
                                  NULL,
                                  optparse_hasopt (p, "long"),
                                  buf,
                                  sizeof (buf));
            printf ("%s\n", buf);
        }
    }
    free (key);
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

int cmd_archive (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-archive");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option create_opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Write to archive NAME (default main)", },
    { .name = "no-force-primary", .has_arg = 0,
      .usage = "Do not force archive to be in the primary KVS namespace", },
    { .name = "directory", .key = 'C', .has_arg = 1, .arginfo = "DIR",
      .usage = "Change to DIR before reading files", },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Increase output detail.", },
    { .name = "overwrite", .has_arg = 0,
      .usage = "Overwrite existing archive", },
    { .name = "append", .has_arg = 0,
      .usage = "Append to existing archive", },
    { .name = "preserve", .has_arg = 0,
      .usage = "Preserve data over Flux restart" },
    { .name = "mmap", .has_arg = 0,
      .usage = "Use mmap(2) to map file content" },
    { .name = "chunksize", .has_arg = 1, .arginfo = "N[KMG]",
      .usage = "Limit blob size to N bytes with 0=unlimited (default 1M)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    { .name = "small-file-threshold", .has_arg = 1, .arginfo = "N[KMG]",
      .usage = "Adjust the maximum size of a \"small file\" in bytes"
               " (default 1K)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option remove_opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Remove archive NAME (default main)", },
    { .name = "no-force-primary", .has_arg = 0,
      .usage = "Do not force archive to be in the primary KVS namespace", },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Ignore a nonexistent archive", },
    OPTPARSE_TABLE_END
};

static struct optparse_option extract_opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Read from archive NAME (default main)", },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Show filenames on stderr", },
    { .name = "directory", .key = 'C', .has_arg = 1, .arginfo = "DIR",
      .usage = "Change to DIR before extracting", },
    { .name = "overwrite", .has_arg = 0,
      .usage = "Overwrite existing files when extracting", },
    { .name = "waitcreate", .has_arg = 2, .arginfo = "[FSD]",
      .usage = "Wait for KVS archive key to appear (timeout optional)", },
    { .name = "no-force-primary", .has_arg = 0,
      .usage = "Do not force archive to be in the primary KVS namespace", },
    { .name = "list-only", .key = 't', .has_arg = 0,
      .usage = "List table of contents without extracting", },
    OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Read from archive NAME (default main)", },
    { .name = "no-force-primary", .has_arg = 0,
      .usage = "Do not force archive to be in the primary KVS namespace", },
    { .name = "long", .key = 'l', .has_arg = 0,
      .usage = "Show file type, mode, size", },
    { .name = "raw", .has_arg = 0,
      .usage = "Show raw RFC 37 file system object without decoding", },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand archive_subcmds[] = {
    { "create",
      "[-n NAME] [-C DIR] [--preserve] PATH ...",
      "Create a KVS file archive",
      subcmd_create,
      0,
      create_opts,
    },
    { "remove",
      "[-n NAME] [-f]",
      "Remove a KVS file archive",
      subcmd_remove,
      0,
      remove_opts,
    },
    { "extract",
      "[-n NAME] [--overwrite] [-C DIR] [PATTERN]",
      "Extract KVS file archive contents",
      subcmd_extract,
      0,
      extract_opts,
    },
    { "list",
      "[-n NAME] [PATTERN]",
      "List KVS file archive contents",
      subcmd_list,
      0,
      list_opts,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_archive_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
                                 "archive",
                                 cmd_archive,
                                 NULL,
                                 "Flux KVS file archive utility",
                                 0,
                                 NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "archive"),
                                  archive_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
