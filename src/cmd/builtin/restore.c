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
#include "src/common/libccan/ccan/list/list.h"
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
static int store_window;

/* Shared bounded-window engine for asynchronous content_store requests.
 * Both restore phases (value blobs, then directory objects) issue stores
 * through one of these, capping the number of in-flight content.store RPCs
 * at 'window' to hide the per-request round-trip latency that otherwise
 * serializes restore of a large KVS (e.g. an instance with many inactive
 * jobs).  Continuations run one at a time on the reactor thread.
 */
struct store_ctx {
    flux_t *h;
    const char *hash_type;  // borrowed
    int window;             // max in-flight content.store RPCs
    int in_flight;          // outstanding content.store RPCs
};

/* Run the reactor until the number of in-flight stores drops to 'limit',
 * one completion at a time.  With limit = window - 1 this throttles a
 * synchronous producer to the window; with limit = 0 it drains all stores.
 * Store errors are fatal (continuations log_msg_exit), so there is nothing
 * to check for on return.
 */
static void store_ctx_wait (struct store_ctx *ctx, int limit)
{
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    while (ctx->in_flight > limit) {
        if (flux_reactor_run (r, FLUX_REACTOR_ONCE) < 0)
            log_err_exit ("flux_reactor_run");
    }
}

static void progress_notify (flux_t *h)
{
    flux_future_t *f;
    char buf[64];

    snprintf (buf,
              sizeof (buf),
              "flux-restore(1) has restored %d keys",
              keycount);
    f = flux_rpc_pack (h,
                       "state-machine.sd-notify",
                       FLUX_NODEID_ANY,
                       FLUX_RPC_NORESPONSE,
                       "{s:s}",
                       "status", buf);
    flux_future_destroy (f);
}

static void progress (flux_t *h, int delta_blob, int delta_keys)
{
    static int last_keycount = 0;

    blobcount += delta_blob;
    keycount += delta_keys;

    if (last_keycount == keycount
        || !(keycount % 100 == 0 || keycount < 10))
        return;

    if (!quiet && !verbose) {
        fprintf (stderr,
                 "\rflux-restore: restored %d keys (%d blobs)",
                 keycount,
                 blobcount);
    }
    if (sd_notify_flag)
        progress_notify (h);

    last_keycount = keycount;
}

static void progress_end (flux_t *h)
{
    if (!quiet && !verbose) {
        fprintf (stderr,
                 "\rflux-restore: restored %d keys (%d blobs)\n",
                 keycount,
                 blobcount);
    }
    if (sd_notify_flag)
        progress_notify (h);
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

/* One directory object awaiting store.  The directory tree is stored
 * bottom-up: a directory's treeobj cannot be encoded and stored until all of
 * its child directories have been stored and their dirref objects substituted
 * into it.  'dir' points at the live node in the in-memory tree, which is
 * mutated in place as child dirrefs replace child dir entries.
 *
 * Dirs with no unstored children are "ready" and are linked through 'list'
 * into a single ready list (order is irrelevant) owned by the dir_walk
 * below.  A node is on the list at most once.
 */
struct dir_op {
    struct dir_walk *walk;  // shared walk state
    json_t *dir;            // borrowed: node in the in-memory tree
    struct dir_op *parent;  // NULL for root
    char *child_name;       // name of 'dir' in parent->dir; NULL for root
    int pending_children;   // child dirs not yet stored
    struct list_node list;  // ready-list link
};

/* State shared across a bottom-up directory store walk. */
struct dir_walk {
    struct store_ctx *ctx;
    struct list_head ready; // dirs ready to store
    json_t *rootref;        // final root dirref, set when the root completes
};

static void dir_pump (struct dir_walk *walk);

/* Recursively create a dir_op for 'dir' and each of its descendant
 * directories, counting child directories so leaves (no child dirs) are
 * immediately marked ready.
 */
static void build_dir_ops (struct dir_walk *walk,
                           json_t *dir,
                           struct dir_op *parent,
                           const char *child_name)
{
    json_t *data = treeobj_get_data (dir);
    const char *name;
    json_t *entry;
    struct dir_op *op;

    if (!(op = calloc (1, sizeof (*op))))
        log_msg_exit ("out of memory");
    op->walk = walk;
    op->dir = dir;
    op->parent = parent;
    if (child_name && !(op->child_name = strdup (child_name)))
        log_msg_exit ("out of memory");

    json_object_foreach (data, name, entry) {
        if (treeobj_is_dir (entry)) { // recurse
            build_dir_ops (walk, entry, op, name);
            op->pending_children++;
        }
    }
    if (op->pending_children == 0)
        list_add_tail (&walk->ready, &op->list);
}

/* content.store completion for a directory object: build its dirref and
 * substitute it into the parent (making the parent ready once its last
 * child completes), or record it as the final root dirref.
 */
static void dirref_continuation (flux_future_t *f, void *arg)
{
    struct dir_op *op = arg;
    struct dir_walk *walk = op->walk;
    struct store_ctx *ctx = walk->ctx;
    const char *blobref;
    json_t *dirref;

    ctx->in_flight--;
    if (content_store_get_blobref (f, ctx->hash_type, &blobref) < 0)
        log_msg_exit ("error storing dirref blob: %s",
                      future_strerror (f, errno));
    progress (ctx->h, 1, 0);
    if (!(dirref = treeobj_create_dirref (blobref)))
        log_msg_exit ("out of memory");
    if (op->parent) {
        if (treeobj_insert_entry_novalidate (op->parent->dir,
                                             op->child_name,
                                             dirref) < 0)
            log_msg_exit ("error inserting dirref for %s", op->child_name);
        json_decref (dirref);
        if (--op->parent->pending_children == 0)
            list_add_tail (&walk->ready, &op->parent->list);
    }
    else
        walk->rootref = dirref; // transfer ownership to caller
    flux_future_destroy (f);
    free (op->child_name);
    free (op);
    dir_pump (walk);
}

/* Issue directory stores from the ready list up to the window limit. */
static void dir_pump (struct dir_walk *walk)
{
    struct store_ctx *ctx = walk->ctx;
    struct dir_op *op;

    while (ctx->in_flight < ctx->window
           && (op = list_pop (&walk->ready, struct dir_op, list))) {
        char *s;
        flux_future_t *f;

        if (!(s = treeobj_encode (op->dir)))
            log_msg_exit ("out of memory");
        if (!(f = content_store (ctx->h, s, strlen (s), content_flags))
            || flux_future_then (f, -1, dirref_continuation, op) < 0)
            log_msg_exit ("error storing dirref blob: %s",
                          future_strerror (f, errno));
        free (s);
        ctx->in_flight++;
    }
}

static struct dir_walk *dir_walk_create (struct store_ctx *ctx)
{
    struct dir_walk *walk;

    if (!(walk = calloc (1, sizeof (*walk))))
        return NULL;
    walk->ctx = ctx;
    list_head_init (&walk->ready);
    return walk;
}

static void dir_walk_destroy (struct dir_walk *walk)
{
    if (walk) {
        int saved_errno = errno;
        free (walk);
        errno = saved_errno;
    }
}

/* Store all directory objects in the in-memory tree bottom-up through the
 * bounded store window and return the root dirref.
 */
static json_t *restore_dir_async (struct store_ctx *ctx, json_t *root)
{
    json_t *result;
    struct dir_walk *walk;

    if (!(walk = dir_walk_create (ctx)))
        return NULL;
    build_dir_ops (walk, root, NULL, NULL);
    dir_pump (walk);
    if (flux_reactor_run (flux_get_reactor (ctx->h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    result = walk->rootref;
    dir_walk_destroy (walk);

    return result;
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
    progress (h, 0, 1);
}

/* One in-flight value-blob store.  'root' and 'ctx' are borrowed; 'path' is
 * owned and freed by the continuation.  The value data is not retained: it
 * has been copied into the content.store request message by the time the
 * store is issued (see restore_value_async()).
 */
struct valref_op {
    struct store_ctx *ctx;
    json_t *root;
    char *path;
};

/* content.store completion for a value blob: turn the returned blobref into a
 * valref treeobj and insert it into the in-memory tree at the stashed path.
 */
static void valref_continuation (flux_future_t *f, void *arg)
{
    struct valref_op *op = arg;
    struct store_ctx *ctx = op->ctx;
    const char *blobref;
    json_t *treeobj;

    ctx->in_flight--;
    if (content_store_get_blobref (f, ctx->hash_type, &blobref) < 0)
        log_msg_exit ("error storing blob for %s: %s",
                      op->path,
                      future_strerror (f, errno));
    progress (ctx->h, 1, 1);
    if (!(treeobj = treeobj_create_valref (blobref)))
        log_err_exit ("error creating valref object for %s", op->path);
    restore_treeobj (op->root, op->path, treeobj);
    json_decref (treeobj);
    flux_future_destroy (f);
    free (op->path);
    free (op);
}

static void restore_value_async (struct store_ctx *ctx,
                                 json_t *root,
                                 const char *path,
                                 const void *buf,
                                 int size)
{
    flux_t *h = ctx->h;
    json_t *treeobj;

    if (size < BLOBREF_MAX_STRING_SIZE) {
        if (!(treeobj = treeobj_create_val (buf, size)))
            log_err_exit ("error creating val object for %s", path);
    }
    else {
        struct valref_op *op;
        flux_future_t *f;

        /* Issue the store through the bounded window.  content_store()
         * copies 'buf' into the request message before returning, so the
         * caller's buffer may be reused as soon as this returns; the
         * continuation completes the valref insertion later.
         */
        store_ctx_wait (ctx, ctx->window - 1);
        if (!(op = calloc (1, sizeof (*op)))
            || !(op->path = strdup (path)))
            log_msg_exit ("out of memory");
        op->ctx = ctx;
        op->root = root;
        if (!(f = content_store (h, buf, size, content_flags))
            || flux_future_then (f, -1, valref_continuation, op) < 0)
            log_msg_exit ("error storing blob for %s: %s",
                          path,
                          future_strerror (f, errno));
        ctx->in_flight++;
        return; // key count is bumped by valref_continuation on completion
    }
    restore_treeobj (root, path, treeobj);
    json_decref (treeobj);
    progress (h, 0, 1);
}

static struct store_ctx *store_ctx_create (flux_t *h,
                                           const char *hash_type,
                                           int window)
{
    struct store_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    ctx->hash_type = hash_type;
    ctx->window = window;
    return ctx;
}

static void store_ctx_destroy (struct store_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx);
        errno = saved_errno;
    }
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
    struct store_ctx *ctx;

    if (!(ctx = store_ctx_create (h, hash_type, store_window))
        || !(root = treeobj_create_dir ()))
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
            restore_value_async (ctx, root, path, buf, size);
            if (verbose)
                fprintf (stderr, "%s\n", path);
        }
    }
    free (buf);
    store_ctx_wait (ctx, 0);  // drain in-flight value stores (phase A)
    rootref = restore_dir_async (ctx, root);
    store_ctx_destroy (ctx);
    json_decref (root);

    return rootref;
}

/* Return the number of characters of 'blobref' that a human might want to see.
 */
static int shortblobref_length (const char *blobref)
{
    int len = 8;
    const char *cp;

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
    store_window = optparse_get_int (p, "maxreqs", 256);
    if (store_window <= 0)
        log_err_exit ("invalid value for maxreqs");

    h = builtin_get_flux_handle (p);

    if (optparse_hasopt (p, "sd-notify"))
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
        progress_end (h);

        if (!quiet) {
            log_msg ("writing snapshot %.*s to checkpoint for next KVS start",
                     shortblobref_length (blobref),
                     blobref);
        }
        /* restoring, therefore we restart sequence number at 0 */
        if (!(f = kvs_checkpoint_commit (h,
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
        progress_end (h);

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
    { .name = "maxreqs", .has_arg = 1, .arginfo = "N",
      .usage = "Max concurrent content store requests (default 256)",
    },
    { .name = "sd-notify", .has_arg = 0,
      .usage = "Send status updates to systemd via flux-broker(1)",
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
