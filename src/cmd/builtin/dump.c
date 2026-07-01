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
# include <config.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <time.h>
#include <stdarg.h>
#include <archive.h>
#include <archive_entry.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libkvs/kvs_treewalk.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "builtin.h"

/* Passed as the kvs_treewalk callback argument. */
struct dump {
    flux_t *h;
    struct archive *ar;
};

static bool sd_notify_flag;
static bool verbose;
static bool quiet;
static bool ignore_failed_read;
static int content_flags;
static time_t dump_time;
static gid_t dump_gid;
static uid_t dump_uid;
static int keycount;
static int async_max;

static void read_verror (const char *fmt, va_list ap)
{
    char buf[128];
    vsnprintf (buf, sizeof (buf), fmt, ap);
    fprintf (stderr, "%s\n", buf);
    if (!ignore_failed_read)
        exit (1);
}

static __attribute__ ((format (printf, 1, 2)))
void read_error (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    read_verror (fmt, ap);
    va_end (ap);
}

static void progress_notify (flux_t *h)
{
    flux_future_t *f;
    char buf[64];

    snprintf (buf,
              sizeof (buf),
              "flux-dump(1) has archived %d keys",
              keycount);
    f = flux_rpc_pack (h,
                       "state-machine.sd-notify",
                       FLUX_NODEID_ANY,
                       FLUX_RPC_NORESPONSE,
                       "{s:s}",
                       "status", buf);
    flux_future_destroy (f);
}

static void progress (flux_t *h, int delta_keys)
{
    static int last_keycount = 0;

    keycount += delta_keys;

    if (last_keycount == keycount
        || !(keycount % 100 == 0 || keycount < 10))
        return;

    if (!verbose && !quiet)
        fprintf (stderr, "\rflux-dump: archived %d keys", keycount);
    if (sd_notify_flag)
        progress_notify (h);

    last_keycount = keycount;
}

static void progress_end (flux_t *h)
{
    if (!quiet && !verbose)
        fprintf (stderr, "\rflux-dump: archived %d keys\n", keycount);
    if (sd_notify_flag)
        progress_notify (h);
}

static struct archive *dump_create (const char *outfile)
{
    struct archive *ar;

    if (!(ar = archive_write_new ()))
        log_msg_exit ("error creating libarchive write context");
    if (streq (outfile, "-")) {
        if (archive_write_set_format_pax_restricted (ar) != ARCHIVE_OK
            || archive_write_open_FILE (ar, stdout) != ARCHIVE_OK)
            log_msg_exit ("%s", archive_error_string (ar));
    }
    else {
#if ARCHIVE_VERSION_NUMBER < 3002000
        /* El7 has libarchive 3.1.2 (circa 2013), but
         * archive_write_set_format_filter_by_ext() appeared in 3.2.0.
         * Just force tar format / no compression on that platform.
         */
        if (archive_write_set_format_pax_restricted (ar) != ARCHIVE_OK
#else
        if (archive_write_set_format_filter_by_ext (ar, outfile) != ARCHIVE_OK
#endif
            || archive_write_open_filename (ar, outfile) != ARCHIVE_OK)
            log_msg_exit ("%s", archive_error_string (ar));
    }
    return ar;
}

static void dump_destroy (struct archive *ar)
{
    if (archive_write_close (ar) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    archive_write_free (ar);
}

/* From archive_write_data(3):
 *   Clients should treat any value less than zero as an error and consider
 *   any non-negative value as success.
 */
static void dump_write_data (struct archive *ar, const void *data, int size)
{
    int n;

    n = archive_write_data (ar, data, size);
    if (n < 0)
        log_msg_exit ("%s", archive_error_string (ar));
    if (n != size)
        log_msg ("short write to archive: %s",
                 "assuming non-fatal libarchive write size reporting error");
}

/* kvs_treewalk valref_done callback: write a valref archive entry from the
 * array of completed content.load responses.  The total size of the value
 * must be calculated and written in the entry header before any data, so all
 * blobs are accumulated by the walker before this is called.  A blob load
 * error (only possible with --ignore-failed-read, else the walker's error
 * path would have exited) causes the whole entry to be skipped.
 */
static void dump_valref (void *arg,
                         const char *path,
                         json_t *treeobj,
                         int count,
                         const struct kvs_treewalk_blob *blobs)
{
    struct dump *d = arg;
    struct archive *ar = d->ar;
    struct archive_entry *entry;
    int total_size = 0;

    for (int i = 0; i < count; i++) {
        size_t len;
        if (blobs[i].errnum) {
            read_error ("%s: missing blobref %d: %s",
                        path,
                        i,
                        strerror (blobs[i].errnum));
            return;
        }
        if (flux_response_decode_raw (blobs[i].msg, NULL, NULL, &len) < 0)
            log_err_exit ("error processing stashed valref responses");
        total_size += len;
    }

    if (!(entry = archive_entry_new ()))
        log_msg_exit ("error creating archive entry");
    archive_entry_set_pathname (entry, path);
    archive_entry_set_size (entry, total_size);
    archive_entry_set_perm (entry, 0644);
    archive_entry_set_filetype (entry, AE_IFREG);
    archive_entry_set_mtime (entry, dump_time, 0);
    archive_entry_set_uid (entry, dump_uid);
    archive_entry_set_gid (entry, dump_gid);

    if (archive_write_header (ar, entry) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    for (int i = 0; i < count; i++) {
        const void *data;
        size_t len;
        if (flux_response_decode_raw (blobs[i].msg, NULL, &data, &len) < 0)
            log_err_exit ("error processing stashed valref responses");
        if (len > 0)
            dump_write_data (ar, data, len);
    }
    archive_entry_free (entry);
    if (verbose)
        fprintf (stderr, "%s\n", path);
    progress (d->h, 1);
}

static void dump_val (void *arg, const char *path, json_t *treeobj)
{
    struct dump *d = arg;
    struct archive *ar = d->ar;
    struct archive_entry *entry;
    void *data;
    size_t len;

    if (treeobj_decode_val (treeobj, &data, &len) < 0)
        log_err_exit ("%s: invalid value object", path);
    if (!(entry = archive_entry_new ()))
        log_msg_exit ("error creating archive entry");
    archive_entry_set_pathname (entry, path);
    archive_entry_set_size (entry, len);
    archive_entry_set_perm (entry, 0644);
    archive_entry_set_filetype (entry, AE_IFREG);

    if (archive_write_header (ar, entry) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    dump_write_data (ar, data, len);
    if (verbose)
        fprintf (stderr, "%s\n", path);
    progress (d->h, 1);

    archive_entry_free (entry);
    free (data);
}

static void dump_symlink (void *arg, const char *path, json_t *treeobj)
{
    struct dump *d = arg;
    struct archive *ar = d->ar;
    struct archive_entry *entry;
    const char *ns;
    const char *target;
    char *target_with_ns = NULL;

    if (treeobj_get_symlink (treeobj, &ns, &target) < 0)
        log_err_exit ("%s: invalid symlink object", path);
    if (ns) {
        if (asprintf (&target_with_ns, "%s::%s", ns, target) < 0)
            log_msg_exit ("out of memory");
        target = target_with_ns;
    }
    if (!(entry = archive_entry_new ()))
        log_msg_exit ("error creating archive entry");
    archive_entry_set_pathname (entry, path);
    archive_entry_set_perm (entry, 0644);
    archive_entry_set_filetype (entry, AE_IFLNK);
    archive_entry_set_symlink (entry, target);
    if (archive_write_header (ar, entry) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    if (verbose)
        fprintf (stderr, "%s\n", path);
    progress (d->h, 1);

    free (target_with_ns);
    archive_entry_free (entry);
}

/* kvs_treewalk error callback: a dirref could not be loaded/decoded.  Report
 * it (read_error exits unless --ignore-failed-read), pruning the subtree.
 */
static void dump_error (void *arg,
                        const char *path,
                        enum kvs_treewalk_error error,
                        int errnum)
{
    switch (error) {
        case KVS_TREEWALK_ERROR_LOAD:
            read_error ("%s: missing blobref: %s", path, strerror (errnum));
            break;
        case KVS_TREEWALK_ERROR_BADCOUNT:
            log_msg_exit ("%s: blobref count is not 1", path);
            break;
        case KVS_TREEWALK_ERROR_DECODE:
            log_err_exit ("%s: could not decode directory", path);
            break;
        case KVS_TREEWALK_ERROR_NOTDIR:
            log_msg_exit ("%s: dirref references non-directory", path);
            break;
        case KVS_TREEWALK_ERROR_INVALID:
            log_msg_exit ("%s: invalid tree object", path);
            break;
    }
}

static const struct kvs_treewalk_ops dump_ops = {
    .value = dump_val,
    .symlink = dump_symlink,
    .valref_done = dump_valref,
    .error = dump_error,
};

static void dump_root (flux_t *h, struct archive *ar, const char *blobref)
{
    struct dump d = { .h = h, .ar = ar };
    struct kvs_treewalk *tw;

    if (!(tw = kvs_treewalk_create (h,
                                    blobref,
                                    '/',
                                    async_max,
                                    content_flags,
                                    &dump_ops,
                                    &d)))
        log_err_exit ("error creating kvs treewalk");
    if (kvs_treewalk_run (tw) < 0)
        log_err_exit ("error walking kvs");
    kvs_treewalk_destroy (tw);
}

static int cmd_dump (optparse_t *p, int ac, char *av[])
{
    int optindex =  optparse_option_index (p);
    flux_t *h;
    struct archive *ar;
    const char *outfile;
    int kvs_checkpoint_flags = 0;

    log_init ("flux-dump");

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    outfile = av[optindex++];
    if (optparse_hasopt (p, "verbose"))
        verbose = true;
    if (optparse_hasopt (p, "quiet"))
        quiet = true;
    if (optparse_hasopt (p, "ignore-failed-read"))
        ignore_failed_read = true;
    if (optparse_hasopt (p, "no-cache")) {
        content_flags |= CONTENT_FLAG_CACHE_BYPASS;
        kvs_checkpoint_flags |= KVS_CHECKPOINT_FLAG_CACHE_BYPASS;
    }
    async_max = optparse_get_int (p, "maxreqs", 2);
    if (async_max <= 0)
        log_err_exit ("invalid value for maxreqs");

    dump_time = time (NULL);
    dump_uid = getuid ();
    dump_gid = getgid ();

    h = builtin_get_flux_handle (p);

    /* If the broker is using sd_notify(3) to talk to systemd during
     * start/stop, we can use it to ensure systemd doesn't kill us
     * while dumping during shutdown.  See flux-framework/flux-core#5778.
     */
    if (optparse_hasopt (p, "sd-notify"))
        sd_notify_flag = true;

    ar = dump_create (outfile);
    if (optparse_hasopt (p, "checkpoint")) {
        flux_future_t *f;
        const json_t *checkpoints;
        json_t *checkpt;
        const char *blobref;
        double timestamp;

        /* index 0 is most recent checkpoint */
        if (!(f = kvs_checkpoint_lookup (h, kvs_checkpoint_flags))
            || kvs_checkpoint_lookup_get (f, &checkpoints) < 0
            || !(checkpt = json_array_get (checkpoints, 0))
            || kvs_checkpoint_parse_rootref (checkpt, &blobref) < 0
            || kvs_checkpoint_parse_timestamp (checkpt, &timestamp) < 0)
            log_msg_exit ("error fetching checkpoints: %s",
                          future_strerror (f, errno));

        dump_time = timestamp;
        dump_root (h, ar, blobref);
        flux_future_destroy (f);
    }
    else {
        flux_future_t *f;
        const char *blobref;

        if (!(f = flux_kvs_getroot (h, NULL, 0))
            || flux_kvs_getroot_get_blobref (f, &blobref) < 0)
            log_msg_exit ("error fetching current KVS root: %s",
                          future_strerror (f, errno));
        dump_root (h, ar, blobref);
        flux_future_destroy (f);
    }

    progress_end (h);

    dump_destroy (ar);
    flux_close (h);

    return 0;
}

static struct optparse_option dump_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "List keys on stderr as they are archived",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Don't show periodic progress updates",
    },
    { .name = "checkpoint", .has_arg = 0,
      .usage = "Dump from checkpoint",
    },
    { .name = "no-cache", .has_arg = 0,
      .usage = "Bypass the broker content cache",
    },
    { .name = "ignore-failed-read", .has_arg = 0,
      .usage = "Treat content load errors as non-fatal",
    },
    { .name = "sd-notify", .has_arg = 0,
      .usage = "Send status updates to systemd via flux-broker(1)",
    },
    { .name = "maxreqs", .has_arg = 1, .arginfo = "N",
      .usage = "Increase number of concurrent requests (default 2)",
    },
    OPTPARSE_TABLE_END
};

int subcommand_dump_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "dump",
        cmd_dump,
        "[OPTIONS] OUTFILE",
        "Dump KVS snapshot to a portable archive format",
        0,
        dump_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
