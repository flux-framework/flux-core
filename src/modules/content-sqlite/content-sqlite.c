/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* content-sqlite.c - content addressable storage with sqlite back end */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sqlite3.h>
#include <czmq.h>
#include <lz4.h>
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

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
const char *sql_dump = "SELECT object,size FROM objects";

typedef struct {
    char *dbdir;
    char *dbfile;
    sqlite3 *db;
    sqlite3_stmt *load_stmt;
    sqlite3_stmt *store_stmt;
    sqlite3_stmt *dump_stmt;
    flux_t *h;
    bool broker_shutdown;
    const char *hashfun;
    uint32_t blob_size_limit;
    size_t lzo_bufsize;
    void *lzo_buf;
} sqlite_ctx_t;

static void log_sqlite_error (sqlite_ctx_t *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    char *s = xvasprintf (fmt, ap);
    va_end (ap);

    const char *error = sqlite3_errmsg (ctx->db);
    int xerrcode = sqlite3_extended_errcode (ctx->db);
    (void)flux_log (ctx->h, LOG_ERR, "%s: %s(%d)",
                    s, error ? error : "failure", xerrcode);
    free (s);
}

static void set_errno_from_sqlite_error (sqlite_ctx_t *ctx)
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

static void freectx (void *arg)
{
    sqlite_ctx_t *ctx = arg;
    if (ctx) {
        if (ctx->store_stmt)
            sqlite3_finalize (ctx->store_stmt);
        if (ctx->load_stmt)
            sqlite3_finalize (ctx->load_stmt);
        if (ctx->dump_stmt)
            sqlite3_finalize (ctx->dump_stmt);
        if (ctx->dbdir)
            free (ctx->dbdir);
        if (ctx->dbfile) {
            unlink (ctx->dbfile);
            free (ctx->dbfile);
        }
        if (ctx->db)
            sqlite3_close (ctx->db);
        if (ctx->lzo_buf)
            free (ctx->lzo_buf);
        free (ctx);
    }
}

static sqlite_ctx_t *getctx (flux_t *h)
{
    sqlite_ctx_t *ctx = (sqlite_ctx_t *)flux_aux_get (h, "flux::content-sqlite");
    const char *dir;
    const char *tmp;
    bool cleanup = false;
    int saved_errno;
    int flags;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->lzo_buf = xzmalloc (lzo_buf_chunksize);
        ctx->lzo_bufsize = lzo_buf_chunksize;
        ctx->h = h;
        if (!(ctx->hashfun = flux_attr_get (h, "content.hash", &flags))) {
            saved_errno = errno;
            flux_log_error (h, "content.hash");
            goto error;
        }
        if (!(tmp = flux_attr_get (h, "content.blob-size-limit", NULL))) {
            saved_errno = errno;
            flux_log_error (h, "content.blob-size-limit");
            goto error;
        }
        ctx->blob_size_limit = strtoul (tmp, NULL, 10);

        if (!(dir = flux_attr_get (h, "persist-directory", NULL))) {
            if (!(dir = flux_attr_get (h, "broker.rundir", NULL))) {
                saved_errno = errno;
                flux_log_error (h, "broker.rundir");
                goto error;
            }
            cleanup = true;
        }
        ctx->dbdir = xasprintf ("%s/content", dir);
        if (mkdir (ctx->dbdir, 0755) < 0 && errno != EEXIST) {
            saved_errno = errno;
            flux_log_error (h, "mkdir %s", ctx->dbdir);
            goto error;
        }
        if (cleanup)
            cleanup_push_string (cleanup_directory_recursive, ctx->dbdir);
        ctx->dbfile = xasprintf ("%s/sqlite", ctx->dbdir);

        if (sqlite3_open (ctx->dbfile, &ctx->db) != SQLITE_OK) {
            saved_errno = errno;
            flux_log_error (h, "sqlite3_open %s", ctx->dbfile);
            goto error;
        }
        if (sqlite3_exec (ctx->db, "PRAGMA journal_mode=OFF",
                                            NULL, NULL, NULL) != SQLITE_OK
                || sqlite3_exec (ctx->db, "PRAGMA synchronous=OFF",
                                            NULL, NULL, NULL) != SQLITE_OK
                || sqlite3_exec (ctx->db, "PRAGMA locking_mode=EXCLUSIVE",
                                            NULL, NULL, NULL) != SQLITE_OK) {
            saved_errno = EINVAL;
            log_sqlite_error (ctx, "setting sqlite pragmas");
            goto error;
        }
        if (sqlite3_exec (ctx->db, sql_create_table,
                                            NULL, NULL, NULL) != SQLITE_OK) {
            saved_errno = EINVAL;
            log_sqlite_error (ctx, "creating table");
            goto error;
        }
        if (sqlite3_prepare_v2 (ctx->db, sql_load, -1, &ctx->load_stmt,
                                            NULL) != SQLITE_OK) {
            saved_errno = EINVAL;
            log_sqlite_error (ctx, "preparing load stmt");
            goto error;
        }
        if (sqlite3_prepare_v2 (ctx->db, sql_store, -1, &ctx->store_stmt,
                                            NULL) != SQLITE_OK) {
            saved_errno = EINVAL;
            log_sqlite_error (ctx, "preparing store stmt");
            goto error;
        }
        if (sqlite3_prepare_v2 (ctx->db, sql_dump, -1, &ctx->dump_stmt,
                                            NULL) != SQLITE_OK) {
            saved_errno = EINVAL;
            log_sqlite_error (ctx, "preparing dump stmt");
            goto error;
        }
        flux_aux_set (h, "flux::content-sqlite", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    errno = saved_errno;
    return NULL;
}

int grow_lzo_buf (sqlite_ctx_t *ctx, size_t size)
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

void load_cb (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    sqlite_ctx_t *ctx = arg;
    const char *blobref = "-";
    int blobref_size;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_len;
    const void *data = NULL;
    int size = 0;
    int uncompressed_size;
    int rc = -1;
    int old_state;
    //delay cancellation to ensure lock-correctness in sqlite
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

    if (flux_request_decode_raw (msg, NULL, (const void **)&blobref,
                                 &blobref_size) < 0) {
        flux_log_error (h, "load: request decode failed");
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        flux_log_error (h, "load: malformed blobref");
        goto done;
    }
    if ((hash_len = blobref_strtohash (blobref, hash, sizeof (hash))) < 0) {
        errno = ENOENT;
        flux_log_error (h, "load: unexpected foreign blobref");
        goto done;
    }
    if (sqlite3_bind_text (ctx->load_stmt, 1, (char *)hash, hash_len,
                                              SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "load: binding key");
        set_errno_from_sqlite_error (ctx);
        goto done;
    }
    if (sqlite3_step (ctx->load_stmt) != SQLITE_ROW) {
        //log_sqlite_error (ctx, "load: executing stmt");
        errno = ENOENT;
        goto done;
    }
    size = sqlite3_column_bytes (ctx->load_stmt, 0);
    if (sqlite3_column_type (ctx->load_stmt, 0) != SQLITE_BLOB && size > 0) {
        flux_log (h, LOG_ERR, "load: selected value is not a blob");
        errno = EINVAL;
        goto done;
    }
    data = sqlite3_column_blob (ctx->load_stmt, 0);
    if (sqlite3_column_type (ctx->load_stmt, 1) != SQLITE_INTEGER) {
        flux_log (h, LOG_ERR, "load: selected value is not an integer");
        errno = EINVAL;
        goto done;
    }
    uncompressed_size = sqlite3_column_int (ctx->load_stmt, 1);
    if (uncompressed_size != -1) {
        if (ctx->lzo_bufsize < uncompressed_size
                                && grow_lzo_buf (ctx, uncompressed_size) < 0)
            goto done;
        int r = LZ4_decompress_safe (data, ctx->lzo_buf, size, uncompressed_size);
        if (r < 0) {
            errno = EINVAL;
            goto done;
        }
        if (r != uncompressed_size) {
            flux_log (h, LOG_ERR, "load: blob size mismatch");
            errno = EINVAL;
            goto done;
        }
        data = ctx->lzo_buf;
        size = uncompressed_size;
    }
    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "load: flux_respond_error");
    }
    else {
        if (flux_respond_raw (h, msg, data, size) < 0)
            flux_log_error (h, "load: flux_respond_raw");
    }
    (void )sqlite3_reset (ctx->load_stmt);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
}

void store_cb (flux_t *h, flux_msg_handler_t *mh,
               const flux_msg_t *msg, void *arg)
{
    sqlite_ctx_t *ctx = arg;
    const void *data;
    int size, hash_len;
    uint8_t hash[BLOBREF_MAX_DIGEST_SIZE];
    char blobref[BLOBREF_MAX_STRING_SIZE] = "-";
    int uncompressed_size = -1;
    int rc = -1;
    int old_state;
    //delay cancellation to ensure lock-correctness in sqlite
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0) {
        flux_log_error (h, "store: request decode failed");
        goto done;
    }
    if (size > ctx->blob_size_limit) {
        errno = EFBIG;
        goto done;
    }
    if (blobref_hash (ctx->hashfun, (uint8_t *)data, size, blobref,
                      sizeof (blobref)) < 0)
        goto done;
    if ((hash_len = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        goto done;
    if (size >= compression_threshold) {
        int r;
        int out_len = LZ4_compressBound(size);
        if (ctx->lzo_bufsize < out_len && grow_lzo_buf (ctx, out_len) < 0)
            goto done;
        r = LZ4_compress_default (data, ctx->lzo_buf, size, out_len);
        if (r == 0) {
            errno = EINVAL;
            goto done;
        }
        uncompressed_size = size;
        size = r;
        data = ctx->lzo_buf;
    }
    if (sqlite3_bind_text (ctx->store_stmt, 1, (char *)hash, hash_len,
                           SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding key");
        set_errno_from_sqlite_error (ctx);
        goto done;
    }
    if (sqlite3_bind_int (ctx->store_stmt, 2, uncompressed_size) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding size");
        set_errno_from_sqlite_error (ctx);
        goto done;
    }
    if (sqlite3_bind_blob (ctx->store_stmt, 3,
                           data, size, SQLITE_STATIC) != SQLITE_OK) {
        log_sqlite_error (ctx, "store: binding data");
        set_errno_from_sqlite_error (ctx);
        goto done;
    }
    if (sqlite3_step (ctx->store_stmt) != SQLITE_DONE
                    && sqlite3_errcode (ctx->db) != SQLITE_CONSTRAINT) {
        log_sqlite_error (ctx, "store: executing stmt");
        set_errno_from_sqlite_error (ctx);
        goto done;
    }
    rc = 0;
done:
    if (rc < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "store: flux_respond_error");
    }
    else {
        if (flux_respond_raw (h, msg, blobref, strlen (blobref) + 1) < 0)
            flux_log_error (h, "store: flux_respond_raw");
    }
    (void) sqlite3_reset (ctx->store_stmt);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
}

int register_backing_store (flux_t *h, bool value, const char *name)
{
    flux_future_t *f;
    int saved_errno = 0;
    int rc = -1;

    if (!(f = flux_rpc_pack (h, "content.backing", FLUX_NODEID_ANY, 0,
                             "{ s:b s:s }",
                             "backing", value,
                             "name", name)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

int register_content_backing_service (flux_t *h)
{
    int rc, saved_errno;
    flux_future_t *f;
    if (!(f = flux_service_register (h, "content-backing")))
        return -1;
    rc = flux_future_get (f, NULL);
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

/* Intercept broker shutdown event.  If broker is shutting down,
 * avoid transferring data back to the content cache at unload time.
 */
void broker_shutdown_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    sqlite_ctx_t *ctx = arg;
    ctx->broker_shutdown = true;
    flux_log (h, LOG_DEBUG, "broker shutdown in progress");
}

/* Manage shutdown of this module.
 * Tell content cache to disable backing store,
 * then write everything back to it before exiting.
 */
void shutdown_cb (flux_t *h, flux_msg_handler_t *mh,
                  const flux_msg_t *msg, void *arg)
{
    sqlite_ctx_t *ctx = arg;
    flux_future_t *f;
    int count = 0;
    int old_state;

    flux_log (h, LOG_DEBUG, "shutdown: begin");
    if (register_backing_store (h, false, "content-sqlite") < 0) {
        flux_log_error (h, "shutdown: unregistering backing store");
        goto done;
    }
    if (ctx->broker_shutdown) {
        flux_log (h, LOG_DEBUG, "shutdown: instance is terminating, don't reload to cache");
        goto done;
    }
    //delay cancellation to ensure lock-correctness in sqlite
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
    while (sqlite3_step (ctx->dump_stmt) == SQLITE_ROW) {
        const char *blobref;
        int blobref_size;
        const void *data = NULL;
        int uncompressed_size;
        int size = sqlite3_column_bytes (ctx->dump_stmt, 0);
        if (sqlite3_column_type (ctx->dump_stmt, 0) != SQLITE_BLOB
                                                            && size > 0) {
            flux_log (h, LOG_ERR, "shutdown: encountered non-blob value");
            continue;
        }
        data = sqlite3_column_blob (ctx->dump_stmt, 0);
        if (sqlite3_column_type (ctx->dump_stmt, 1) != SQLITE_INTEGER) {
            flux_log (h, LOG_ERR, "shutdown: selected value is not an integer");
            errno = EINVAL;
            goto done;
        }
        uncompressed_size = sqlite3_column_int (ctx->dump_stmt, 1);
        if (uncompressed_size != -1) {
            if (ctx->lzo_bufsize < uncompressed_size
                            && grow_lzo_buf (ctx, uncompressed_size) < 0)
                goto done;
            int r = LZ4_decompress_safe (data, ctx->lzo_buf, size, uncompressed_size);
            if (r < 0) {
                errno = EINVAL;
                goto done;
            }
            if (r != uncompressed_size) {
                flux_log (h, LOG_ERR, "shutdown: blob size mismatch");
                errno = EINVAL;
                goto done;
            }
            data = ctx->lzo_buf;
            size = uncompressed_size;
        }
        if (!(f = flux_rpc_raw (h, "content.store", data, size,
                                                        FLUX_NODEID_ANY, 0))) {
            flux_log_error (h, "shutdown: store");
            continue;
        }
        if (flux_rpc_get_raw (f, (const void **)&blobref, &blobref_size) < 0) {
            flux_log_error (h, "shutdown: store");
            flux_future_destroy (f);
            continue;
        }
        if (!blobref || blobref[blobref_size - 1] != '\0') {
            flux_log (h, LOG_ERR, "shutdown: store returned malformed blobref");
            flux_future_destroy (f);
            continue;
        }
        flux_future_destroy (f);
        count++;
    }
    (void )sqlite3_reset (ctx->dump_stmt);
    flux_log (h, LOG_DEBUG, "shutdown: %d entries returned to cache", count);
done:
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    flux_reactor_stop (flux_get_reactor (h));
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",    load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",   store_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-sqlite.shutdown", shutdown_cb, 0, },
    { FLUX_MSGTYPE_EVENT,   "shutdown",                broker_shutdown_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_msg_handler_t **handlers = NULL;
    sqlite_ctx_t *ctx = getctx (h);
    if (!ctx)
        goto done;
    if (flux_event_subscribe (h, "shutdown") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if (register_backing_store (h, true, "content-sqlite") < 0) {
        flux_log_error (h, "registering backing store");
        goto done;
    }
    if (register_content_backing_service (h) < 0) {
        flux_log_error (h, "service.add: content-backing");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
done:
    flux_msg_handler_delvec (handlers);
    return 0;
}

MOD_NAME ("content-sqlite");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
