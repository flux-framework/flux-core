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
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/monotime.h"

#include "src/common/libcontent/content-util.h"
#include "ccan/str/str.h"

const size_t lzo_buf_chunksize = 1024*1024;
const size_t compression_threshold = 256; /* compress blobs >= this size */

const char *sql_create_table = "CREATE TABLE if not exists objects("
                               "  hash BLOB PRIMARY KEY,"
                               "  size INT,"
                               "  object BLOB"
                               ");";
const char *sql_load = "SELECT object,size FROM objects"
                       "  WHERE hash = ?1 LIMIT 1";
const char *sql_store = "INSERT INTO objects (hash,size,object) "
                        "  values (?1, ?2, ?3)";
const char *sql_objects_count = "SELECT count(1) FROM objects";

const char *sql_create_table_checkpt = "CREATE TABLE if not exists checkpt("
                                       "  key TEXT UNIQUE,"
                                       "  value TEXT"
                                       ");";
const char *sql_checkpt_get = "SELECT value FROM checkpt"
                              "  WHERE key = ?1";
const char *sql_checkpt_put = "REPLACE INTO checkpt (key,value) "
                              "  values (?1, ?2)";

struct content_stats {
    tstat_t load;
    tstat_t store;
};

struct content_sqlite {
    flux_msg_handler_t **handlers;
    char *dbfile;
    sqlite3 *db;
    sqlite3_stmt *load_stmt;
    sqlite3_stmt *store_stmt;
    sqlite3_stmt *checkpt_get_stmt;
    sqlite3_stmt *checkpt_put_stmt;
    flux_t *h;
    char *hashfun;
    int hash_size;
    size_t lzo_bufsize;
    void *lzo_buf;
    struct content_stats stats;
    char *journal_mode;
    char *synchronous;
    bool truncate;
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

static void log_sqlite_error (struct content_sqlite *ctx, const char *fmt, ...)
{
    char buf[64];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);

    if (ctx->db) {
        const char *errmsg = sqlite3_errmsg (ctx->db);
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s: %s(%d)",
                  buf,
                  errmsg ? errmsg : "unknown error code",
                  sqlite3_extended_errcode (ctx->db));
    }
    else
        flux_log (ctx->h, LOG_ERR, "%s: unknown error, no sqlite3 handle", buf);
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
    /* N.B. ignore SQLITE_CONSTRAINT errors - it means the insert failed
     * because it violated the implicit primary key uniqueness constraint.
     * Blob and blobref are indeed stored and storage is conserved - success!
     */
    if (sqlite3_step (ctx->store_stmt) != SQLITE_DONE
                    && sqlite3_errcode (ctx->db) != SQLITE_CONSTRAINT) {
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
    struct timespec t0;

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0) {
        flux_log_error (h, "store: request decode failed");
        goto error;
    }
    monotime (&t0);
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
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "store: flux_respond_error");
}

void checkpoint_get_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_sqlite *ctx = arg;
    const char *key;
    char *s;
    json_t *o = NULL;
    const char *errstr = NULL;
    json_error_t error;

    if (flux_request_unpack (msg, NULL, "{s:s}", "key", &key) < 0)
        goto error;
    if (sqlite3_bind_text (ctx->checkpt_get_stmt,
                           1,
                           (char *)key,
                           strlen (key),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt_get: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_step (ctx->checkpt_get_stmt) != SQLITE_ROW) {
        errno = ENOENT;
        goto error;
    }
    s = (char *)sqlite3_column_text (ctx->checkpt_get_stmt, 0);
    if (!(o = json_loads (s, 0, &error))) {
        if (blobref_validate (s) < 0) {
            errstr = error.text;
            errno = EINVAL;
            goto error;
        }
        /* assume "version 0" if value is a bare blobref and return it
         * in a json envelope */
        if (!(o = json_pack ("{s:i s:s s:i s:f}",
                             "version", 0,
                             "rootref", s,
                             "sequence", 0,
                             "timestamp", 0.))) {
            errstr = "failed to encode blobref in json envelope";
            errno = EINVAL;
            goto error;
        }
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:O}",
                           "value", o) < 0)
        flux_log_error (h, "flux_respond_pack");
    (void )sqlite3_reset (ctx->checkpt_get_stmt);
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "flux_respond_error");
    (void )sqlite3_reset (ctx->checkpt_get_stmt);
    json_decref (o);
}

void checkpoint_put_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_sqlite *ctx = arg;
    const char *key;
    json_t *o;
    char *value = NULL;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:o}",
                             "key", &key,
                             "value", &o) < 0)
        goto error;
    if (!(value = json_dumps (o, JSON_COMPACT))) {
        errstr = "failed to encode checkpoint value";
        errno = EINVAL;
        goto error;
    }
    if (sqlite3_bind_text (ctx->checkpt_put_stmt,
                           1,
                           (char *)key,
                           strlen (key),
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "checkpt_put: binding key");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    if (sqlite3_bind_text (ctx->checkpt_put_stmt,
                           2,
                           value,
                           strlen (value),
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
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "flux_respond");
    (void )sqlite3_reset (ctx->checkpt_put_stmt);
    free (value);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "flux_respond_error");
    (void )sqlite3_reset (ctx->checkpt_put_stmt);
    free (value);
}

static void content_sqlite_closedb (struct content_sqlite *ctx)
{
    if (ctx) {
        int saved_errno = errno;
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
    int *result = arg;
    int count = 0;
    int rc = -1;

    if (ncols == 1) {
        errno = 0;
        count = strtoul (cols[0], NULL, 10);
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
    int count;
    const char *errmsg = NULL;
    json_t *load_time = NULL;
    json_t *store_time = NULL;

    if (sqlite3_exec (ctx->db,
                      sql_objects_count,
                      set_count,
                      &count,
                      NULL) != SQLITE_OK) {
        errmsg = sqlite3_errmsg (ctx->db);
        errno = EPERM;
        goto error;
    }
    if (!(load_time = pack_tstat (&ctx->stats.load))
        || !(store_time = pack_tstat (&ctx->stats.store)))
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:I s:I s:O s:O s:{s:s s:s}}",
                           "object_count", count,
                           "dbfile_size", get_file_size (ctx->dbfile),
                           "dbfile_free", get_fs_free (ctx->dbfile),
                           "load_time", load_time,
                           "store_time", store_time,
                           "config",
                             "journal_mode", ctx->journal_mode,
                             "synchronous", ctx->synchronous) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (load_time);
    json_decref (store_time);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (load_time);
    json_decref (store_time);
}

/* Open the database file ctx->dbfile and set up the database.
 */
static int content_sqlite_opendb (struct content_sqlite *ctx, bool truncate)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    char s[128];
    int count;

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
                      sql_create_table_checkpt,
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "creating checkpt table");
        goto error;
    }
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
                            sql_checkpt_get,
                            -1,
                            &ctx->checkpt_get_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt_get stmt");
        goto error;
    }
    if (sqlite3_prepare_v2 (ctx->db,
                            sql_checkpt_put,
                            -1,
                            &ctx->checkpt_put_stmt,
                            NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "preparing checkpt_put stmt");
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
              "%s (%d objects) journal_mode=%s synchronous=%s",
              ctx->dbfile,
              count,
              ctx->journal_mode,
              ctx->synchronous);
    return 0;
error:
    set_errno_from_sqlite_error (ctx);
    return -1;
}

static void content_sqlite_destroy (struct content_sqlite *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
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
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",    load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",   store_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.checkpoint-get",
                            checkpoint_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.checkpoint-put",
                            checkpoint_put_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-sqlite.stats-get",
                            stats_get_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};

static struct content_sqlite *content_sqlite_create (flux_t *h)
{
    struct content_sqlite *ctx;
    const char *dbdir;
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

    /* Prefer 'statedir' as the location for content.sqlite file, if set.
     * Otherwise use 'rundir', and enable pragmas that increase performance
     * but risk database corruption on a crash (since rundir is temporary
     * and the database is not being preserved after a crash anyway).
     */
    if (!(dbdir = flux_attr_get (h, "statedir"))) {
        dbdir = flux_attr_get (h, "rundir");
        if (set_config (&ctx->journal_mode, "OFF") < 0)
            goto error;
        if (set_config (&ctx->synchronous, "OFF") < 0)
            goto error;
    }
    if (!dbdir) {
        flux_log_error (h, "neither statedir nor rundir are set");
        goto error;
    }
    if (asprintf (&ctx->dbfile, "%s/content.sqlite", dbdir) < 0)
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

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s s?s}}",
                          "content-sqlite",
                            "journal_mode", &journal_mode,
                            "synchronous", &synchronous) < 0) {
        flux_log_error (ctx->h, "%s", error.text);
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
    int rc = -1;

    if (!(ctx = content_sqlite_create (h))) {
        flux_log_error (h, "content_sqlite_create failed");
        return -1;
    }
    if (process_config (ctx, flux_get_conf (h)) < 0)
        goto done;
    if (process_args (ctx, argc, argv, &truncate) < 0)
        goto done;
    if (content_sqlite_opendb (ctx, truncate) < 0)
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
