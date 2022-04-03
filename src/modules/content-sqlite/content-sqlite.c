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
#include <unistd.h>
#include <sqlite3.h>
#include <lz4.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"

#include "src/common/libcontent/content-util.h"

const size_t lzo_buf_chunksize = 1024*1024;
const size_t compression_threshold = 256; /* compress blobs >= this size */

const char *sql_create_table = "CREATE TABLE if not exists objects("
                               "  hash CHAR(20) PRIMARY KEY,"
                               "  size INT,"
                               "  object BLOB"
                               ");";
const char *sql_load = "SELECT object,size FROM objects"
                       "  WHERE hash = ?1 LIMIT 1";
const char *sql_store = "INSERT INTO objects (hash,size,object) "
                        "  values (?1, ?2, ?3)";

const char *sql_create_table_checkpt = "CREATE TABLE if not exists checkpt("
                                       "  key TEXT UNIQUE,"
                                       "  value TEXT"
                                       ");";
const char *sql_checkpt_get = "SELECT value FROM checkpt"
                              "  WHERE key = ?1";
const char *sql_checkpt_put = "REPLACE INTO checkpt (key,value) "
                              "  values (?1, ?2)";

struct content_sqlite {
    flux_msg_handler_t **handlers;
    char *dbfile;
    sqlite3 *db;
    sqlite3_stmt *load_stmt;
    sqlite3_stmt *store_stmt;
    sqlite3_stmt *checkpt_get_stmt;
    sqlite3_stmt *checkpt_put_stmt;
    flux_t *h;
    const char *hashfun;
    size_t lzo_bufsize;
    void *lzo_buf;
};

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
        case SQLITE_ABORT:      /* statment is not authorized */
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
                                const char *blobref,
                                const void **datap,
                                int *sizep)
{
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_len;
    const void *data = NULL;
    int size = 0;
    int uncompressed_size;

    if ((hash_len = blobref_strtohash (blobref, hash, sizeof (hash))) < 0) {
        errno = ENOENT;
        flux_log_error (ctx->h, "load: unexpected foreign blobref");
        return -1;
    }
    if (sqlite3_bind_text (ctx->load_stmt,
                           1,
                           (char *)hash,
                           hash_len,
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
 * Blobref resulting from hash over 'data' is stored to 'blobref'.
 * Returns 0 on success, -1 on error with errno set.
 */
static int content_sqlite_store (struct content_sqlite *ctx,
                                 const void *data,
                                 int size,
                                 char *blobref,
                                 int blobrefsz)
{
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_len;
    int uncompressed_size = -1;

    if (blobref_hash (ctx->hashfun,
                      (uint8_t *)data,
                      size,
                      blobref,
                      blobrefsz) < 0)
        return -1;
    if ((hash_len = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        return -1;
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
                           (char *)hash,
                           hash_len,
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
    if (sqlite3_step (ctx->store_stmt) != SQLITE_DONE
                    && sqlite3_errcode (ctx->db) != SQLITE_CONSTRAINT) {
        log_sqlite_error (ctx, "store: executing stmt");
        set_errno_from_sqlite_error (ctx);
        goto error;
    }
    sqlite3_reset (ctx->store_stmt);
    return 0;
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
    const char *blobref;
    int blobref_size;
    const void *data;
    int size;

    if (flux_request_decode_raw (msg,
                                 NULL,
                                 (const void **)&blobref,
                                 &blobref_size) < 0) {
        flux_log_error (h, "load: request decode failed");
        goto error;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        flux_log_error (h, "load: malformed blobref");
        goto error;
    }
    if (content_sqlite_load (ctx, blobref, &data, &size) < 0)
        goto error;
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
    int size;
    char blobref[BLOBREF_MAX_STRING_SIZE];

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0) {
        flux_log_error (h, "store: request decode failed");
        goto error;
    }
    if (content_sqlite_store (ctx, data, size, blobref, sizeof (blobref)) < 0)
        goto error;
    if (flux_respond_raw (h, msg, blobref, strlen (blobref) + 1) < 0)
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
        if (!(o = json_pack ("{s:i s:s s:f}",
                             "version", 0,
                             "rootref", s,
                             "timestamp", 0.))) {
            errstr = "failed to encode blobref in json envelope";
            errno = EINVAL;
            goto error;
        }
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:O}",
                           "value",
                           o) < 0)
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
                             "key",
                             &key,
                             "value",
                             &o) < 0)
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

/* Open the database file ctx->dbfile and set up the database.
 */
static int content_sqlite_opendb (struct content_sqlite *ctx)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    if (sqlite3_open_v2 (ctx->dbfile, &ctx->db, flags, NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "opening %s", ctx->dbfile);
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA journal_mode=OFF",
                      NULL,
                      NULL,
                      NULL) != SQLITE_OK) {
        log_sqlite_error (ctx, "setting sqlite 'journal_mode' pragma");
        goto error;
    }
    if (sqlite3_exec (ctx->db,
                      "PRAGMA synchronous=OFF",
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
        free (ctx);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",    load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",   store_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs-checkpoint.get", checkpoint_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs-checkpoint.put", checkpoint_put_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static struct content_sqlite *content_sqlite_create (flux_t *h)
{
    struct content_sqlite *ctx;
    const char *dbdir;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->lzo_buf = calloc (1, lzo_buf_chunksize)))
        goto error;
    ctx->lzo_bufsize = lzo_buf_chunksize;
    ctx->h = h;

    /* Some tunables:
     * - the hash function, e.g. sha1, sha256
     * - the maximum blob size
     * - path to sqlite file
     */
    if (!(ctx->hashfun = flux_attr_get (h, "content.hash"))) {
        flux_log_error (h, "content.hash");
        goto error;
    }

    /* Prefer 'statedir' as the location for content.sqlite file, if set.
     * Otherwise use 'rundir'.
     */
    if (!(dbdir = flux_attr_get (h, "statedir")))
        dbdir = flux_attr_get (h, "rundir");
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

int mod_main (flux_t *h, int argc, char **argv)
{
    struct content_sqlite *ctx;

    if (!(ctx = content_sqlite_create (h))) {
        flux_log_error (h, "content_sqlite_create failed");
        return -1;
    }
    if (content_sqlite_opendb (ctx) < 0)
        goto done;
    if (content_register_backing_store (h, "content-sqlite") < 0)
        goto done;
    if (content_register_service (h, "content-backing") < 0)
        goto done;
    if (content_register_service (h, "kvs-checkpoint") < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    if (content_unregister_backing_store (h) < 0)
        goto done;
done:
    content_sqlite_closedb (ctx);
    content_sqlite_destroy (ctx);
    return 0;
}

MOD_NAME ("content-sqlite");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
