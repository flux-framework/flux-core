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
#if HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
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
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "builtin.h"

static void dump_treeobj (struct archive *ar,
                          flux_t *h,
                          const char *path,
                          json_t *treeobj);

static bool sd_notify_flag;
static bool verbose;
static bool quiet;
static bool ignore_failed_read;
static int content_flags;
static time_t dump_time;
static gid_t dump_gid;
static uid_t dump_uid;
static int keycount;

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

static void progress (int delta_keys)
{
    keycount += delta_keys;

    if (!verbose
        && !quiet
        && (keycount % 100 == 0 || keycount < 10))
        fprintf (stderr, "\rflux-dump: archived %d keys", keycount);
#if HAVE_LIBSYSTEMD
    if (sd_notify_flag
        && (keycount % 100 == 0 || keycount < 10)) {
        sd_notifyf (0, "EXTEND_TIMEOUT_USEC=%d", 10000000); // 10s
        sd_notifyf (0, "STATUS=flux-dump(1) has archived %d keys", keycount);
    }
#endif
}

static void progress_end (void)
{
    if (!quiet && !verbose)
        fprintf (stderr, "\rflux-dump: archived %d keys\n", keycount);
#if HAVE_LIBSYSTEMD
    if (sd_notify_flag) {
        sd_notifyf (0, "STATUS=flux-dump(1) has archived %d keys", keycount);
    }
#endif
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

static void dump_valref (struct archive *ar,
                         flux_t *h,
                         const char *path,
                         json_t *treeobj)
{
    int count = treeobj_get_count (treeobj);
    struct flux_msglist *l;
    const flux_msg_t *msg;
    int total_size = 0;
    struct archive_entry *entry;
    const void *data;
    int len;

    /* We need the total size before we start writing archive data,
     * so make a first pass, saving the data for writing later.
     */
    /* N.B. first attempt was to save the futures in an array, but ran into:
     *   flux: ev_epoll.c:134: epoll_modify: Assertion `("libev: I/O watcher
     *    with invalid fd found in epoll_ctl", errno != EBADF && errno != ELOOP
     *    && errno != EINVAL)' failed.
     * while archiving a resource.eventlog with 781 entries.  Instead of
     * retaining the futures for a second pass, just retain references to the
     * content.load response messages.
     */
    if (!(l = flux_msglist_create ()))
        log_err_exit ("could not create message list");
    for (int i = 0; i < count; i++) {
        flux_future_t *f;
        if (!(f = content_load_byblobref (h,
                                          treeobj_get_blobref (treeobj, i),
                                          content_flags))
            || flux_future_get (f, (const void **)&msg) < 0
            || flux_response_decode_raw (msg, NULL, &data, &len) < 0) {
            read_error ("%s: missing blobref %d: %s",
                        path,
                        i,
                        future_strerror (f, errno));
            flux_future_destroy (f);
            flux_msglist_destroy (l);
            return;
        }
        if (flux_msglist_append (l, msg) < 0)
            log_err_exit ("could not stash load response message");
        total_size += len;
        flux_future_destroy (f);
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
    while ((msg = flux_msglist_pop (l))) {
        if (flux_response_decode_raw (msg, NULL, &data, &len) < 0)
            log_err_exit ("error processing stashed valref responses");
        if (len > 0)
            dump_write_data (ar, data, len);
        flux_msg_decref (msg);
    }
    archive_entry_free (entry);
    progress (1);
    flux_msglist_destroy (l);
}

static void dump_val (struct archive *ar,
                      flux_t *h,
                      const char *path,
                      json_t *treeobj)
{
    struct archive_entry *entry;
    void *data;
    int len;

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
    progress (1);

    archive_entry_free (entry);
    free (data);
}

static void dump_symlink (struct archive *ar,
                          flux_t *h,
                          const char *path,
                          json_t *treeobj)
{
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
    progress (1);

    free (target_with_ns);
    archive_entry_free (entry);
}

static void dump_dir (struct archive *ar,
                      flux_t *h,
                      const char *path,
                      json_t *treeobj)
{
    json_t *dict = treeobj_get_data (treeobj);
    const char *name;
    json_t *entry;

    json_object_foreach (dict, name, entry) {
        char *newpath;
        if (asprintf (&newpath, "%s/%s", path, name) < 0)
            log_msg_exit ("out of memory");
        dump_treeobj (ar, h, newpath, entry); // recurse
        free (newpath);
    }
}

static void dump_dirref (struct archive *ar,
                         flux_t *h,
                         const char *path,
                         json_t *treeobj)
{
    flux_future_t *f;
    const void *buf;
    int buflen;
    json_t *treeobj_deref = NULL;

    if (treeobj_get_count (treeobj) != 1)
        log_msg_exit ("%s: blobref count is not 1", path);
    if (!(f = content_load_byblobref (h,
                                      treeobj_get_blobref (treeobj, 0),
                                      content_flags))
        || content_load_get (f, &buf, &buflen) < 0) {
        read_error ("%s: missing blobref: %s",
                    path,
                    future_strerror (f, errno));
        flux_future_destroy (f);
        return;
    }
    if (!(treeobj_deref = treeobj_decodeb (buf, buflen)))
        log_err_exit ("%s: could not decode directory", path);
    if (!treeobj_is_dir (treeobj_deref))
        log_msg_exit ("%s: dirref references non-directory", path);
    dump_dir (ar, h, path, treeobj_deref); // recurse
    json_decref (treeobj_deref);
    flux_future_destroy (f);
}

static void dump_treeobj (struct archive *ar,
                          flux_t *h,
                          const char *path,
                          json_t *treeobj)
{
    if (treeobj_validate (treeobj) < 0)
        log_msg_exit ("%s: invalid tree object", path);
    if (treeobj_is_symlink (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        dump_symlink (ar, h, path, treeobj);
    }
    else if (treeobj_is_val (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        dump_val (ar, h, path, treeobj);
    }
    else if (treeobj_is_valref (treeobj)) {
        if (verbose)
            fprintf (stderr, "%s\n", path);
        dump_valref (ar, h, path, treeobj);
    }
    else if (treeobj_is_dirref (treeobj)) {
        dump_dirref (ar, h, path, treeobj); // recurse
    }
    else if (treeobj_is_dir (treeobj)) {
        dump_dir (ar, h, path, treeobj); // recurse
    }
}

static void dump_blobref (struct archive *ar,
                          flux_t *h,
                          const char *blobref)
{
    flux_future_t *f;
    const void *buf;
    int buflen;
    json_t *treeobj;
    json_t *dict;
    const char *key;
    json_t *entry;

    if (!(f = content_load_byblobref (h, blobref, content_flags))
        || content_load_get (f, &buf, &buflen) < 0) {
        read_error ("cannot load root tree object: %s",
                    future_strerror (f, errno));
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
        dump_treeobj (ar, h, key, entry);
    }
    json_decref (treeobj);
    flux_future_destroy (f);
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

    dump_time = time (NULL);
    dump_uid = getuid ();
    dump_gid = getgid ();

    h = builtin_get_flux_handle (p);

    /* If the broker is using sd_notify(3) to talk to systemd during
     * start/stop, we can use it to ensure systemd doesn't kill us
     * while dumping during shutdown.  See flux-framework/flux-core#5778.
     */
    const char *s;
    if ((s = flux_attr_get (h, "broker.sd-notify")) && !streq (s, "0"))
        sd_notify_flag = true;

    ar = dump_create (outfile);
    if (optparse_hasopt (p, "checkpoint")) {
        flux_future_t *f;
        const char *blobref;
        double timestamp;

        if (!(f = kvs_checkpoint_lookup (h, NULL, kvs_checkpoint_flags))
            || kvs_checkpoint_lookup_get_rootref (f, &blobref) < 0
            || kvs_checkpoint_lookup_get_timestamp (f, &timestamp) < 0)
            log_msg_exit ("error fetching checkpoint: %s",
                          future_strerror (f, errno));
        dump_time = timestamp;
        dump_blobref (ar, h, blobref);
        flux_future_destroy (f);
    }
    else {
        flux_future_t *f;
        const char *blobref;

        if (!(f = flux_kvs_getroot (h, NULL, 0))
            || flux_kvs_getroot_get_blobref (f, &blobref) < 0)
            log_msg_exit ("error fetching current KVS root: %s",
                          future_strerror (f, errno));
        dump_blobref (ar, h, blobref);
        flux_future_destroy (f);
    }

    progress_end ();

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
