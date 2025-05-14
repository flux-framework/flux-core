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
#include "ccan/str/str.h"

#include "builtin.h"

static void fsck_treeobj (flux_t *h,
                          const char *path,
                          json_t *treeobj);

static bool verbose;
static bool quiet;
static int errorcount;

static void read_verror (const char *fmt, va_list ap)
{
    char buf[128];
    vsnprintf (buf, sizeof (buf), fmt, ap);
    fprintf (stderr, "%s\n", buf);
}

static __attribute__ ((format (printf, 1, 2)))
void read_error (const char *fmt, ...)
{
    va_list ap;
    if (quiet)
        return;
    va_start (ap, fmt);
    read_verror (fmt, ap);
    va_end (ap);
}

static void fsck_valref (flux_t *h,
                         const char *path,
                         json_t *treeobj)
{
    int count = treeobj_get_count (treeobj);
    const void *buf;
    size_t buflen;

    for (int i = 0; i < count; i++) {
        flux_future_t *f;
        if (!(f = content_load_byblobref (h,
                                          treeobj_get_blobref (treeobj, i),
                                          CONTENT_FLAG_CACHE_BYPASS))
            || content_load_get (f, &buf, &buflen) < 0) {
            if (errno == ENOENT)
                read_error ("%s: missing blobref index=%d",
                            path,
                            i);
            else
                read_error ("%s: error retrieving blobref index=%d: %s",
                            path,
                            i,
                            future_strerror (f, errno));
            errorcount++;
            flux_future_destroy (f);
            return;
        }
        flux_future_destroy (f);
    }
}

static void fsck_val (flux_t *h,
                      const char *path,
                      json_t *treeobj)
{
    /* Do nothing for now */
}

static void fsck_symlink (flux_t *h,
                          const char *path,
                          json_t *treeobj)
{
    /* Do nothing for now */
}

static void fsck_dir (flux_t *h,
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
        fsck_treeobj (h, newpath, entry); // recurse
        free (newpath);
    }
}

static void fsck_dirref (flux_t *h,
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
        read_error ("%s: invalid dirref treeobj count=%d",
                    path,
                    count);
        errorcount++;
        return;
    }
    if (!(f = content_load_byblobref (h,
                                      treeobj_get_blobref (treeobj, 0),
                                      CONTENT_FLAG_CACHE_BYPASS))
        || content_load_get (f, &buf, &buflen) < 0) {
        if (errno == ENOENT)
            read_error ("%s: missing dirref blobref", path);
        else
            read_error ("%s: error retrieving dirref blobref: %s",
                        path,
                        future_strerror (f, errno));
        errorcount++;
        flux_future_destroy (f);
        return;
    }
    if (!(treeobj_deref = treeobj_decodeb (buf, buflen))) {
        read_error ("%s: could not decode directory", path);
        errorcount++;
        goto cleanup;
    }
    if (!treeobj_is_dir (treeobj_deref)) {
        read_error ("%s: dirref references non-directory", path);
        errorcount++;
        goto cleanup;
    }
    fsck_dir (h, path, treeobj_deref); // recurse
cleanup:
    json_decref (treeobj_deref);
    flux_future_destroy (f);
}

static void fsck_treeobj (flux_t *h,
                          const char *path,
                          json_t *treeobj)
{
    if (treeobj_validate (treeobj) < 0) {
        read_error ("%s: invalid tree object", path);
        errorcount++;
        return;
    }
    if (treeobj_is_symlink (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        fsck_symlink (h, path, treeobj);
    }
    else if (treeobj_is_val (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        fsck_val (h, path, treeobj);
    }
    else if (treeobj_is_valref (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        fsck_valref (h, path, treeobj);
    }
    else if (treeobj_is_dirref (treeobj)) {
        fsck_dirref (h, path, treeobj); // recurse
    }
    else if (treeobj_is_dir (treeobj)) {
        fsck_dir (h, path, treeobj); // recurse
    }
}

static void fsck_blobref (flux_t *h, const char *blobref)
{
    flux_future_t *f;
    const void *buf;
    size_t buflen;
    json_t *treeobj;
    json_t *dict;
    const char *key;
    json_t *entry;

    if (!(f = content_load_byblobref (h, blobref, CONTENT_FLAG_CACHE_BYPASS))
        || content_load_get (f, &buf, &buflen) < 0) {
        read_error ("cannot load root tree object: %s",
                    future_strerror (f, errno));
        errorcount++;
        flux_future_destroy (f);
        return;
    }
    if (!(treeobj = treeobj_decodeb (buf, buflen)))
        log_err_exit ("cannot decode root tree object");
    if (treeobj_validate (treeobj) < 0)
        log_msg_exit ("invalid root tree object");
    if (!treeobj_is_dir (treeobj))
        log_msg_exit ("root tree object is not a directory");

    dict = treeobj_get_data (treeobj);
    json_object_foreach (dict, key, entry) {
        fsck_treeobj (h, key, entry);
    }
    json_decref (treeobj);
    flux_future_destroy (f);
}

static int cmd_fsck (optparse_t *p, int ac, char *av[])
{
    int optindex =  optparse_option_index (p);
    flux_future_t *f;
    const char *blobref;
    double timestamp;
    flux_t *h;

    log_init ("flux-fsck");

    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (optparse_hasopt (p, "verbose"))
        verbose = true;
    if (optparse_hasopt (p, "quiet"))
        quiet = true;

    h = builtin_get_flux_handle (p);

    if (!(f = kvs_checkpoint_lookup (h,
                                     NULL,
                                     KVS_CHECKPOINT_FLAG_CACHE_BYPASS))
        || kvs_checkpoint_lookup_get_timestamp (f, &timestamp) < 0
        || kvs_checkpoint_lookup_get_rootref (f, &blobref) < 0)
        log_msg_exit ("error fetching checkpoint: %s",
                      future_strerror (f, errno));
    if (!quiet) {
        char buf[64] = "";
        struct tm tm;
        if (!timestamp_from_double (timestamp, &tm, NULL))
            strftime (buf, sizeof (buf), "%Y-%m-%dT%T", &tm);
        fprintf (stderr,
                 "Checking integrity of checkpoint from %s\n",
                 buf);
    }

    fsck_blobref (h, blobref);

    flux_future_destroy (f);

    flux_close (h);

    if (!quiet)
        fprintf (stderr, "Total errors: %d\n", errorcount);
    return (errorcount ? -1 : 0);
}

static struct optparse_option fsck_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "List keys as they are being validated",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Don't output diagnostic messages and discovered errors",
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
