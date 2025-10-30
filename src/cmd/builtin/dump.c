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
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "builtin.h"

#define BLOBREF_ASYNC_MAX 1000

struct dump_valref_data
{
    flux_t *h;
    json_t *treeobj;
    const flux_msg_t **msgs;
    const char *path;
    int total_size;
    int index;
    int count;
    int in_flight;
    int errorcount;
    int errnum;
};

static void get_blobref (struct dump_valref_data *dvd);

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
static bool fast;

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

static void get_blobref_continuation (flux_future_t *f, void *arg)
{
    struct dump_valref_data *dvd = arg;
    const flux_msg_t *msg;
    size_t len;
    int *index;

    index = flux_future_aux_get (f, "index");
    if (flux_future_get (f, (const void **)&msg) < 0
        || flux_response_decode_raw (msg, NULL, NULL, &len) < 0) {
        read_error ("%s: missing blobref %d: %s",
                    dvd->path,
                    (*index),
                    future_strerror (f, errno));
        flux_future_destroy (f);
        dvd->errorcount++;
        dvd->errnum = errno;    /* we'll report the last errno */
        return;
    }
    dvd->in_flight--;
    dvd->total_size += len;
    dvd->msgs[(*index)] = flux_msg_incref (msg);

    /* if an error has occurred, we won't get more blobrefs */
    if (dvd->index < dvd->count
        && !dvd->errorcount) {
        get_blobref (dvd);
        dvd->in_flight++;
        dvd->index++;
    }
    flux_future_destroy (f);
}

static void get_blobref (struct dump_valref_data *dvd)
{
    const char *blobref;
    flux_future_t *f;
    int *indexp;

    blobref = treeobj_get_blobref (dvd->treeobj, dvd->index);

    if (!(f = content_load_byblobref (dvd->h, blobref, content_flags))
        || flux_future_then (f, -1, get_blobref_continuation, dvd) < 0) {
        read_error ("%s: cannot load blobref %d", dvd->path, dvd->index);
        flux_future_destroy (f);
    }
    if (!(indexp = (int *)malloc (sizeof (int))))
        log_err_exit ("cannot allocate index memory");
    (*indexp) = dvd->index;
    if (flux_future_aux_set (f, "index", indexp, free) < 0)
        log_err_exit ("could not save index value");
}

static int dump_valref_async (struct dump_valref_data *dvd)
{
    while (dvd->in_flight < BLOBREF_ASYNC_MAX
           && dvd->index < dvd->count) {
        get_blobref (dvd);
        dvd->in_flight++;
        dvd->index++;
    }

    if (flux_reactor_run (flux_get_reactor (dvd->h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (dvd->errorcount) {
        errno = dvd->errnum;
        return -1;
    }

    return 0;
}

static int dump_valref_serial (struct dump_valref_data *dvd)
{
    for (int i = 0; i < dvd->count; i++) {
        const flux_msg_t *msg;
        size_t len;
        flux_future_t *f;
        if (!(f = content_load_byblobref (dvd->h,
                                          treeobj_get_blobref (dvd->treeobj, i),
                                          content_flags))
            || flux_future_get (f, (const void **)&msg) < 0
            || flux_response_decode_raw (msg, NULL, NULL, &len) < 0) {
            read_error ("%s: missing blobref %d: %s",
                        dvd->path,
                        i,
                        future_strerror (f, errno));
            flux_future_destroy (f);
            dvd->errorcount++;
            dvd->errnum = errno;
            return -1;
        }
        dvd->msgs[i] = flux_msg_incref (msg);
        dvd->total_size += len;
        flux_future_destroy (f);
    }
    return 0;
}

static void dump_valref (struct archive *ar,
                         flux_t *h,
                         const char *path,
                         json_t *treeobj)
{
    int count = treeobj_get_count (treeobj);
    const flux_msg_t **msgs;
    struct archive_entry *entry;
    struct dump_valref_data dvd = {0};

    /* We need the total size before we start writing archive data,
     * so make a first pass, saving the data for writing later.
     */
    if (!(msgs = calloc (count, sizeof (msgs[0]))))
        log_err_exit ("could not create messages array");

    dvd.h = h;
    dvd.treeobj = treeobj;
    dvd.msgs = msgs;
    dvd.path = path;
    dvd.count = count;

    if (fast) {
        if (dump_valref_async (&dvd) < 0)
            goto cleanup;
    }
    else {
        if (dump_valref_serial (&dvd) < 0)
            goto cleanup;
    }

    if (!(entry = archive_entry_new ()))
        log_msg_exit ("error creating archive entry");
    archive_entry_set_pathname (entry, path);
    archive_entry_set_size (entry, dvd.total_size);
    archive_entry_set_perm (entry, 0644);
    archive_entry_set_filetype (entry, AE_IFREG);
    archive_entry_set_mtime (entry, dump_time, 0);
    archive_entry_set_uid (entry, dump_uid);
    archive_entry_set_gid (entry, dump_gid);

    if (archive_write_header (ar, entry) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    for (int i = 0; i < dvd.count; i++) {
        const void *data;
        size_t len;
        if (flux_response_decode_raw (msgs[i], NULL, &data, &len) < 0)
            log_err_exit ("error processing stashed valref responses");
        if (len > 0)
            dump_write_data (ar, data, len);
        flux_msg_decref (msgs[i]);
        msgs[i] = NULL;
    }
    archive_entry_free (entry);
    progress (h, 1);
cleanup:
    for (int i = 0; i < dvd.count; i++) {
        if (msgs[i])
            flux_msg_decref (msgs[i]);
    }
    free (msgs);
}

static void dump_val (struct archive *ar,
                      flux_t *h,
                      const char *path,
                      json_t *treeobj)
{
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
    progress (h, 1);

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
    progress (h, 1);

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
    size_t buflen;
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
    size_t buflen;
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
    if (optparse_hasopt (p, "fast"))
        fast = true;
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
    { .name = "fast", .has_arg = 0,
      .usage = "Speed up flux-dump by running some operations asynchronously",
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
