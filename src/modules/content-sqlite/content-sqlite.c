/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* content-sqlite.c - content addressable storage with sqlite back end */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sqlite3.h>
#include <lz4.h>
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libkvs/kvs_checkpoint.h"

#include "src/common/libcontent/content-util.h"
#include "src/common/libccan/ccan/list/list.h"
#include "ccan/str/str.h"

const size_t lzo_buf_chunksize = 1024*1024;
const size_t compression_threshold = 256; /* compress blobs >= this size */

const char *sql_create_table = "CREATE TABLE if not exists objects("
                               "  hash BLOB PRIMARY KEY,"
                               "  size INT,"
                               "  object BLOB,"
                               "  epoch INT DEFAULT 0"
                               ");";
const char *sql_load = "SELECT object,size FROM objects"
                       "  WHERE hash = ?1 LIMIT 1";
const char *sql_store = "INSERT INTO objects (hash,size,object,epoch) "
                        "  values (?1, ?2, ?3, ?4) "
                        "ON CONFLICT(hash) DO UPDATE SET epoch = excluded.epoch";
const char *sql_validate = "SELECT EXISTS("
                           "  SELECT 1 FROM objects WHERE hash = ?1)";
const char *sql_objects_count = "SELECT count(1) FROM objects";

const char *sql_checkpt_get_v1 = "SELECT value FROM checkpt"
                                 "  WHERE key = ?1";

const char *sql_drop_checkpt = "DROP TABLE IF EXISTS checkpt";

const char *sql_create_table_checkpt_v2 =
    "CREATE TABLE if not exists checkpt_v2("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
    "  value TEXT"
    ");";
const char *sql_checkpt_get_v2 = "SELECT value FROM checkpt_v2"
                                 " ORDER BY id DESC";
const char *sql_checkpt_put_v2 = "INSERT INTO checkpt_v2 (value)"
                                 " values (?1)";
const char *sql_checkpt_prune =
    "DELETE FROM checkpt_v2"
    " WHERE id IN ("
    "   SELECT id FROM checkpt_v2 ORDER BY id DESC LIMIT -1 OFFSET ?1"
    " );";

const char *sql_table_list = "SELECT tbl_name FROM sqlite_master where type = 'table'";

const char *sql_checkpt_get_all = "SELECT * FROM checkpt_v2 ORDER BY id DESC";

const char *sql_alter_objects_add_epoch = "ALTER TABLE objects ADD COLUMN epoch INT DEFAULT 0";
const char *sql_get_max_checkpt_id = "SELECT MAX(id) FROM checkpt_v2";
const char *sql_table_info = "PRAGMA table_info(objects)";
const char *sql_mark_blob = "UPDATE objects SET epoch = MAX(epoch, ?1) WHERE hash = ?2";
/* Select up to 'delete_cap' (?4) garbage rowids in a bounded rowid window
 * (?2 < rowid <= ?3), ascending, resuming from a cursor.  See sweep_cb.
 */
const char *sql_sweep_select = "SELECT rowid FROM objects"
                               "  WHERE epoch < ?1 AND rowid > ?2 AND rowid <= ?3"
                               "  ORDER BY rowid LIMIT ?4";
const char *sql_sweep_delete = "DELETE FROM objects WHERE rowid = ?1";
const char *sql_max_rowid = "SELECT MAX(rowid) FROM objects";
const char *sql_count_sweep_candidates = "SELECT COUNT(*) FROM objects WHERE epoch < ?1";

#define MAX_CHECKPOINTS_DEFAULT 5

/* Upper bound on the number of hashes accepted by a single mark RPC.
 * The mark handler runs synchronously on content-sqlite's single reactor
 * thread, so an unbounded array would block all other backing traffic while
 * it is processed.  flux-gc batches marks in chunks of 100; this cap is well
 * above that but still bounds the per-RPC work.  Like the sweep ceilings below,
 * it targets a worst-case stall of roughly 100ms: a mark is an indexed UPDATE
 * (~1us warm by the same rough microbenchmark), so 16384 ~= 16ms, comfortably
 * under budget.  Treat this as an order-of-magnitude bound, not a guarantee.
 */
#define MARK_HASHES_MAX 16384

/* Per-call ceilings on the two sweep bounds.  The sweep runs synchronously on
 * the reactor thread, so both the DELETE work and the SELECT scan must be
 * bounded to cap the stall.  'delete_cap' limits rows deleted (the dominant
 * cost); 'window' limits rows scanned (so a sparse span still makes progress).
 * Requests above these are CLAMPED, not rejected: flux-gc terminates its sweep
 * loop on the rowid cursor reaching the high-water mark, not on batch size, so
 * a clamped call is harmless -- the tool simply makes another call.
 *
 * The ceilings target a worst-case per-call stall of roughly 100ms.  This is a
 * ROUGH back-of-envelope from warm-cache microbenchmarks (~5us per deleted row,
 * ~0.1us per scanned row): 8192 deletes ~= 40ms and 512K scanned rows ~= 50ms,
 * ~90ms combined.  Real disk / cold cache is slower, so treat 100ms as an
 * order-of-magnitude bound, not a guarantee.  flux-gc's defaults (1000 / 100K)
 * sit well under these; the ceilings only bound a hostile or buggy caller.
 */
#define SWEEP_DELETE_MAX 8192
#define SWEEP_WINDOW_MAX (1<<19)   /* 512K rows scanned per call */

struct content_stats {
    tstat_t load;
    tstat_t store;
    tstat_t batch;  // stores per group-committed transaction
};

struct content_sqlite {
    flux_msg_handler_t **handlers;
    char *dbfile;
    sqlite3 *db;
    sqlite3_stmt *load_stmt;
    sqlite3_stmt *store_stmt;
    sqlite3_stmt *validate_stmt;
    sqlite3_stmt *checkpt_get_stmt;
    sqlite3_stmt *checkpt_put_stmt;
    sqlite3_stmt *checkpt_prune_stmt;
    sqlite3_stmt *checkpt_get_all_stmt;
    flux_t *h;
    char *hashfun;
    int hash_size;
    size_t lzo_bufsize;
    void *lzo_buf;
    struct content_stats stats;
    char *journal_mode;
    char *synchronous;
    int max_checkpoints;
    bool truncate;
    int64_t current_epoch;

    /* Group commit: when batch_timeout > 0, store inserts are grouped into
     * an explicit transaction to amortize per-commit WAL overhead across
     * many blobs. A store request is NOT answered until the transaction
     * that includes it has committed, preserving content's guarantee that a
     * store response implies the blob is durable.  Responses for all stores
     * in a batch are therefore held on batch_pending and sent together by
     * batch_commit().
     *
     * Grouping is opportunistic: the open batch is committed as soon as no
     * more store requests are queued behind the current one
     * (flux_pollevents), i.e. the burst has drained.  Under a sustained
     * burst (e.g. flux-restore) stores pile into large transactions, while
     * an isolated runtime store commits at once with no durability lag (no
     * delay is ever added to wait for a batch to fill).  Two ceilings bound
     * a batch that would otherwise never drain:
     *  - batch_timeout seconds since the batch opened (bounds response
     *  latency and WAL growth under a continuous burst), - BATCH_COUNT_MAX
     *  stores accumulated (hard WAL-growth safety cap),
     * plus boundaries: checkpoint-put and shutdown.
     */
    int batch_count;    // stores in the currently open transaction
    bool in_batch;      // an explicit transaction is open
    double batch_timeout;  // >0 enables group commit; max open-batch age
    flux_watcher_t *batch_timer;  // one-shot, armed while a batch is open
    struct list_head batch_pending; // store responses awaiting commit
};

/* Hard ceiling on stores per group-committed transaction if a burst never
 * lets the recv queue drain.  Not user-tunable: the opportunistic drain check
 * and batch_timeout are the normal commit triggers; this only caps a
 * pathological continuous burst.  Note this bounds the store count, not bytes:
 * batch_timeout is the primary bound on how long WAL growth can accumulate.
 */
#define BATCH_COUNT_MAX 65536

/* A store request whose response is deferred until its batch commits. 'hash'
 * is the computed blobref to return (always ctx->hash_size bytes); 'msg' is
 * the borrowed request (ref held).
 */
struct pending_store {
    struct list_node list;
    const flux_msg_t *msg;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
};

static int set_config (char **conf, const char *val)
{
    char *tmp;
    if (!(tmp = strdup (val)))
        return -1;
    free (*conf);
    *conf = tmp;
    return 0;
}

/* Fill 'errp' with the current sqlite error message so a handler can return
 * it to the client, and return errp->text for convenient assignment.
 * log_sqlite_error() formats the same text with a code-site prefix for the
 * broker log.
 */
static const char *set_text_from_sqlite_error (struct content_sqlite *ctx,
                                               flux_error_t *errp)
{
    if (!errp)
        return NULL;
    if (ctx->db) {
        const char *errmsg = sqlite3_errmsg (ctx->db);
        errprintf (errp,
                   "%s(%d)",
                   errmsg ? errmsg : "unknown error code",
                   sqlite3_extended_errcode (ctx->db));
    }
    else
        errprintf (errp, "unknown error, no sqlite3 handle");
    return errp->text;
}

static void log_sqlite_error (struct content_sqlite *ctx, const char *fmt, ...)
{
    char buf[64];
    flux_error_t error;
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    flux_log (ctx->h,
              LOG_ERR,
              "%s: %s",
              buf,
              set_text_from_sqlite_error (ctx, &error));
}

static void set_errno_from_sqlite_error (struct content_sqlite *ctx)
{
    switch (sqlite3_errcode (ctx->db)) {
        case SQLITE_IOERR:      /* os io error */
            errno = EIO;
            break;
        case SQLITE_NOMEM:      /* cannot allocate memory */
            errno = ENOMEM;
            break;
        case SQLITE_ABORT:      /* statement is not authorized */
        case SQLITE_PERM:       /* access mode for new db cannot be provided */
        case SQLITE_READONLY:   /* attempt to alter data with no permission */
            errno = EPERM;
            break;
        case SQLITE_TOOBIG:     /* blob too large */
            errno = EFBIG;
            break;
        case SQLITE_FULL:       /* file system full */
            errno = ENOSPC;
            break;
        default:
            errno = EINVAL;
            break;
    }
}

/* Begin an explicit write transaction for group-committed stores, and arm the
 * timeout so the batch cannot stay open indefinitely when stores arrive
 * slowly (which would keep blobs non-durable and grow the WAL unbounded).
 */
static int batch_begin (struct content_sqlite *ctx)
{
    if (sqlite3_exec (ctx->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "batch: BEGIN");
        set_errno_from_sqlite_error (ctx);
        return -1;
    }
    ctx->in_batch = true;
    ctx->batch_count = 0;
    if (ctx->batch_timer) {
        flux_timer_watcher_reset (ctx->batch_timer, ctx->batch_timeout, 0.);
        flux_watcher_start (ctx->batch_timer);
    }
    return 0;
}

static void pending_store_destroy (struct pending_store *p)
{
    if (p) {
        int saved_errno = errno;
        flux_msg_decref (p->msg);
        free (p);
        errno = saved_errno;
    }
}

/* Answer every deferred store response held for the just-completed batch.
 * On success each gets its blobref; on failure each gets 'errnum'.
 */
static void batch_respond_pending (struct content_sqlite *ctx, int errnum)
{
    struct pending_store *p;

    while ((p = list_pop (&ctx->batch_pending, struct pending_store, list))) {
        if (errnum == 0) {
            if (flux_respond_raw (ctx->h, p->msg, p->hash, ctx->hash_size) < 0)
                flux_log_error (ctx->h, "store: flux_respond_raw");
        }
        else {
            if (flux_respond_error (ctx->h, p->msg, errnum, NULL) < 0)
                flux_log_error (ctx->h, "store: flux_respond_error");
        }
        pending_store_destroy (p);
    }
}

/* Commit the open group-commit transaction, if any, disarm the timeout,
 * and only then answer the deferred store responses (a response must not
 * precede the durable commit).  Called on the size cap, at boundaries
 * (checkpoint-put, shutdown), and from the batch timeout.
 */
static int batch_commit (struct content_sqlite *ctx)
{
    if (!ctx->in_batch)
        return 0;
    if (ctx->batch_timer)
        flux_watcher_stop (ctx->batch_timer);
    if (sqlite3_exec (ctx->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "batch: COMMIT");
        set_errno_from_sqlite_error (ctx);
        ctx->in_batch = false;
        ctx->batch_count = 0;
        batch_respond_pending (ctx, errno); // commit failed: fail them all
        return -1;
    }
    ctx->in_batch = false;
    tstat_push (&ctx->stats.batch, ctx->batch_count);
    ctx->batch_count = 0;
    batch_respond_pending (ctx, 0);
    return 0;
}

/* Batch timeout: commit whatever has accumulated so it doesn't age further. */
static void batch_timer_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    struct content_sqlite *ctx = arg;
    if (batch_commit (ctx) < 0)
        flux_log_error (ctx->h, "batch: timed commit failed");
}

static int grow_lzo_buf (struct content_sqlite *ctx, size_t size)
{
    size_t newsize = ctx->lzo_bufsize;
    void *newbuf;
    while (newsize < size)
        newsize += lzo_buf_chunksize;
    if (!(newbuf = realloc (ctx->lzo_buf, newsize))) {
        errno = ENOMEM;
        return -1;
    }
    ctx->lzo_bufsize = newsize;
    ctx->lzo_buf = newbuf;
    return 0;
}

/* Load blob from objects table, uncompressing if necessary.
 * Returns 0 on success, -1 on error with errno set.
 * On successful return, must call sqlite3_reset (ctx->load_stmt),
 * which invalidates returned data.
 */
static int content_sqlite_load (struct content_sqlite *ctx,
                                const void *hash,
                                int hash_size,
                                const void **datap,
                                int *sizep)
{
    const void *data = NULL;
    int size = 0;
    int uncompressed_size;

    if (sqlite3_bind_text (ctx->load_stmt,
                           1,
                           (char *)hash,
                           hash_size,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "load: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_step (ctx->load_stmt) != SQLITE_ROW) {
        //log_sqlite_error (ctx, "load: executing stmt");
        errno = ENOENT;
        goto error;
    }
    size = sqlite3_column_bytes (ctx->load_stmt, 0);
    if (sqlite3_column_type (ctx->load_stmt, 0) != SQLITE_BLOB && size > 0) {
        flux_log (ctx->h, LOG_ERR, "load: selected value is not a blob");
        errno = EINVAL;
        goto error;
    }
    data = sqlite3_column_blob (ctx->load_stmt, 0);
    if (sqlite3_column_type (ctx->load_stmt, 1) != SQLITE_INTEGER) {
        flux_log (ctx->h, LOG_ERR, "load: selected value is not an integer");
        errno = EINVAL;
        goto error;
    }
    uncompressed_size = sqlite3_column_int (ctx->load_stmt, 1);
    if (uncompressed_size != -1) {
        if (ctx->lzo_bufsize < uncompressed_size
                                && grow_lzo_buf (ctx, uncompressed_size) < 0)
            goto error;
        int r = LZ4_decompress_safe (data,
                                     ctx->lzo_buf,
                                     size,
                                     uncompressed_size);
        if (r < 0) {
            errno = EINVAL;
            goto error;
        }
        if (r != uncompressed_size) {
            flux_log (ctx->h, LOG_ERR, "load: blob size mismatch");
            errno = EINVAL;
            goto error;
        }
        data = ctx->lzo_buf;
        size = uncompressed_size;
    }
    *datap = data;
    *sizep = size;
    /* call sqlite3_reset() on ctx->load_stmt in caller, after it has
     * used returned data pointer */
    return 0;
error:
    ERRNO_SAFE_WRAP (sqlite3_reset, ctx->load_stmt);
    return -1;
}

/* Store blob to objects table, compressing if necessary.
 * hash over 'data' is stored to 'hash'.
 * Returns hash size on success, -1 on error with errno set.
 */
static int content_sqlite_store (struct content_sqlite *ctx,
                                 const void *data,
                                 int size,
                                 void *hash,
                                 int hash_len)
{
    int uncompressed_size = -1;
    int hash_size;

    if ((hash_size = blobref_hash_raw (ctx->hashfun,
                                       data,
                                       size,
                                       hash,
                                       hash_len)) < 0)
        return -1;
    assert (hash_size == ctx->hash_size);
    if (size >= compression_threshold) {
        int r;
        int out_len = LZ4_compressBound(size);
        if (ctx->lzo_bufsize < out_len && grow_lzo_buf (ctx, out_len) < 0)
            return -1;
        r = LZ4_compress_default (data, ctx->lzo_buf, size, out_len);
        if (r == 0) {
            errno = EINVAL;
            return -1;
        }
        uncompressed_size = size;
        size = r;
        data = ctx->lzo_buf;
    }
    if (sqlite3_bind_text (ctx->store_stmt,
                           1,
                           hash,
                           hash_size,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_bind_int (ctx->store_stmt,
                          2,
                          uncompressed_size) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding size");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_bind_blob (ctx->store_stmt,
                           3,
                           data,
                           size,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding data");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_bind_int64 (ctx->store_stmt,
                            4,
                            ctx->current_epoch) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding epoch");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    /* N.B. ON CONFLICT clause updates epoch without rewriting object.
     */
    if (sqlite3_step (ctx->store_stmt) != SQLITE_DONE) {
        log_sqlite_error (ctx, "store: executing stmt");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    sqlite3_reset (ctx->store_stmt);
    return hash_size;
error:
    ERRNO_SAFE_WRAP (sqlite3_reset, ctx->store_stmt);
    return -1;
}

/* Validate blob in objects table.
 * Returns 0 if valid, -1 on error (ENOENT if  not found)
 */
static int content_sqlite_validate (struct content_sqlite *ctx,
                                    const void *hash,
                                    int hash_size)
{
    if (sqlite3_bind_text (ctx->validate_stmt,
                           1,
                           (char *)hash,
                           hash_size,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "validate: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_step (ctx->validate_stmt) != SQLITE_ROW) {
        //log_sqlite_error (ctx, "validate: executing stmt");
        errno = ENOENT;
        goto error;
    }
    if (sqlite3_column_type (ctx->validate_stmt, 0) != SQLITE_INTEGER) {
        flux_log (ctx->h, LOG_ERR, "validate: result is not an integer");
        errno = EINVAL;
        goto error;
    }
    if (!sqlite3_column_int (ctx->validate_stmt, 0)) {
        errno = ENOENT;
        goto error;
    }
    (void )sqlite3_reset (ctx->validate_stmt);
    return 0;
error:
    ERRNO_SAFE_WRAP (sqlite3_reset, ctx->validate_stmt);
    return -1;
}

static void load_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct content_sqlite *ctx = arg;
    const void *hash;
    size_t hash_size;
    const void *data;
    int size;
    struct timespec t0;

    if (flux_request_decode_raw (msg,
                                 NULL,
                                 &hash,
                                 &hash_size) < 0)
        goto error;
    if (hash_size != ctx->hash_size) {
        errno = EPROTO;
        goto error;
    }
    monotime (&t0);
    if (content_sqlite_load (ctx, hash, hash_size, &data, &size) < 0)
        goto error;
    tstat_push (&ctx->stats.load, monotime_since (t0));
    if (flux_respond_raw (h, msg, data, size) < 0)
        flux_log_error (h, "load: flux_respond_raw");
    (void )sqlite3_reset (ctx->load_stmt);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "load: flux_respond_error");
}

void store_cb (flux_t *h,
               flux_msg_handler_t *mh,
               const flux_msg_t *msg,
               void *arg)
{
    struct content_sqlite *ctx = arg;
    const void *data;
    size_t size;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_size;
    struct pending_store *p;
    struct timespec t0;

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0) {
        flux_log_error (h, "store: request decode failed");
        goto error;
    }
    monotime (&t0);
    /* Unbatched: store and respond immediately (the store is durable per the
     * synchronous/journal_mode settings once the autocommit returns).
     */
    if (ctx->batch_timeout == 0.) {
        if ((hash_size = content_sqlite_store (ctx,
                                               data,
                                               size,
                                               hash,
                                               sizeof (hash))) < 0)
            goto error;
        tstat_push (&ctx->stats.store, monotime_since (t0));
        if (flux_respond_raw (h, msg, hash, hash_size) < 0)
            flux_log_error (h, "store: flux_respond_raw");
        return;
    }
    /* Batched: run the INSERT inside an open transaction and defer the
     * response until batch_commit() makes the blob durable (see policy at
     * struct content_sqlite).
     */
    if (!ctx->in_batch) {
        if (batch_begin (ctx) < 0)
            goto error;
    }
    if (!(p = calloc (1, sizeof (*p))))
        goto error;
    if (content_sqlite_store (ctx, data, size, p->hash, sizeof (p->hash)) < 0) {
        /* This store failed; fail it directly.  Earlier stores in the batch
         * are valid, so commit them (releasing their deferred responses)
         * rather than losing the open transaction.
         */
        int saved_errno = errno;
        pending_store_destroy (p);
        (void)batch_commit (ctx);
        errno = saved_errno;
        goto error;
    }
    p->msg = flux_msg_incref (msg);
    list_add_tail (&ctx->batch_pending, &p->list);
    ctx->batch_count++;
    tstat_push (&ctx->stats.store, monotime_since (t0));

    /* Commit when the burst drains (opportunistic), or a ceiling is hit.
     * The deferred responses are released by batch_commit().  FLUX_POLLIN
     * on our handle means another request is already queued behind this
     * one, so the burst has not drained yet and the batch keeps growing.
     * This per-store check keeps an isolated store from waiting for the
     * timeout; the timeout and BATCH_COUNT_MAX bound the batch if the queue
     * never drains.
     */
    bool drained = !(flux_pollevents (h) & FLUX_POLLIN);
    if (drained || ctx->batch_count >= BATCH_COUNT_MAX) {
        if (batch_commit (ctx) < 0)
            flux_log_error (h, "store: batch commit failed");
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "store: flux_respond_error");
}

static void validate_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct content_sqlite *ctx = arg;
    const void *hash;
    size_t hash_size;

    if (flux_request_decode_raw (msg,
                                 NULL,
                                 &hash,
                                 &hash_size) < 0)
        goto error;
    if (hash_size != ctx->hash_size) {
        errno = EPROTO;
        goto error;
    }
    if (content_sqlite_validate (ctx, hash, hash_size) < 0)
        goto error;
    if (flux_respond_raw (h, msg, NULL, 0) < 0)
        flux_log_error (h, "validate: flux_respond_raw");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "validate: flux_respond_error");
}

void checkpoint_get_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_sqlite *ctx = arg;
    const char *errstr = NULL;
    flux_error_t sql_error;
    json_t *a = NULL;
    int rc;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    while ((rc = sqlite3_step (ctx->checkpt_get_stmt)) == SQLITE_ROW) {
        const char *s;
        json_t *o = NULL;
        json_error_t error;

        s = (const char *)sqlite3_column_text (ctx->checkpt_get_stmt, 0);

        if (!(o = json_loads (s, 0, &error))) {
            /* recovery from version 0 checkpoint blobref not supported */
            errstr = error.text;
            errno = EINVAL;
            goto error;
        }

        if (json_array_append_new (a, o) < 0) {
            // jansson decrefs the new object on failure
            errno = ENOMEM;
            goto error;
        }
    }
    /* A step error (e.g. I/O error) must not be mistaken for end-of-data,
     * which would silently return a partial result or ENOENT.
     */
    if (rc != SQLITE_DONE) {
        log_sqlite_error (ctx, "checkpt_get: executing stmt");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &sql_error);
        goto error;
    }

    /* if no checkpoint entries, we return ENOENT */
    if (json_array_size (a) > 0) {
        if (flux_respond_pack (h,
                               msg,
                               "{s:O}",
                               "value", a) < 0)
            flux_log_error (h, "flux_respond_pack");
    }
    else {
        errno = ENOENT;
        goto error;
    }

    (void )sqlite3_reset (ctx->checkpt_get_stmt);
    json_decref (a);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "flux_respond_error");
    (void )sqlite3_reset (ctx->checkpt_get_stmt);
    json_decref (a);
}

void checkpoint_put_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_sqlite *ctx = arg;
    json_t *o;
    char *value = NULL;
    const char *errstr = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o}",
                             "value", &o) < 0)
        goto error;
    /* Flush any pending group-committed stores before recording a checkpoint
     * that may reference them, so the checkpoint is never durable ahead of its
     * blobs.
     */
    if (batch_commit (ctx) < 0)
        goto error;
    if (!(value = json_dumps (o, JSON_COMPACT))) {
        errstr = "failed to encode checkpoint value";
        errno = EINVAL;
        goto error;
    }
    if (sqlite3_bind_text (ctx->checkpt_put_stmt,
                           1,
                           value,
                           strlen (value),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt_put: binding value");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    if (sqlite3_step (ctx->checkpt_put_stmt) != SQLITE_DONE
                    && sqlite3_errcode (ctx->db) != SQLITE_CONSTRAINT) {
        log_sqlite_error (ctx, "checkpt_put: executing stmt");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    /* Update current_epoch to the id of the just-inserted checkpoint */
    ctx->current_epoch = sqlite3_last_insert_rowid (ctx->db);
    flux_log (ctx->h,
              LOG_DEBUG,
              "checkpoint-put: advanced epoch to %jd",
              (intmax_t)ctx->current_epoch);
    if (sqlite3_bind_int (ctx->checkpt_prune_stmt,
                          1,
                          ctx->max_checkpoints) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt_prune: binding count");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    if (sqlite3_step (ctx->checkpt_prune_stmt) != SQLITE_DONE) {
        log_sqlite_error (ctx, "checkpt_prune: executing stmt");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "flux_respond");
    (void )sqlite3_reset (ctx->checkpt_put_stmt);
    (void )sqlite3_reset (ctx->checkpt_prune_stmt);
    free (value);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "flux_respond_error");
    (void )sqlite3_reset (ctx->checkpt_put_stmt);
    (void )sqlite3_reset (ctx->checkpt_prune_stmt);
    free (value);
}

/* On failure, returns NULL with errno set and 'errp' filled with a
 * human-readable description for the client.
 */
static json_t *stats_checkpoints (struct content_sqlite *ctx,
                                  flux_error_t *errp)
{
    sqlite3_stmt *stmt = ctx->checkpt_get_all_stmt;
    json_t *checkpts = NULL;
    int rc;

    if (!(checkpts = json_array ())) {
        errprintf (errp, "checkpt_get_all: out of memory");
        errno = ENOMEM;
        return NULL;
    }

    while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
        int id;
        const char *s;
        json_t *o, *value;

        if ((id = sqlite3_column_int (stmt, 0)) < 0
            || !(s = (const char *)sqlite3_column_text (stmt, 1))) {
            log_sqlite_error (ctx, "checkpt_get_all: getting values");
            set_errno_from_sqlite_error (ctx);
            set_text_from_sqlite_error (ctx, errp);
            goto error;
        }
        if (!(value = json_loads (s, 0, NULL))) {
            flux_log (ctx->h,
                      LOG_ERR,
                      "invalid checkpoint value: %s",
                      s);
            continue;
        }
        if (!(o = json_pack ("{s:i s:o}",
                             "id", id,
                             "value", value))
            || json_array_append_new (checkpts, o) < 0) {
            json_decref (value);
            // jansson decrefs the new object on failure
            errprintf (errp, "checkpt_get_all: out of memory");
            errno = ENOMEM;
            goto error;
        }
    }
    /* A step error must not be mistaken for end-of-data (which would
     * silently return a truncated checkpoint list in the stats output).
     */
    if (rc != SQLITE_DONE) {
        log_sqlite_error (ctx, "checkpt_get_all: executing stmt");
        set_errno_from_sqlite_error (ctx);
        set_text_from_sqlite_error (ctx, errp);
        goto error;
    }

    (void )sqlite3_reset (ctx->checkpt_get_all_stmt);
    return checkpts;

error:
    ERRNO_SAFE_WRAP (json_decref, checkpts);
    (void )sqlite3_reset (ctx->checkpt_get_all_stmt);
    return NULL;
}

static void content_sqlite_closedb (struct content_sqlite *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        /* Commit any pending group-committed stores before teardown. */
        (void)batch_commit (ctx);
        if (ctx->validate_stmt) {
            if (sqlite3_finalize (ctx->validate_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize validate_stmt");
        }
        if (ctx->store_stmt) {
            if (sqlite3_finalize (ctx->store_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize store_stmt");
        }
        if (ctx->load_stmt) {
            if (sqlite3_finalize (ctx->load_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize load_stmt");
        }
        if (ctx->checkpt_get_stmt) {
            if (sqlite3_finalize (ctx->checkpt_get_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize checkpt_get_stmt");
        }
        if (ctx->checkpt_put_stmt) {
            if (sqlite3_finalize (ctx->checkpt_put_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize checkpt_put_stmt");
        }
        if (ctx->checkpt_prune_stmt) {
            if (sqlite3_finalize (ctx->checkpt_prune_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize checkpt_prune_stmt");
        }
        if (ctx->checkpt_get_all_stmt) {
            if (sqlite3_finalize (ctx->checkpt_get_all_stmt) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite_finalize checkpt_get_all_stmt");
        }
        if (ctx->db) {
            if (sqlite3_close (ctx->db) != SQLITE_OK)
                log_sqlite_error (ctx, "sqlite3_close");
        }
        errno = saved_errno;
    }
}

/* sqlite3_exec() callback from sql_objects_count query.
 * On success, return 0 and set *arg to the count result.
 * On error, return -1 which causes sqlite3_exec() to fail with SQLITE_ABORT.
 */
static int set_count (void *arg, int ncols, char **cols, char **col_names)
{
    int64_t *result = arg;
    int64_t count = 0;
    int rc = -1;

    if (ncols == 1) {
        errno = 0;
        count = strtoll (cols[0], NULL, 10);
        if (errno == 0) {
            *result = count;
            rc = 0;
        }
    }
    return rc; // returning -1 causes SQLITE_ABORT
}

static json_t *pack_tstat (tstat_t *ts)
{
    json_t *o;
    if (!(o = json_pack ("{s:i s:f s:f s:f s:f}",
                         "count", tstat_count (ts),
                         "min", tstat_min (ts),
                         "max", tstat_max (ts),
                         "mean", tstat_mean (ts),
                          "stddev", tstat_stddev (ts)))) {
        errno = ENOMEM;
        return NULL;
    }
    return o;
}

static unsigned long long get_file_size (const char *path)
{
    struct stat sb;

    if (stat (path, &sb) < 0)
        return 0;
    return sb.st_size;
}

static unsigned long long get_fs_free (const char *path)
{
    struct statvfs sb;

    if (statvfs (path, &sb) < 0)
        return 0;
    return sb.f_bsize * sb.f_bavail;
}

void stats_get_cb (flux_t *h,
                   flux_msg_handler_t *mh,
                   const flux_msg_t *msg,
                   void *arg)
{
    struct content_sqlite *ctx = arg;
    int64_t count;
    const char *errmsg = NULL;
    flux_error_t error;
    json_t *load_time = NULL;
    json_t *store_time = NULL;
    json_t *batch_size = NULL;
    json_t *checkpoints = NULL;

    if (sqlite3_exec (ctx->db,
                      sql_objects_count,
                      set_count,
                      &count,
                      NULL) != SQLITE_OK) {
        errmsg = set_text_from_sqlite_error (ctx, &error);
        errno = EPERM;
        goto error;
    }
    if (!(load_time = pack_tstat (&ctx->stats.load))
        || !(store_time = pack_tstat (&ctx->stats.store))
        || !(batch_size = pack_tstat (&ctx->stats.batch)))
        goto error;
    if (!(checkpoints = stats_checkpoints (ctx, &error))) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:I s:I s:I s:I s:O s:O s:O s:{s:s s:s s:f} s:O}",
                           "object_count", count,
                           "current_epoch", ctx->current_epoch,
                           "dbfile_size", get_file_size (ctx->dbfile),
                           "dbfile_free", get_fs_free (ctx->dbfile),
                           "load_time", load_time,
                           "store_time", store_time,
                           "batch_size", batch_size,
                           "config",
                             "journal_mode", ctx->journal_mode,
                             "synchronous", ctx->synchronous,
                             "batch_timeout", ctx->batch_timeout,
                           "checkpoints", checkpoints) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (load_time);
    json_decref (store_time);
    json_decref (batch_size);
    json_decref (checkpoints);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (load_time);
    json_decref (store_time);
    json_decref (batch_size);
    json_decref (checkpoints);
}

/* content-backing.mark - mark a batch of blobs to target epoch
 * Request: {"epoch":I, "hashes":[s,s,...]}  (hashes are blobref strings)
 * Response: {"marked":i}
 *
 * Raise the epoch of each named blob to at least 'epoch' (the GC horizon H),
 * protecting it from a later sweep.  This is the "mark" half of the online
 * mark-and-sweep GC driven by flux-gc: the tool walks the reachable KVS tree
 * and marks every reachable blob to H, then sweeps everything left below H.
 *
 * The update is UPDATE ... SET epoch = MAX(epoch, ?) so it is idempotent and
 * monotonic: re-marking never lowers an epoch, and a blob absent from the
 * store (already swept, or never stored) simply matches no row.  'marked' is
 * the number of rows actually changed, for the caller's progress accounting.
 *
 * The batch is capped at MARK_HASHES_MAX because the whole loop runs
 * synchronously on this module's reactor thread, blocking all other content
 * requests until it completes; the cap bounds that stall (and rejects a
 * malformed or hostile request that would otherwise pin the reactor).
 */
static void mark_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct content_sqlite *ctx = arg;
    int64_t target_epoch;
    json_t *hashes;
    size_t index;
    json_t *hash_str;
    sqlite3_stmt *stmt = NULL;
    int marked_count = 0;
    bool in_txn = false;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:o}",
                             "epoch", &target_epoch,
                             "hashes", &hashes) < 0)
        goto error;

    if (!json_is_array (hashes)) {
        errno = EPROTO;
        goto error;
    }
    /* Bound the synchronous work per request (see function comment). */
    if (json_array_size (hashes) > MARK_HASHES_MAX) {
        errprintf (&error,
                   "mark request of %zu hashes exceeds limit of %d",
                   json_array_size (hashes),
                   MARK_HASHES_MAX);
        errstr = error.text;
        errno = EINVAL;
        goto error;
    }

    /* Flush any pending group-committed stores first.  The mark below opens
     * its own transaction (SQLite has no nested transactions, so a BEGIN would
     * otherwise fail), and the UPDATE must see the just-stored blobs it may be
     * asked to mark.
     */
    if (batch_commit (ctx) < 0)
        goto error;

    /* Wrap the batch in a single transaction.  In autocommit mode each
     * sqlite3_step() below would be its own implicit transaction, so a mark of
     * N hashes would incur N commits (N WAL-frame appends and fsync-class
     * bookkeeping under synchronous=NORMAL) instead of one, roughly doubling
     * the cost of the batch.
     */
    if (sqlite3_exec (ctx->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "mark: BEGIN");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    in_txn = true;

    /* Prepare once and re-bind/re-step per hash (reset below after each). */
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_mark_blob,
                            -1,
                            &stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "mark: preparing statement");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }

    json_array_foreach (hashes, index, hash_str) {
        const char *blobref;
        char hash[BLOBREF_MAX_DIGEST_SIZE];
        ssize_t hash_len;

        if (!json_is_string (hash_str)) {
            errno = EPROTO;
            goto error;
        }

        blobref = json_string_value (hash_str);

        /* Convert blobref string to raw hash */
        if ((hash_len = blobref_strtohash (blobref, hash, sizeof (hash))) < 0) {
            errno = EPROTO;
            goto error;
        }

        if (hash_len != ctx->hash_size) {
            errno = EPROTO;
            goto error;
        }

        if (sqlite3_bind_int64 (stmt, 1, target_epoch) != SQLITE_OK) {
            log_sqlite_error (ctx, "mark: binding epoch");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        if (sqlite3_bind_text (stmt, 2, hash, hash_len, SQLITE_TRANSIENT) != SQLITE_OK) {
            log_sqlite_error (ctx, "mark: binding hash");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        if (sqlite3_step (stmt) != SQLITE_DONE) {
            log_sqlite_error (ctx, "mark: executing statement");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        /* sqlite3_changes() is 0 when the hash matched no row or MAX() left
         * the epoch unchanged, so 'marked' counts only real updates.  Reset
         * the statement to reuse it for the next hash.
         */
        marked_count += sqlite3_changes (ctx->db);
        sqlite3_reset (stmt);
    }

    sqlite3_finalize (stmt);
    stmt = NULL;
    if (sqlite3_exec (ctx->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "mark: COMMIT");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    in_txn = false;

    if (flux_respond_pack (h, msg, "{s:i}", "marked", marked_count) < 0)
        flux_log_error (h, "mark: flux_respond_pack");
    return;

error:
    if (stmt)
        sqlite3_finalize (stmt);
    if (in_txn)
        sqlite3_exec (ctx->db, "ROLLBACK", NULL, NULL, NULL);
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "mark: flux_respond_error");
}

/* content-backing.sweep - delete a bounded batch of blobs with epoch < H
 * Request:  {"epoch":I, "cursor":I, "high_water":I, "delete_cap":i, "window":i}
 * Response: {"deleted":I, "cursor":I}
 *
 * Delete blobs with epoch < 'epoch' (the GC horizon H) whose rowid lies in
 * (cursor, min(high_water, cursor+window)], ascending by rowid, stopping after
 * 'delete_cap' deletions.  Returns the number deleted and a new cursor for the
 * caller to pass to the next call.  The caller sweeps by looping until the
 * cursor reaches the high-water rowid it froze at the start of the run (via
 * gc-info); a call that deletes nothing still advances the cursor by the
 * window, so the loop always makes progress and terminates deterministically.
 *
 * Two caps bound the synchronous per-call work independently: 'delete_cap'
 * limits rows deleted (the dominant cost) and 'window' limits rows scanned (so
 * a sparse span does not stall scanning for delete_cap matches).  Both are
 * clamped to server ceilings rather than rejected -- since termination is by
 * cursor, not batch size, a clamped call is harmless.
 *
 * The rowid cursor makes the whole sweep a single ascending pass: every row at
 * or below the returned cursor is settled (deleted, or epoch >= H and staying
 * so, since epochs only rise), so it is never rescanned.  Bounding at
 * high_water keeps the sweep from chasing blobs stored after the run began.
 */
static void sweep_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct content_sqlite *ctx = arg;
    int64_t threshold_epoch;
    int64_t cursor;
    int64_t high_water;
    int delete_cap;
    int window;
    sqlite3_stmt *select_stmt = NULL;
    sqlite3_stmt *delete_stmt = NULL;
    int64_t *rowids = NULL;
    int64_t scan_limit;
    int64_t deleted = 0;
    bool in_txn = false;
    int n = 0;
    int i;
    int rc;
    const char *errstr = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:I s:I s:i s:i}",
                             "epoch", &threshold_epoch,
                             "cursor", &cursor,
                             "high_water", &high_water,
                             "delete_cap", &delete_cap,
                             "window", &window) < 0)
        goto error;

    if (cursor < 0 || high_water < 0 || delete_cap <= 0 || window <= 0) {
        errno = EINVAL;
        goto error;
    }
    /* Clamp both caps to their ceilings (see SWEEP_DELETE_MAX / _WINDOW_MAX). */
    if (delete_cap > SWEEP_DELETE_MAX)
        delete_cap = SWEEP_DELETE_MAX;
    if (window > SWEEP_WINDOW_MAX)
        window = SWEEP_WINDOW_MAX;

    /* Scan the window (cursor, scan_limit], capped at high_water. */
    scan_limit = cursor + window;
    if (scan_limit > high_water)
        scan_limit = high_water;

    if (!(rowids = malloc (delete_cap * sizeof (rowids[0])))) {
        errno = ENOMEM;
        goto error;
    }

    /* Flush any pending group-committed stores first, so the select scans and
     * the delete below run against committed table state (and the delete's own
     * BEGIN does not fail -- SQLite has no nested transactions).
     */
    if (batch_commit (ctx) < 0)
        goto error;

    /* Collect the batch of garbage rowids first, then delete them.  Draining
     * the SELECT fully before issuing any DELETE avoids modifying the table
     * while a scan against it is open (undefined behavior in SQLite).
     */
    if (sqlite3_prepare_v2 (ctx->db, sql_sweep_select, -1, &select_stmt, NULL)
            != SQLITE_OK) {
        log_sqlite_error (ctx, "sweep: preparing select");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    if (sqlite3_bind_int64 (select_stmt, 1, threshold_epoch) != SQLITE_OK
        || sqlite3_bind_int64 (select_stmt, 2, cursor) != SQLITE_OK
        || sqlite3_bind_int64 (select_stmt, 3, scan_limit) != SQLITE_OK
        || sqlite3_bind_int (select_stmt, 4, delete_cap) != SQLITE_OK) {
        log_sqlite_error (ctx, "sweep: binding select");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
        rowids[n++] = sqlite3_column_int64 (select_stmt, 0);
    if (rc != SQLITE_DONE) {
        log_sqlite_error (ctx, "sweep: executing select");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    sqlite3_finalize (select_stmt);
    select_stmt = NULL;

    /* Delete the collected rowids in one transaction (as in mark_cb). */
    if (sqlite3_prepare_v2 (ctx->db, sql_sweep_delete, -1, &delete_stmt, NULL)
            != SQLITE_OK) {
        log_sqlite_error (ctx, "sweep: preparing delete");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    if (sqlite3_exec (ctx->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "sweep: BEGIN");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    in_txn = true;
    for (i = 0; i < n; i++) {
        if (sqlite3_bind_int64 (delete_stmt, 1, rowids[i]) != SQLITE_OK
            || sqlite3_step (delete_stmt) != SQLITE_DONE) {
            log_sqlite_error (ctx, "sweep: executing delete");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }
        deleted += sqlite3_changes (ctx->db);
        sqlite3_reset (delete_stmt);
    }
    sqlite3_finalize (delete_stmt);
    delete_stmt = NULL;
    if (sqlite3_exec (ctx->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "sweep: COMMIT");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    in_txn = false;

    /* Advance the cursor.  If delete_cap bounded the scan (the select returned
     * a full batch), the window may still hold garbage past the last row, so
     * resume just past it; otherwise the whole window is settled.  rowids are
     * ascending, so the last one is the highest.
     */
    if (n == delete_cap)
        cursor = rowids[n - 1];
    else
        cursor = scan_limit;

    free (rowids);
    if (flux_respond_pack (h,
                           msg,
                           "{s:I s:I}",
                           "deleted", deleted,
                           "cursor", cursor) < 0)
        flux_log_error (h, "sweep: flux_respond_pack");
    return;

error:
    if (select_stmt)
        sqlite3_finalize (select_stmt);
    if (delete_stmt)
        sqlite3_finalize (delete_stmt);
    if (in_txn)
        sqlite3_exec (ctx->db, "ROLLBACK", NULL, NULL, NULL);
    free (rowids);
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "sweep: flux_respond_error");
}

/* content-backing.gc-info - get GC information
 * Request: {"epoch":I get_count?b}
 * Response: {"current_epoch":I "high_water":I candidates?I}
 *
 * 'high_water' is MAX(rowid) of the objects table -- the largest rowid present
 * when the run begins.  flux-gc freezes it and bounds the sweep at it so the
 * sweep terminates deterministically without chasing blobs stored after the
 * run began (which get a higher rowid and epoch >= H).  It is cheap: MAX(rowid)
 * on a rowid table reads the last btree entry, no scan.
 *
 * 'candidates' (the count of blobs with epoch < the requested threshold) is
 * only computed and returned when 'get_count' is true.  It requires a COUNT(*)
 * over the objects table -- an UNBOUNDED full-table scan that runs synchronously
 * on this module's single reactor thread, blocking all other content traffic
 * for its duration.  On a large production store that stall can be seconds or
 * more, so 'get_count' must NOT be used on a hot path.  flux-gc never sets it
 * (it reads only current_epoch and high_water, and the count is not even a
 * meaningful "reclaimable" estimate before the mark phase runs, since it
 * includes reachable data); the count exists solely for the test suite and
 * deliberate, low-frequency ad-hoc inspection where the stall is acceptable.
 */
static void gc_info_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_sqlite *ctx = arg;
    int64_t threshold_epoch;
    int get_count = 0;
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *hw_stmt = NULL;
    int64_t candidates = 0;
    int64_t high_water = 0;
    const char *errstr = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s?b}",
                             "epoch", &threshold_epoch,
                             "get_count", &get_count) < 0)
        goto error;

    /* MAX(rowid); NULL (empty table) reads back as 0 via column_int64. */
    if (sqlite3_prepare_v2 (ctx->db, sql_max_rowid, -1, &hw_stmt, NULL)
            != SQLITE_OK
        || sqlite3_step (hw_stmt) != SQLITE_ROW) {
        log_sqlite_error (ctx, "gc-info: querying high_water");
        set_errno_from_sqlite_error (ctx);
        errstr = set_text_from_sqlite_error (ctx, &error);
        goto error;
    }
    high_water = sqlite3_column_int64 (hw_stmt, 0);

    if (get_count) {
        /* Unbounded full-table scan on the reactor thread -- test / ad-hoc
         * inspection only, never a hot path (see function comment).
         */
        if (sqlite3_prepare_v2 (ctx->db,
                                sql_count_sweep_candidates,
                                -1,
                                &stmt,
                                NULL) != SQLITE_OK) {
            log_sqlite_error (ctx, "gc-info: preparing statement");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        if (sqlite3_bind_int64 (stmt, 1, threshold_epoch) != SQLITE_OK) {
            log_sqlite_error (ctx, "gc-info: binding epoch");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        if (sqlite3_step (stmt) != SQLITE_ROW) {
            log_sqlite_error (ctx, "gc-info: executing statement");
            set_errno_from_sqlite_error (ctx);
            errstr = set_text_from_sqlite_error (ctx, &error);
            goto error;
        }

        candidates = sqlite3_column_int64 (stmt, 0);
    }

    if (flux_respond_pack (h,
                           msg,
                           get_count ? "{s:I s:I s:I}" : "{s:I s:I}",
                           "current_epoch", ctx->current_epoch,
                           "high_water", high_water,
                           "candidates", candidates) < 0)
        flux_log_error (h, "gc-info: flux_respond_pack");

    sqlite3_finalize (hw_stmt);
    sqlite3_finalize (stmt);
    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "gc-info: flux_respond_error");
    if (hw_stmt)
        sqlite3_finalize (hw_stmt);
    if (stmt)
        sqlite3_finalize (stmt);
}

/* Check if epoch column exists in objects table.
 * Returns 1 if exists, 0 if not, -1 on error.
 */
static int epoch_column_exists (struct content_sqlite *ctx)
{
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    int rc;

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_table_info,
                            -1,
                            &stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing table_info query");
        return -1;
    }

    while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
        const unsigned char *col_name = sqlite3_column_text (stmt, 1);
        if (col_name && streq ((const char *)col_name, "epoch")) {
            exists = 1;
            break;
        }
    }

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        log_sqlite_error (ctx, "querying table_info");
        sqlite3_finalize (stmt);
        return -1;
    }

    sqlite3_finalize (stmt);
    return exists;
}

/* Add epoch column to objects table if it doesn't exist.
 */
static int migrate_add_epoch_column (struct content_sqlite *ctx)
{
    int exists = epoch_column_exists (ctx);

    if (exists < 0)
        return -1;

    if (exists) {
        flux_log (ctx->h, LOG_DEBUG, "epoch column already exists");
        return 0;
    }

    flux_log (ctx->h, LOG_INFO, "adding epoch column to objects table");
    if (sqlite3_exec (ctx->db,
                      sql_alter_objects_add_epoch,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "adding epoch column");
        set_errno_from_sqlite_error (ctx);
        return -1;
    }

    return 0;
}

/* Initialize current_epoch from MAX(checkpt_v2.id).
 * Returns 0 on success, -1 on error.
 */
static int init_current_epoch (struct content_sqlite *ctx)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_get_max_checkpt_id,
                            -1,
                            &stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing get_max_checkpt_id query");
        return -1;
    }

    rc = sqlite3_step (stmt);
    if (rc == SQLITE_ROW) {
        if (sqlite3_column_type (stmt, 0) == SQLITE_NULL) {
            ctx->current_epoch = 0;  // No checkpoints yet
        }
        else {
            ctx->current_epoch = sqlite3_column_int64 (stmt, 0);
        }
    }
    else {
        log_sqlite_error (ctx, "querying max checkpoint id");
        sqlite3_finalize (stmt);
        return -1;
    }

    sqlite3_finalize (stmt);
    flux_log (ctx->h,
              LOG_DEBUG,
              "initialized current_epoch=%jd",
              (intmax_t)ctx->current_epoch);
    return 0;
}

/* Open the database file ctx->dbfile and set up the database.
 */
static int content_sqlite_opendb (struct content_sqlite *ctx, bool truncate)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    char s[128];
    int64_t count;

    if (truncate)
        (void)unlink (ctx->dbfile);

    if (sqlite3_open_v2 (ctx->dbfile, &ctx->db, flags, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "opening %s", ctx->dbfile);
        goto error;
    }
    snprintf (s, sizeof (s), "PRAGMA journal_mode=%s", ctx->journal_mode);
    if (sqlite3_exec (ctx->db,
                      s,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'journal_mode' pragma");
        goto error;
    }
    snprintf (s, sizeof (s), "PRAGMA synchronous=%s", ctx->synchronous);
    if (sqlite3_exec (ctx->db,
                      s,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'synchronous' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA locking_mode=EXCLUSIVE",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'locking_mode' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA quick_check",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'quick_check' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      sql_create_table,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "creating object table");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      sql_create_table_checkpt_v2,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "creating checkpt table");
        goto error;
    }
    if (migrate_add_epoch_column (ctx) < 0)
        goto error;
    if (init_current_epoch (ctx) < 0)
        goto error;
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_load,
                            -1,
                            &ctx->load_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing load stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_store,
                            -1,
                            &ctx->store_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing store stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_validate,
                            -1,
                            &ctx->validate_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing validate stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_get_v2,
                            -1,
                            &ctx->checkpt_get_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt_get stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_put_v2,
                            -1,
                            &ctx->checkpt_put_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt_put stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_prune,
                            -1,
                            &ctx->checkpt_prune_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt prune stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_get_all,
                            -1,
                            &ctx->checkpt_get_all_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt get_all stmt");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      sql_objects_count,
                      set_count,
                      &count,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "querying objects count");
        goto error;
    }
    flux_log (ctx->h,
              LOG_DEBUG,
              "%s (%jd objects) journal_mode=%s synchronous=%s",
              ctx->dbfile,
              (intmax_t)count,
              ctx->journal_mode,
              ctx->synchronous);
    return 0;
error:
    set_errno_from_sqlite_error (ctx);
    return -1;
}

static int content_sqlite_checkpt_migrate (struct content_sqlite *ctx)
{
    sqlite3_stmt *checkpt_get_v1_stmt = NULL;
    json_t *o = NULL;
    const char *s;
    int rv = -1;

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_get_v1,
                            -1,
                            &checkpt_get_v1_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt_get migrate stmt");
        goto error;
    }

    if (sqlite3_bind_text (checkpt_get_v1_stmt,
                           1,
                           (char *)KVS_DEFAULT_CHECKPOINT,
                           strlen (KVS_DEFAULT_CHECKPOINT),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt migrate: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }

    /* no checkpoint stored, just drop the table */
    if (sqlite3_step (checkpt_get_v1_stmt) != SQLITE_ROW)
        goto drop;

    s = (const char *)sqlite3_column_text (checkpt_get_v1_stmt, 0);

    if (!(o = json_loads (s, 0, NULL))) {
        /* version 0 checkpoint blobref not supported */
        flux_log (ctx->h,
                  LOG_ERR,
                  "invalid checkpoint format in legacy checkpt table");
        goto error;
    }

    if (sqlite3_bind_text (ctx->checkpt_put_stmt,
                           1,
                           s,
                           strlen (s),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt_put: binding value");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_step (ctx->checkpt_put_stmt) != SQLITE_DONE
                    && sqlite3_errcode (ctx->db) != SQLITE_CONSTRAINT) {
        log_sqlite_error (ctx, "checkpt_put: executing stmt");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }

    (void )sqlite3_reset (checkpt_get_v1_stmt);
    (void )sqlite3_reset (ctx->checkpt_put_stmt);

drop:
    if (sqlite3_exec (ctx->db,
                      sql_drop_checkpt,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "drop checkpt");
        goto error;
    }
    rv = 0;
error:
    if (checkpt_get_v1_stmt) {
        if (sqlite3_finalize (checkpt_get_v1_stmt) != SQLITE_OK)
            log_sqlite_error (ctx, "sqlite_finalize checkpt_get_v1_stmt");
    }
    json_decref (o);
    return rv;
}

static int content_sqlite_table_exists (struct content_sqlite *ctx,
                                        const char *table_name,
                                        bool *exists)
{
    sqlite3_stmt *table_list_stmt = NULL;
    int rv = 0;

    if (sqlite3_prepare_v2 (ctx->db,
                            sql_table_list,
                            -1,
                            &table_list_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing sql_table_list stmt");
        goto cleanup;
    }

    (*exists) = false;
    while (sqlite3_step (table_list_stmt) == SQLITE_ROW) {
        const char *s = (const char *)sqlite3_column_text (table_list_stmt, 0);
        if (sqlite3_column_type (table_list_stmt, 0) != SQLITE_TEXT) {
            flux_log (ctx->h, LOG_ERR, "table_list: tbl_name not a string");
            errno = EINVAL;
            goto cleanup;
        }
        if (streq (s, table_name)) {
            (*exists) = true;
            break;
        }
    }

    rv = 0;
cleanup:
    if (table_list_stmt) {
        if (sqlite3_finalize (table_list_stmt) != SQLITE_OK)
            log_sqlite_error (ctx, "sqlite_finalize table_list_stmt");
    }
    return rv;
}

static void content_sqlite_destroy (struct content_sqlite *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        flux_watcher_destroy (ctx->batch_timer);
        free (ctx->dbfile);
        free (ctx->lzo_buf);
        free (ctx->hashfun);
        free (ctx->journal_mode);
        free (ctx->synchronous);
        free (ctx);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.load",
        load_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.store",
        store_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.validate",
        validate_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.checkpoint-get",
        checkpoint_get_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.checkpoint-put",
        checkpoint_put_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-sqlite.stats-get",
        stats_get_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.mark",
        mark_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.sweep",
        sweep_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content-backing.gc-info",
        gc_info_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static struct content_sqlite *content_sqlite_create (flux_t *h)
{
    struct content_sqlite *ctx;
    const char *statedir;
    const char *s;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->lzo_buf = calloc (1, lzo_buf_chunksize)))
        goto error;
    ctx->lzo_bufsize = lzo_buf_chunksize;
    ctx->h = h;
    if (set_config (&ctx->journal_mode, "WAL") < 0)
        goto error;
    if (set_config (&ctx->synchronous, "NORMAL") < 0)
        goto error;
    ctx->max_checkpoints = MAX_CHECKPOINTS_DEFAULT;
    ctx->batch_timeout = 0.1;   // enable group commit by default (0 disables)
    list_head_init (&ctx->batch_pending);

    /* Some tunables:
     * - the hash function, e.g. sha1, sha256
     * - the maximum blob size
     * - path to sqlite file
     */
    if (!(s = flux_attr_get (h, "content.hash"))
        || !(ctx->hashfun = strdup (s))
        || (ctx->hash_size = blobref_validate_hashtype (s)) < 0) {
        flux_log_error (h, "content.hash");
        goto error;
    }

    /* Use 'statedir' as the location for content.sqlite file.
     */
    if (!(statedir = flux_attr_get (h, "statedir"))) {
        flux_log_error (h, "statedir is not set");
        goto error;
    }
    /* If 'statedir-cleanup' is set to 1, enable pragmas that increase
     * performance but risk database corruption on a crash since the
     * directory is temporary and the database is not being preserved
     * after a crash anyway.
     */
    if ((s = flux_attr_get (h, "statedir-cleanup")) && streq (s, "1")) {
        if (set_config (&ctx->journal_mode, "OFF") < 0)
            goto error;
        if (set_config (&ctx->synchronous, "OFF") < 0)
            goto error;
    }
    if (asprintf (&ctx->dbfile, "%s/content.sqlite", statedir) < 0)
        goto error;

    /* If dbfile exists, we are restarting.
     * If existing dbfile does not have the right permissions, fail early.
     */
    if (access (ctx->dbfile, F_OK) == 0) {
        if (access (ctx->dbfile, R_OK | W_OK) < 0) {
            flux_log_error (h, "%s", ctx->dbfile);
            goto error;
        }
    }

    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    return ctx;
error:
    content_sqlite_destroy (ctx);
    return NULL;
}

static bool journal_mode_valid (const char *s)
{
    /* N.B. sqlite is case sensitive by default, we assume it here */
    if (!streq (s, "DELETE")
        && !streq (s, "TRUNCATE")
        && !streq (s, "PERSIST")
        && !streq (s, "MEMORY")
        && !streq (s, "WAL")
        && !streq (s, "OFF"))
        return false;
    return true;
}

static bool synchronous_valid (const char *s)
{
    /* N.B. sqlite is case sensitive by default, we assume it here */
    if (!streq (s, "EXTRA")
        && !streq (s, "FULL")
        && !streq (s, "NORMAL")
        && !streq (s, "OFF"))
        return false;
    return true;
}

static int process_config (struct content_sqlite *ctx,
                           const flux_conf_t *conf)
{
    flux_error_t error;
    const char *journal_mode = NULL;
    const char *synchronous = NULL;
    int tmp_max_checkpoints = ctx->max_checkpoints;
    const char *batch_timeout = NULL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s s?s s?i s?s}}",
                          "content-sqlite",
                            "journal_mode", &journal_mode,
                            "synchronous", &synchronous,
                            "max_checkpoints", &tmp_max_checkpoints,
                            "batch_timeout", &batch_timeout) < 0) {
        flux_log_error (ctx->h, "%s", error.text);
        return -1;
    }
    if (batch_timeout && fsd_parse_duration (batch_timeout,
                                             &ctx->batch_timeout) < 0) {
        flux_log (ctx->h, LOG_ERR, "invalid content-sqlite.batch_timeout");
        errno = EINVAL;
        return -1;
    }
    if (journal_mode) {
        if (!journal_mode_valid (journal_mode)) {
            flux_log (ctx->h, LOG_ERR, "invalid journal_mode config");
            errno = EINVAL;
            return -1;
        }
        if (set_config (&ctx->journal_mode, journal_mode) < 0)
            return -1;
    }
    if (synchronous) {
        if (!synchronous_valid (synchronous)) {
            flux_log (ctx->h, LOG_ERR, "invalid synchronous config");
            errno = EINVAL;
            return -1;
        }
        if (set_config (&ctx->synchronous, synchronous) < 0)
            return -1;
    }
    if (tmp_max_checkpoints <= 0) {
        flux_log (ctx->h, LOG_ERR, "invalid max_checkpoints config");
        errno = EINVAL;
        return -1;
    }
    ctx->max_checkpoints = tmp_max_checkpoints;

    return 0;
}

static int process_args (struct content_sqlite *ctx,
                         int argc,
                         char **argv,
                         bool *truncate)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (strstarts (argv[i], "journal_mode=")) {
            if (!journal_mode_valid (argv[i] + 13)) {
                flux_log (ctx->h, LOG_ERR, "invalid journal_mode specified");
                errno = EINVAL;
                return -1;
            }
            if (set_config (&ctx->journal_mode, argv[i] + 13) < 0)
                return -1;
        }
        else if (strstarts (argv[i], "synchronous=")) {
            if (!synchronous_valid (argv[i] + 12)) {
                flux_log (ctx->h, LOG_ERR, "invalid synchronous specified");
                errno = EINVAL;
                return -1;
            }
            if (set_config (&ctx->synchronous, argv[i] + 12) < 0)
                return -1;
        }
        else if (strstarts (argv[i], "max-checkpoints=")) {
            char *endptr;
            int tmp_max_checkpoints;
            errno = 0;
            tmp_max_checkpoints = strtoul (argv[i] + 16, &endptr, 10);
            if (errno != 0
                || *endptr != '\0'
                || tmp_max_checkpoints <= 0) {
                flux_log (ctx->h, LOG_ERR, "invalid max-checkpoints specified");
                errno = EINVAL;
                return -1;
            }
            ctx->max_checkpoints = tmp_max_checkpoints;
        }
        else if (strstarts (argv[i], "batch-timeout=")) {
            double tmp;
            if (fsd_parse_duration (argv[i] + 14, &tmp) < 0) {
                flux_log (ctx->h, LOG_ERR, "invalid batch-timeout specified");
                errno = EINVAL;
                return -1;
            }
            ctx->batch_timeout = tmp;
        }
        else if (streq ("truncate", argv[i])) {
            *truncate = true;
        }
        else {
            flux_log (ctx->h, LOG_ERR, "Unknown module option: '%s'", argv[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct content_sqlite *ctx;
    bool truncate = false;
    bool exists = false;
    int rc = -1;

    if (!(ctx = content_sqlite_create (h))) {
        flux_log_error (h, "content_sqlite_create failed");
        return -1;
    }
    if (process_config (ctx, flux_get_conf (h)) < 0)
        goto done;
    if (process_args (ctx, argc, argv, &truncate) < 0)
        goto done;
    /* Create the batch timeout watcher only when group commit is enabled;
     * the NULL batch_timer otherwise signals batching is off.
     */
    if (ctx->batch_timeout > 0.
        && !(ctx->batch_timer = flux_timer_watcher_create (
                                        flux_get_reactor (h),
                                        ctx->batch_timeout,
                                        0.,
                                        batch_timer_cb,
                                        ctx))) {
        flux_log_error (h, "flux_timer_watcher_create");
        goto done;
    }
    if (content_sqlite_opendb (ctx, truncate) < 0)
        goto done;
    if (content_sqlite_table_exists (ctx, "checkpt", &exists) < 0
        || (exists
            && content_sqlite_checkpt_migrate (ctx) < 0))
        goto done;
    if (content_register_service (h, "content-backing") < 0)
        goto done;
    if (content_register_backing_store (h, "content-sqlite") < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }
    rc = 0;
done_unreg:
    (void)content_unregister_backing_store (h);
done:
    content_sqlite_closedb (ctx);
    content_sqlite_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
