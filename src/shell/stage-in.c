/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* stage-in.c - copy previously mapped files for job */

#define FLUX_SHELL_PLUGIN_NAME "stage-in"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>
#include <jansson.h>
#include <argz.h>
#include <archive.h>
#include <archive_entry.h>
#include <flux/core.h>

#include "ccan/base64/base64.h"
#include "src/common/libcontent/content.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/fileref.h"

#include "builtins.h"
#include "internal.h"
#include "info.h"

struct stage_in {
    json_t *tags;
    const char *pattern;
    const char *destdir;
    flux_t *h;
    int count;
    size_t total_size;
    int direct;
};

json_t *parse_tags (const char *s, const char *default_value)
{
    char *argz = NULL;
    size_t argz_len;
    json_t *a;
    json_t *o;
    const char *entry;

    if (!(a = json_array ()))
        return NULL;
    if (s) {
        if (argz_create_sep (s, ',', &argz, &argz_len) != 0)
            goto error;
        entry = NULL;
        while ((entry = argz_next (argz, argz_len, entry))) {
            if (!(o = json_string (entry))
                || json_array_append_new (a, o) < 0) {
                json_decref (o);
                goto error;
            }
        }
    }
    if (json_array_size (a) == 0 && default_value) {
        if (!(o = json_string (default_value))
            || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }
    free (argz);
    return a;
error:
    free (argz);
    json_decref (a);
    return NULL;
}

/* Decode the raw data field a fileref object, setting the result in 'data'
 * and 'data_size'.  Caller must free.
 */
static int decode_data (const char *s, void **data, size_t *data_size)
{
    if (s) {
        int len = strlen (s);
        size_t bufsize = base64_decoded_length (len);
        void *buf;
        ssize_t n;

        if (!(buf = malloc (bufsize)))
            return -1;
        if ((n = base64_decode (buf, bufsize, s, len)) < 0) {
            free (buf);
            errno = EINVAL;
            return -1;
        }
        *data = buf;
        *data_size = n;
    }
    return 0;
}

static json_t *load_fileref (flux_t *h, const char *blobref)
{
    flux_future_t *f;
    const void *buf;
    int size;
    json_t *o;
    json_error_t error;

    if (!(f = content_load_byblobref (h, blobref, 0))
        || content_load_get (f, &buf, &size) < 0) {
        shell_log_error ("error loading fileref from %s: %s",
                          blobref,
                          future_strerror (f, errno));
        flux_future_destroy (f);
        return NULL;
    }
    if (!(o = json_loads (buf, 0, &error))) {
        shell_log_error ("error decoding fileref object from %s: %s",
                         blobref,
                         error.text);
        flux_future_destroy (f);
        return NULL;
    }
    flux_future_destroy (f);
    return o;
}

static flux_future_t *mmap_list (flux_t *h,
                                 bool blobref,
                                 json_t *tags,
                                 const char *pattern)
{
    flux_future_t *f;

    if (pattern) {
        f = flux_rpc_pack (h,
                           "content.mmap-list",
                           0,
                           FLUX_RPC_STREAMING,
                           "{s:b s:s s:O}",
                           "blobref", blobref ? 1 : 0,
                           "pattern", pattern,
                           "tags", tags);
    }
    else {
        f = flux_rpc_pack (h,
                           "content.mmap-list",
                           0,
                           FLUX_RPC_STREAMING,
                           "{s:b s:O}",
                           "blobref", blobref ? 1 : 0,
                           "tags", tags);
    }
    return f;
}

static int extract_blob (flux_t *h,
                         struct archive *archive,
                         const char *path,
                         json_t *o)
{
    struct {
        json_int_t offset;
        json_int_t size;
        const char *blobref;
    } entry;
    flux_future_t *f;
    const void *buf;
    int size;
    int rc = -1;

    if (json_unpack (o,
                     "[I,I,s]",
                     &entry.offset,
                     &entry.size,
                     &entry.blobref) < 0) {
        shell_log_error ("%s: error decoding blobvec entry", path);
        return -1;
    }
    if (!(f = content_load_byblobref (h, entry.blobref, 0))
        || content_load_get (f, &buf, &size) < 0) {
        shell_log_error ("%s: error loading offset=%ju size=%ju from %s: %s",
                         path,
                         (uintmax_t)entry.offset,
                         (uintmax_t)entry.size,
                         entry.blobref,
                         future_strerror (f, errno));
        goto done;
    }
    if (size != entry.size) {
        shell_log_error ("%s: error loading offset=%ju size=%ju from %s:"
                         " unexpected size %ju",
                         path,
                         (uintmax_t)entry.offset,
                         (uintmax_t)entry.size,
                         entry.blobref,
                         (uintmax_t)size);
        goto done;
    }
    if (archive_write_data_block (archive,
                                  buf,
                                  size,
                                  entry.offset) != ARCHIVE_OK) {
        shell_log_error ("%s: write: %s", path, archive_error_string (archive));
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int extract_file (struct stage_in *ctx,
                         struct archive *archive,
                         json_t *fileref)
{
    const char *path;
    json_int_t size;
    json_int_t ctime;
    json_int_t mtime;
    int mode;
    json_t *blobvec;
    const char *data = NULL;
    size_t index;
    json_t *o;
    struct archive_entry *entry;
    char tracebuf[1024];
    json_error_t error;

    fileref_pretty_print (fileref, true, tracebuf, sizeof (tracebuf));
    shell_trace ("%s", tracebuf);

    if (json_unpack_ex (fileref,
                        &error,
                        0,
                        "{s:s s:I s:I s:I s:i s?s s:o}",
                        "path", &path,
                        "size", &size,
                        "mtime", &mtime,
                        "ctime", &ctime,
                        "mode", &mode,
                        "data", &data,
                        "blobvec", &blobvec) < 0) {
        shell_log_error ("error decoding fileref object: %s", error.text);
        return -1;
    }
    ctx->total_size += size;

    /* metadata
     */
    if (!(entry = archive_entry_new ())) {
        shell_log_error ("%s: error creating libarchive entry", path);
        return -1;
    }
    archive_entry_set_pathname (entry, path);
    archive_entry_set_mode (entry, mode);
    archive_entry_set_mtime (entry, mtime, 0);
    archive_entry_set_ctime (entry, ctime, 0);
    if (S_ISREG (mode)) {
        archive_entry_set_size (entry, size);
    }
    else if (S_ISLNK (mode)) {
        if (!data) {
            shell_log_error ("%s: missing symlink data", path);
            goto error;
        }
        archive_entry_set_symlink (entry, data);
    }
    else if (!S_ISDIR (mode)) { // nothing to do for directory
        shell_log_error ("%s: unknown file type (mode=0%o)", path, mode);
        goto error;
    }

    if (archive_write_header (archive, entry) != ARCHIVE_OK) {
        shell_log_error ("%s: %s", path, archive_error_string (archive));
        goto error;
    }

    /* data
     */
    if (S_ISREG (mode)) {
        if (data) { // small file is contained in fileref.data
            void *buf;
            size_t buf_size;

            if (decode_data (data, &buf, &buf_size) < 0) {
                shell_log_error ("%s: could not decode file data", path);
                goto error;
            }
            if (archive_write_data_block (archive,
                                          buf,
                                          buf_size,
                                          0) != ARCHIVE_OK) {
                shell_log_error ("%s: write: %s",
                                 path,
                                 archive_error_string (archive));
                free (buf);
                goto error;
            }
            free (buf);
        }
        else { // large file is spread over multiple blobrefs
            json_array_foreach (blobvec, index, o) {
                if (extract_blob (ctx->h, archive, path, o) < 0)
                    goto error;
            }
        }
    }
    archive_entry_free (entry);
    return 0;
error:
    archive_entry_free (entry);
    return -1;

}

static int extract (struct stage_in *ctx, struct archive *archive)
{
    flux_future_t *f;
    size_t index;
    json_t *entry;

    if (!(f = mmap_list (ctx->h,
                         ctx->direct ? false : true,
                         ctx->tags,
                         ctx->pattern))) {
        shell_log_error ("mmap-list: %s", strerror (errno));
        return -1;
    }
    for (;;) {
        json_t *files;

        if (flux_rpc_get_unpack (f, "{s:o}", "files", &files) < 0) {
            if (errno == ENODATA)
                break; // end of stream
            shell_log_error ("mmap-list: %s", future_strerror (f, errno));
            return -1;
        }
        json_array_foreach (files, index, entry) {
            if (ctx->direct) {
                if (extract_file (ctx, archive, entry) < 0)
                    return -1;
            }
            else {
                json_t *fileref;

                if (!(fileref = load_fileref (ctx->h,
                                              json_string_value (entry))))
                    return -1;
                if (extract_file (ctx, archive, fileref) < 0) {
                    json_decref (fileref);
                    return -1;
                }
                json_decref (fileref);
            }
            ctx->count++;
        }
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    return 0;
}

static int extract_files (struct stage_in *ctx)
{
    char *orig_dir;
    struct archive *archive = NULL;
    struct timespec t;
    int rc = -1;

    if (!(orig_dir = getcwd (NULL, 0))) {
        shell_log_error ("getcwd: %s", strerror (errno));
        return -1;
    }
    if (!(archive = archive_write_disk_new ())) {
        shell_log_error ("error creating libarchive context");
        goto done;
    }
    if (chdir (ctx->destdir) < 0) {
        shell_log_error ("chdir %s: %s", ctx->destdir, strerror (errno));
        goto done;
    }
    shell_debug ("=> %s", ctx->destdir);
    monotime (&t);
    if (extract (ctx, archive) == 0) {
        double elapsed = monotime_since (t) / 1000;
        shell_debug ("%d files %.1fMB/s",
                     ctx->count,
                     1E-6 * ctx->total_size / elapsed);
        rc = 0;
    }
done:
    if (chdir (orig_dir) < 0) {
        shell_die (1,
                   "could not chdir back to original directory %s: %s",
                   orig_dir,
                   strerror (errno));
    }
    if (archive)
        archive_write_free (archive);
    free (orig_dir);
    return rc;
}

static int stage_in (flux_shell_t *shell, json_t *config)
{
    struct stage_in ctx;
    const char *tags = NULL;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = shell->h;

    if (json_is_object (config)) {
        if (json_unpack (config,
                         "{s?s s?s s?s s?i}",
                         "tags", &tags,
                         "pattern", &ctx.pattern,
                         "destdir", &ctx.destdir,
                         "direct", &ctx.direct)) {
            shell_log_error ("Error parsing stage_in shell option");
            goto error;
        }
    }
    if (!(ctx.tags = parse_tags (tags, "main"))) {
        shell_log_error ("Error parsing stage_in.tags shell option");
        goto error;
    }
    if (!ctx.destdir) {
        ctx.destdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR");
        if (!ctx.destdir) {
            shell_log_error ("FLUX_JOB_TMPDIR is not set");
            goto error;
        }
    }
    if (extract_files (&ctx) < 0)
        goto error;

    json_decref (ctx.tags);
    return 0;
error:
    json_decref (ctx.tags);
    return -1;
}

static int stage_in_init (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    json_t *config = NULL;

    if (flux_shell_getopt_unpack (shell, "stage-in", "o", &config) < 0)
        return -1;
    if (!config)
        return 0;
    return stage_in (shell, config);
}

struct shell_builtin builtin_stage_in = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = stage_in_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
