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
#include <archive.h>
#include <archive_entry.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "builtin.h"

#define BLOCKSIZE 10240 // taken from libarchive example

static bool sd_notify_flag;
static bool verbose;
static bool quiet;
static int content_flags;
static time_t restore_timestamp;
static int blobcount;
static int keycount;
static int blob_size_limit;

static void progress (int delta_blob, int delta_keys)
{
    blobcount += delta_blob;
    keycount += delta_keys;

    if (!quiet
        && !verbose
        && (keycount % 100 == 0 || keycount < 10)) {
        fprintf (stderr,
                 "\rflux-restore: restored %d keys (%d blobs)",
                 keycount,
                 blobcount);
    }
#if HAVE_LIBSYSTEMD
    if (sd_notify_flag
        && (keycount % 100 == 0 || keycount < 10)) {
        sd_notifyf (0, "EXTEND_TIMEOUT_USEC=%d", 10000000); // 10s
        sd_notifyf (0, "STATUS=flux-restore(1) has restored %d keys", keycount);
    }
#endif
}
static void progress_end (void)
{
    if (!quiet && !verbose) {
        fprintf (stderr,
                 "\rflux-restore: restored %d keys (%d blobs)\n",
                 keycount,
                 blobcount);
    }
#if HAVE_LIBSYSTEMD
    if (sd_notify_flag) {
        sd_notifyf (0, "STATUS=flux-restore(1) has restored %d keys", keycount);
    }
#endif
}

static struct archive *restore_create (const char *infile)
{
    struct archive *ar;

    if (!(ar = archive_read_new ()))
        log_msg_exit ("error creating libarchive read context");
    if (archive_read_support_format_all (ar) != ARCHIVE_OK
        || archive_read_support_filter_all (ar) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    if (streq (infile, "-")) {
        if (archive_read_open_FILE (ar, stdin) != ARCHIVE_OK)
            log_msg_exit ("%s", archive_error_string (ar));
    }
    else {
        if (archive_read_open_filename (ar, infile, BLOCKSIZE) != ARCHIVE_OK)
            log_msg_exit ("%s", archive_error_string (ar));
    }
    return ar;
}

static void restore_destroy (struct archive *ar)
{
    if (archive_read_close (ar) != ARCHIVE_OK)
        log_msg_exit ("%s", archive_error_string (ar));
    archive_read_free (ar);
}

static json_t *restore_dir (flux_t *h, const char *hash_type, json_t *dir)
{
    json_t *data = treeobj_get_data (dir);
    const char *name;
    json_t *entry;
    json_t *ndir;

    if (!(ndir = treeobj_create_dir ()))
        log_msg_exit ("out of memory");
    json_object_foreach (data, name, entry) {
        json_t *nentry = NULL;
        if (treeobj_is_dir (entry)) // recurse
            nentry = restore_dir (h, hash_type, entry);
        if (treeobj_insert_entry_novalidate (ndir,
                                             name,
                                             nentry ? nentry : entry) < 0)
            log_msg_exit ("error inserting object");
        json_decref (nentry);
    }

    char *s;
    flux_future_t *f;
    const char *blobref;
    json_t *dirref;

    if (!(s = treeobj_encode (ndir)))
        log_msg_exit ("out of memory");
    if (!(f = content_store (h, s, strlen (s), content_flags))
        || content_store_get_blobref (f, hash_type, &blobref) < 0)
        log_msg_exit ("error storing dirref blob: %s",
                      future_strerror (f, errno));
    progress (1, 0);
    if (!(dirref = treeobj_create_dirref (blobref)))
        log_msg_exit ("out of memory");
    free (s);
    flux_future_destroy (f);
    json_decref (ndir);

    return dirref;
}

static void restore_treeobj (json_t *root, const char *path, json_t *treeobj)
{
    char *argz = NULL;
    size_t argz_len = 0;
    int count;
    char *name;
    json_t *dir;
    json_t *dp;

    /* Walk 'path' to the penultimate component (creating any missing dirs)
     * and leave 'dir' pointing to it.
     */
    if (argz_create_sep (path, '/', &argz, &argz_len) != 0)
        log_msg_exit ("out of memory");
    dir = root;
    count = argz_count (argz, argz_len);
    name = NULL;
    for (int i = 0; i < count - 1; i++) {
        name = argz_next (argz, argz_len, name);
        if (!(dp = treeobj_get_entry (dir, name))) {
            if (!(dp = treeobj_create_dir ())
                || treeobj_insert_entry (dir, name, dp) < 0)
                log_msg_exit ("out of memory");
            json_decref (dp); // treeobj_insert_entry took a ref
        }
        else if (!treeobj_is_dir (dp))
            log_msg_exit ("%s in %s is not a directory", name, path);
        dir = dp;
    }
    /* Insert treeobj into 'dir' under final path component.
     */
    name = argz_next (argz, argz_len, name);
    if (treeobj_insert_entry (dir, name, treeobj) < 0)
        log_err_exit ("error inserting %s into root directory", path);

    free (argz);
}

static void restore_symlink (flux_t *h,
                             json_t *root,
                             const char *path,
                             const char *ns_target)
{
    char *cpy;
    char *cp;
    const char *ns;
    const char *target;
    json_t *treeobj;

    if (!(cpy = strdup (ns_target)))
        log_msg_exit ("out of memory");
    if ((cp = strstr (cpy, "::"))) {
        *cp = '\0';
        ns = cpy;
        target = cp + 2;
    }
    else {
        ns = NULL;
        target = cpy;
    }
    if (!(treeobj = treeobj_create_symlink (ns, target)))
        log_err_exit ("cannot create symlink object for %s", path);
    restore_treeobj (root, path, treeobj);
    json_decref (treeobj);
    free (cpy);
    progress (0, 1);
}

static void restore_value (flux_t *h,
                           const char *hash_type,
                           json_t *root,
                           const char *path,
                           const void *buf,
                           int size)
{
    json_t *treeobj;

    if (size < BLOBREF_MAX_STRING_SIZE) {
        if (!(treeobj = treeobj_create_val (buf, size)))
            log_err_exit ("error creating val object for %s", path);
    }
    else {
        flux_future_t *f;
        const char *blobref;

        if (!(f = content_store (h, buf, size, content_flags))
            || content_store_get_blobref (f, hash_type, &blobref) < 0)
            log_msg_exit ("error storing blob for %s: %s",
                          path,
                          future_strerror (f, errno));
        progress (1, 0);
        if (!(treeobj = treeobj_create_valref (blobref)))
            log_err_exit ("error creating valref object for %s", path);
        flux_future_destroy (f);
    }
    restore_treeobj (root, path, treeobj);
    json_decref (treeobj);
    progress (0, 1);
}

/* Restore archive and return a 'dirref' object pointing to it.
 */
static json_t *restore_snapshot (struct archive *ar,
                                 flux_t *h,
                                 const char *hash_type)
{
    void *buf = NULL;
    int bufsize = 0;
    json_t *root;
    json_t *rootref;

    if (!(root = treeobj_create_dir ()))
        log_msg_exit ("out of memory");

    for (;;) {
        struct archive_entry *entry;
        int res;
        const char *path;
        mode_t type;
        time_t mtime;

        res = archive_read_next_header (ar, &entry);
        if (res == ARCHIVE_EOF)
            break;
        if (res != ARCHIVE_OK)
            log_msg_exit ("%s", archive_error_string (ar));
        path = archive_entry_pathname (entry);
        type = archive_entry_filetype (entry);
        mtime = archive_entry_mtime (entry);

        if (restore_timestamp < mtime)
            restore_timestamp = mtime;

        if (type == AE_IFLNK) {
            const char *target = archive_entry_symlink (entry);

            restore_symlink (h, root, path, target);
            if (verbose)
                fprintf (stderr, "%s -> %s\n", path, target);
        }
        else if (type == AE_IFREG) {
            int size = archive_entry_size (entry);

            if (blob_size_limit > 0 && size > blob_size_limit) {
                fprintf (stderr,
                         "%s%s size %d exceeds %d limit, skipping\n",
                         (!quiet && !verbose) ? "\r" : "",
                         path,
                         size,
                         blob_size_limit);
                // N.B.  archive_read_next_header() skips unconsumed data
                //   automatically so it is safe to "continue" here.
                continue;
            }
            if (size > bufsize) {
                void *newbuf;
                if (!(newbuf = realloc (buf, size)))
                    log_msg_exit ("out of memory");
                buf = newbuf;
                bufsize = size;
            }
            res = archive_read_data (ar, buf, bufsize);
            if (res != size) {
                if (res < 0)
                    log_msg_exit ("%s", archive_error_string (ar));
                else
                    log_msg_exit ("short read from archive");
            }
            restore_value (h, hash_type, root, path, buf, size);
            if (verbose)
                fprintf (stderr, "%s\n", path);
        }
    }
    free (buf);
    rootref = restore_dir (h, hash_type, root);
    json_decref (root);

    return rootref;
}

/* Return the number of characters of 'blobref' that a human might want to see.
 */
static int shortblobref_length (const char *blobref)
{
    int len = 8;
    char *cp;

    if ((cp = strchr (blobref, '-')))
        len += (cp - blobref) + 1;
    return len;
}

static bool kvs_is_running (flux_t *h)
{
    flux_future_t *f;
    bool running = true;

    if ((f = flux_kvs_getroot (h, NULL, 0)) != NULL
        && flux_rpc_get (f, NULL) < 0
        && errno == ENOSYS)
        running = false;
    flux_future_destroy (f);
    return running;
}

static void flush_content (flux_t *h, uint32_t rank)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h, "content.flush", NULL, rank, 0))
        || flux_rpc_get (f, NULL) < 0)
        log_msg ("error flushing content cache: %s", future_strerror (f, errno));
    flux_future_destroy (f);
}

static int cmd_restore (optparse_t *p, int ac, char *av[])
{
    int optindex =  optparse_option_index (p);
    flux_t *h;
    struct archive *ar;
    const char *infile;
    int kvs_checkpoint_flags = 0;
    char *hash_type = "sha1";

    log_init ("flux-restore");

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    infile = av[optindex++];
    if (optparse_hasopt (p, "verbose"))
        verbose = true;
    if (optparse_hasopt (p, "quiet"))
        quiet = true;
    if (optparse_hasopt (p, "no-cache")) {
        content_flags |= CONTENT_FLAG_CACHE_BYPASS;
        kvs_checkpoint_flags |= KVS_CHECKPOINT_FLAG_CACHE_BYPASS;
    }
    blob_size_limit = optparse_get_size_int (p, "size-limit", "0");

    h = builtin_get_flux_handle (p);

    const char *s;
    if ((s = flux_attr_get (h, "broker.sd-notify")) && !streq (s, "0"))
        sd_notify_flag = true;

    ar = restore_create (infile);

    if (optparse_hasopt (p, "checkpoint")) {
        json_t *dirref;
        const char *blobref;
        flux_future_t *f;

        if (kvs_is_running (h))
            log_msg_exit ("please unload kvs module before using --checkpoint");

        dirref = restore_snapshot (ar, h, hash_type);
        blobref = treeobj_get_blobref (dirref, 0);
        progress_end ();

        if (!quiet) {
            log_msg ("writing snapshot %.*s to checkpoint for next KVS start",
                     shortblobref_length (blobref),
                     blobref);
        }
        /* restoring, therefore we restart sequence number at 0 */
        if (!(f = kvs_checkpoint_commit (h,
                                         NULL,
                                         blobref,
                                         0,
                                         restore_timestamp,
                                         kvs_checkpoint_flags))
            || flux_rpc_get (f, NULL) < 0) {
            log_msg_exit ("error updating checkpoint: %s",
                          future_strerror (f, errno));
        }
        flux_future_destroy (f);

        json_decref (dirref);
    }
    else if (optparse_hasopt (p, "key")) {
        const char *key = optparse_get_str (p, "key", NULL);
        json_t *dirref;
        char *s;
        const char *blobref;
        flux_kvs_txn_t *txn;
        flux_future_t *f;

        dirref = restore_snapshot (ar, h, hash_type);
        blobref = treeobj_get_blobref (dirref, 0);
        progress_end ();

        if (!quiet) {
            log_msg ("writing snapshot %.*s to KVS key '%s'",
                     shortblobref_length (blobref),
                     blobref,
                     key);
        }

        if (!(s = json_dumps (dirref, JSON_COMPACT)))
            log_msg_exit ("error encoding final dirref object");

        f = NULL;
        if (!(txn = flux_kvs_txn_create ())
            || flux_kvs_txn_put_treeobj (txn, 0, key, s) < 0
            || !(f = flux_kvs_commit (h, NULL, 0, txn))
            || flux_rpc_get (f, NULL) < 0) {
            log_msg_exit ("error updating %s: %s",
                          key,
                          future_strerror (f, errno));
        }
        flux_future_destroy (f);
        flux_kvs_txn_destroy (txn);

        free (s);
        json_decref (dirref);
    }
    else {
        log_msg_exit ("Please specify a restore target with"
                      " --checkpoint or --key");
    }

    if (!optparse_hasopt (p, "no-cache"))
        flush_content (h, 0);

    restore_destroy (ar);
    flux_close (h);

    return 0;
}

static struct optparse_option restore_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "List keys on stderr as they are restored",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Don't show periodic progress updates",
    },
    { .name = "checkpoint", .has_arg = 0,
      .usage = "Restore to checkpoint",
    },
    { .name = "key", .has_arg = 1,
      .arginfo = "KEY",
      .usage = "Restore to KVS key",
    },
    { .name = "no-cache", .has_arg = 0,
      .usage = "Bypass the broker content cache",
    },
    { .name = "size-limit", .has_arg = 1, .arginfo = "SIZE",
      .usage = "Do not restore blobs greater than SIZE bytes",
    },
    OPTPARSE_TABLE_END
};

int subcommand_restore_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "restore",
        cmd_restore,
        "[OPTIONS] INFILE",
        "Restore KVS snapshot from a portable archive format",
        0,
        restore_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
