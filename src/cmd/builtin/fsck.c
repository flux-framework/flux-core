/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <time.h>
#include <stdarg.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "builtin.h"

#define BLOBREF_ASYNC_MAX 1000

struct fsck_ctx {
    flux_t *h;
    json_t *root;
    bool verbose;
    bool quiet;
    int errorcount;
};

struct fsck_valref_data
{
    struct fsck_ctx *ctx;
    json_t *treeobj;
    int index;
    int count;
    int in_flight;
    const char *path;
    int errorcount;
    int errnum;
    zlist_t *missing_indexes;
};

static void fsck_treeobj (struct fsck_ctx *ctx,
                          const char *path,
                          json_t *treeobj);

static void valref_validate (struct fsck_valref_data *fvd,
                             json_t *treeobj,
                             int index);

static void read_verror (const char *fmt, va_list ap)
{
    char buf[128];
    vsnprintf (buf, sizeof (buf), fmt, ap);
    fprintf (stderr, "%s\n", buf);
}

static __attribute__ ((format (printf, 2, 3)))
void read_error (struct fsck_ctx *ctx, const char *fmt, ...)
{
    va_list ap;
    if (ctx->quiet)
        return;
    va_start (ap, fmt);
    read_verror (fmt, ap);
    va_end (ap);
}

static void save_missing_ref_index (struct fsck_valref_data *fvd, int index)
{
    int *cpy;

    if (!fvd->missing_indexes) {
        if (!(fvd->missing_indexes = zlist_new ()))
            log_err_exit ("cannot create missing indexes list");
    }

    if (!(cpy = malloc (sizeof (index))))
        log_err_exit ("cannot allocate memory for index");
    *cpy = index;

    if (zlist_append (fvd->missing_indexes, cpy) < 0)
        log_err_exit ("cannot append index to list");
    zlist_freefn (fvd->missing_indexes, cpy, (zlist_free_fn *) free, true);
}

static void valref_validate_continuation (flux_future_t *f, void *arg)
{
    struct fsck_valref_data *fvd = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        int *index = flux_future_aux_get (f, "index");
        if (fvd->ctx->verbose) {
            if (errno == ENOENT)
                read_error (fvd->ctx,
                            "%s: missing blobref index=%d",
                            fvd->path,
                            (*index));
            else
                read_error (fvd->ctx,
                            "%s: error retrieving blobref index=%d: %s",
                            fvd->path,
                            (*index),
                            future_strerror (f, errno));
        }
        fvd->errorcount++;
        fvd->errnum = errno;     /* we'll report the last errno */
        if (errno == ENOENT)
            save_missing_ref_index (fvd, *index);
    }
    fvd->in_flight--;

    if (fvd->index < fvd->count) {
        valref_validate (fvd, fvd->treeobj, fvd->index);
        fvd->in_flight++;
        fvd->index++;
    }

    flux_future_destroy (f);
}

static void valref_validate (struct fsck_valref_data *fvd,
                             json_t *treeobj,
                             int index)
{
    uint32_t hash[BLOBREF_MAX_DIGEST_SIZE];
    ssize_t hash_size;
    const char *blobref;
    flux_future_t *f;
    int *indexp;

    blobref = treeobj_get_blobref (treeobj, index);

    if ((hash_size = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        log_err_exit ("cannot get hash from ref string");

    if (!(f = flux_rpc_raw (fvd->ctx->h,
                            "content-backing.validate",
                            hash,
                            hash_size,
                            0,
                            0))
        || flux_future_then (f, -1, valref_validate_continuation, fvd) < 0)
        log_err_exit ("cannot validate valref blob");
    if (!(indexp = (int *)malloc (sizeof (int))))
        log_err_exit ("cannot allocate index memory");
    (*indexp) = index;
    if (flux_future_aux_set (f, "index", indexp, free) < 0)
        log_err_exit ("could not save index value");
}

static void fsck_valref (struct fsck_ctx *ctx,
                         const char *path,
                         json_t *treeobj)
{
    struct fsck_valref_data fvd = {0};

    fvd.ctx = ctx;
    fvd.treeobj = treeobj;
    fvd.count = treeobj_get_count (treeobj);
    fvd.path = path;

    while (fvd.in_flight < BLOBREF_ASYNC_MAX
           && fvd.index < fvd.count) {
        valref_validate (&fvd, treeobj, fvd.index);
        fvd.in_flight++;
        fvd.index++;
    }

    if (flux_reactor_run (flux_get_reactor (ctx->h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (fvd.errorcount) {
        /* each invalid blobref will be output in verbose mode */
        if (!ctx->verbose) {
            if (fvd.errnum == ENOENT)
                read_error (ctx, "%s: missing blobref(s)", path);
            else
                read_error (ctx,
                            "%s: error retrieving blobref(s): %s",
                            path,
                            strerror (fvd.errnum));
        }
        ctx->errorcount++;
    }

    zlist_destroy (&fvd.missing_indexes);
}


static void fsck_val (struct fsck_ctx *ctx,
                      const char *path,
                      json_t *treeobj)
{
    /* Do nothing for now */
}

static void fsck_symlink (struct fsck_ctx *ctx,
                          const char *path,
                          json_t *treeobj)
{
    /* Do nothing for now */
}

static void fsck_dir (struct fsck_ctx *ctx,
                      const char *path,
                      json_t *treeobj)
{
    json_t *dict = treeobj_get_data (treeobj);
    const char *name;
    json_t *entry;

    json_object_foreach (dict, name, entry) {
        char *newpath;
        if (asprintf (&newpath, "%s.%s", path, name) < 0)
            log_msg_exit ("out of memory");
        fsck_treeobj (ctx, newpath, entry); // recurse
        free (newpath);
    }
}

static void fsck_dirref (struct fsck_ctx *ctx,
                         const char *path,
                         json_t *treeobj)
{
    flux_future_t *f = NULL;
    const void *buf;
    size_t buflen;
    json_t *treeobj_deref = NULL;
    int count;

    count = treeobj_get_count (treeobj);
    if (count != 1) {
        read_error (ctx,
                    "%s: invalid dirref treeobj count=%d",
                    path,
                    count);
        ctx->errorcount++;
        return;
    }
    if (!(f = content_load_byblobref (ctx->h,
                                      treeobj_get_blobref (treeobj, 0),
                                      CONTENT_FLAG_CACHE_BYPASS))
        || content_load_get (f, &buf, &buflen) < 0) {
        if (errno == ENOENT)
            read_error (ctx, "%s: missing dirref blobref", path);
        else
            read_error (ctx,
                        "%s: error retrieving dirref blobref: %s",
                        path,
                        future_strerror (f, errno));
        ctx->errorcount++;
        flux_future_destroy (f);
        return;
    }
    if (!(treeobj_deref = treeobj_decodeb (buf, buflen))) {
        read_error (ctx, "%s: could not decode directory", path);
        ctx->errorcount++;
        goto cleanup;
    }
    if (!treeobj_is_dir (treeobj_deref)) {
        read_error (ctx, "%s: dirref references non-directory", path);
        ctx->errorcount++;
        goto cleanup;
    }
    fsck_dir (ctx, path, treeobj_deref); // recurse
cleanup:
    json_decref (treeobj_deref);
    flux_future_destroy (f);
}

static void fsck_treeobj (struct fsck_ctx *ctx,
                          const char *path,
                          json_t *treeobj)
{
    if (treeobj_validate (treeobj) < 0) {
        read_error (ctx, "%s: invalid tree object", path);
        ctx->errorcount++;
        return;
    }
    if (ctx->verbose)
        fprintf (stderr, "%s\n", path);
    if (treeobj_is_symlink (treeobj))
        fsck_symlink (ctx, path, treeobj);
    else if (treeobj_is_val (treeobj))
        fsck_val (ctx, path, treeobj);
    else if (treeobj_is_valref (treeobj))
        fsck_valref (ctx, path, treeobj);
    else if (treeobj_is_dirref (treeobj))
        fsck_dirref (ctx, path, treeobj); // recurse
    else if (treeobj_is_dir (treeobj))
        fsck_dir (ctx, path, treeobj); // recurse
}

static void fsck_blobref (struct fsck_ctx *ctx, const char *blobref)
{
    flux_future_t *f;
    const void *buf;
    size_t buflen;
    json_t *dict;
    const char *key;
    json_t *entry;

    if (!(f = content_load_byblobref (ctx->h,
                                      blobref,
                                      CONTENT_FLAG_CACHE_BYPASS))
        || content_load_get (f, &buf, &buflen) < 0) {
        read_error (ctx,
                    "cannot load root tree object: %s",
                    future_strerror (f, errno));
        ctx->errorcount++;
        flux_future_destroy (f);
        return;
    }
    if (!(ctx->root = treeobj_decodeb (buf, buflen))
        || treeobj_validate (ctx->root) < 0)
        log_msg_exit ("blobref does not refer to a valid RFC 11 tree object");
    if (!treeobj_is_dir (ctx->root))
        log_msg_exit ("root tree object is not a directory");

    dict = treeobj_get_data (ctx->root);
    json_object_foreach (dict, key, entry) {
        fsck_treeobj (ctx, key, entry);
    }
    flux_future_destroy (f);
}

static bool kvs_is_running (struct fsck_ctx *ctx)
{
	flux_future_t *f;
	bool running = true;

	if ((f = flux_kvs_getroot (ctx->h, NULL, 0)) != NULL
		&& flux_rpc_get (f, NULL) < 0
		&& errno == ENOSYS)
		running = false;
	flux_future_destroy (f);
	return running;
}

static int cmd_fsck (optparse_t *p, int ac, char *av[])
{
    struct fsck_ctx ctx = {0};
    int optindex = optparse_option_index (p);
    flux_future_t *f = NULL;
    const char *blobref;

    log_init ("flux-fsck");

    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (optparse_hasopt (p, "verbose"))
        ctx.verbose = true;
    if (optparse_hasopt (p, "quiet"))
        ctx.quiet = true;

    ctx.h = builtin_get_flux_handle (p);

    if (kvs_is_running (&ctx))
        log_msg_exit ("please unload kvs module before using flux-fsck");

    if ((blobref = optparse_get_str (p, "rootref", NULL))) {
        if (blobref_validate (blobref) < 0)
            log_msg_exit ("invalid blobref specified");
    }
    else {
        const json_t *checkpoints;
        json_t *checkpt;
        double timestamp;

        /* index 0 is most recent checkpoint */
        if (!(f = kvs_checkpoint_lookup (ctx.h, KVS_CHECKPOINT_FLAG_CACHE_BYPASS))
            || kvs_checkpoint_lookup_get (f, &checkpoints) < 0
            || !(checkpt = json_array_get (checkpoints, 0))
            || kvs_checkpoint_parse_rootref (checkpt, &blobref) < 0
            || kvs_checkpoint_parse_timestamp (checkpt, &timestamp) < 0)
            log_msg_exit ("error fetching checkpoints: %s",
                          future_strerror (f, errno));

        if (!ctx.quiet) {
            char buf[64] = "";
            struct tm tm;
            if (!timestamp_from_double (timestamp, &tm, NULL))
                strftime (buf, sizeof (buf), "%Y-%m-%dT%T", &tm);
            fprintf (stderr,
                     "Checking integrity of checkpoint from %s\n",
                     buf);
        }
    }

    fsck_blobref (&ctx, blobref);

    flux_future_destroy (f);

    if (!ctx.quiet)
        fprintf (stderr, "Total errors: %d\n", ctx.errorcount);

    json_decref (ctx.root);
    flux_close (ctx.h);

    return (ctx.errorcount ? -1 : 0);
}

static struct optparse_option fsck_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "List keys as they are being validated",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Don't output diagnostic messages and discovered errors",
    },
    { .name = "rootref", .key = 'r', .has_arg = 1, .arginfo = "BLOBREF",
      .usage = "Check integrity starting with BLOBREF",
    },
    OPTPARSE_TABLE_END
};

int subcommand_fsck_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "fsck",
        cmd_fsck,
        "[OPTIONS]",
        "check integrity of content store data",
        0,
        fsck_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
