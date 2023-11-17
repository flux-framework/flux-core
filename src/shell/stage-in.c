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
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <archive.h>
#include <archive_entry.h>
#include <flux/core.h>

#include "ccan/base64/base64.h"
#include "ccan/str/str.h"
#include "src/common/libcontent/content.h"
#include "src/common/libfilemap/filemap.h"
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

static void trace_cb (void *arg,
                      json_t *fileref,
                      const char *path,
                      int mode,
                      int64_t size,
                      int64_t mtime,
                      int64_t ctime,
                      const char *encoding)
{
    struct stage_in *ctx = arg;
    char buf[1024];
    ctx->count++;
    if (size != -1)
        ctx->total_size += size;
    fileref_pretty_print (fileref, NULL, true, buf, sizeof (buf));
    shell_trace ("%s", buf);
}

static int extract (struct stage_in *ctx)
{
    flux_future_t *f;

    if (!(f = filemap_mmap_list (ctx->h,
                                 !ctx->direct,
                                 ctx->tags,
                                 ctx->pattern))) {
        shell_log_error ("mmap-list: %s", strerror (errno));
        return -1;
    }
    for (;;) {
        json_t *files;
        flux_error_t error;

        if (flux_rpc_get_unpack (f, "{s:o}", "files", &files) < 0) {
            if (errno == ENODATA)
                break; // end of stream
            shell_log_error ("mmap-list: %s", future_strerror (f, errno));
            return -1;
        }
        if (filemap_extract (ctx->h,
                             files,
                             ctx->direct,
                             &error,
                             trace_cb,
                             ctx) < 0) {
            shell_log_error ("%s", error.text);
            return -1;
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
    if (chdir (ctx->destdir) < 0) {
        shell_log_error ("chdir %s: %s", ctx->destdir, strerror (errno));
        goto done;
    }
    shell_debug ("=> %s", ctx->destdir);
    monotime (&t);
    if (extract (ctx) == 0) {
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
    const char *destination = NULL;
    bool leader_only = false;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = shell->h;

    if (json_is_object (config)) {
        if (json_unpack (config,
                         "{s?s s?s s?s s?i}",
                         "tags", &tags,
                         "pattern", &ctx.pattern,
                         "destination", &destination,
                         "direct", &ctx.direct)) {
            shell_log_error ("Error parsing stage_in shell option");
            goto error;
        }
    }
    if (!(ctx.tags = parse_tags (tags, "main"))) {
        shell_log_error ("Error parsing stage_in.tags shell option");
        goto error;
    }
    if (destination) {
        if (strstarts (destination, "local:"))
            ctx.destdir = destination + 6;
        else if (strstarts (destination, "global:")) {
            ctx.destdir = destination + 7;
            leader_only = true;
        }
        else if (strchr (destination, ':') == NULL)
            ctx.destdir = destination;
        else {
            shell_log_error ("destination prefix must be local: or global:");
            goto error;
        }
    }
    if (!ctx.destdir) {
        ctx.destdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR");
        if (!ctx.destdir) {
            shell_log_error ("FLUX_JOB_TMPDIR is not set");
            goto error;
        }
    }
    if (shell->info->shell_rank == 0 || leader_only == false) {
        if (extract_files (&ctx) < 0)
            goto error;
    }

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
