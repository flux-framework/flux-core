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

/* content-sophia.c - content addressable storage with sophia back end */

/* Sophia put/commit is nearly as fast as hash insert.
 * Sophia get is O(20X) slower.
 * Sophia get with lz4 compression is O(4X) slower.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/sha1.h"
#include "src/common/libutil/shastring.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libsophia/sophia.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

typedef struct {
    char *dir;
    void *env;
    void *db;
    flux_t h;
    bool broker_shutdown;
    uint32_t blob_size_limit;
} ctx_t;

static void log_sophia_error (ctx_t *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    char *s = xvasprintf (fmt, ap);
    va_end (ap);

    int error_size;
    char *error = NULL;
    if (ctx->env)
        error = sp_getstring (ctx->env, "sophia.error", &error_size);
    (void)flux_log (ctx->h, LOG_ERR, "%s: %s", s, error ? error : "failure");
    if (error)
        free (error);
    free (s);
}

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    if (ctx) {
        if (ctx->dir)
            free (ctx->dir); /* remove? */
        if (ctx->db)
            sp_destroy (ctx->db);
        if (ctx->env)
            sp_destroy (ctx->env);
        free (ctx);
    }
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "flux::content-sophia");
    const char *dir;
    const char *hashfun;
    const char *tmp;
    bool cleanup = false;
    int saved_errno;
    int flags;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (!(hashfun = flux_attr_get (h, "content-hash", &flags))) {
            saved_errno = errno;
            flux_log_error (h, "content-hash");
            goto error;
        }
        if (strcmp (hashfun, "sha1") != 0) {
            saved_errno = errno = EINVAL;
            flux_log_error (h, "content-hash %s", hashfun);
            goto error;
        }
        if (!(tmp = flux_attr_get (h, "content-blob-size-limit", NULL))) {
            saved_errno = errno;
            flux_log_error (h, "content-blob-size-limit");
            goto error;
        }
        ctx->blob_size_limit = strtoul (tmp, NULL, 10);
        if (!(dir = flux_attr_get (h, "persist-directory", NULL))) {
            if (!(dir = flux_attr_get (h, "scratch-directory", NULL))) {
                saved_errno = errno;
                flux_log_error (h, "scratch-directory");
                goto error;
            }
            cleanup = true;
        }
        ctx->dir = xasprintf ("%s/content", dir);
        if (!(ctx->env = sp_env ())
                || sp_setstring (ctx->env, "sophia.path", ctx->dir, 0) < 0
                || sp_setstring (ctx->env, "db", "content", 0) < 0
                || sp_setstring (ctx->env, "db.content.index.key",
                                                    "string", 0) < 0
                || sp_open (ctx->env) < 0
                || !(ctx->db = sp_getobject (ctx->env, "db.content"))) {
            saved_errno = EINVAL;
            log_sophia_error (ctx, "initialization");
            goto error;
        }
        if (cleanup)
            cleanup_push_string (cleanup_directory_recursive, ctx->dir);
        flux_aux_set (h, "flux::content-sophia", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    errno = saved_errno;
    return NULL;
}

void load_cb (flux_t h, flux_msg_handler_t *w,
              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    char *blobref = "-";
    int blobref_size;
    uint8_t hash[SHA1_DIGEST_SIZE];
    void *data = NULL;
    int size = 0;
    int rc = -1;
    void *o, *result = NULL;

    if (flux_request_decode_raw (msg, NULL, &blobref, &blobref_size) < 0) {
        flux_log_error (h, "load: request decode failed");
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        flux_log_error (h, "load: malformed blobref");
        goto done;
    }
    if (sha1_strtohash (blobref, hash) < 0) {
        errno = ENOENT;
        flux_log_error (h, "load: unexpected foreign blobref");
        goto done;
    }
    o = sp_object (ctx->db);
    if (sp_setstring (o, "key", hash, SHA1_DIGEST_SIZE) < 0) {
        log_sophia_error (ctx, "load: sp_setstring key");
        errno = EINVAL;
        goto done;
    }
    if (!(result = sp_get (ctx->db, o))) {
        log_sophia_error (ctx, "load: sp_get");
        errno = ENOENT; /* XXX */
        goto done;
    }
    data = sp_getstring (result, "value", &size);
    rc = 0;
done:
    if (flux_respond_raw (h, msg, rc < 0 ? errno : 0,
                                  rc < 0 ? NULL : data, size) < 0)
        flux_log_error (h, "flux_respond");
    if (result)
        sp_destroy (result);
}

void store_cb (flux_t h, flux_msg_handler_t *w,
               const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    void *data;
    int size;
    SHA1_CTX sha1_ctx;
    uint8_t hash[SHA1_DIGEST_SIZE];
    char blobref[SHA1_STRING_SIZE] = "-";
    void *o;
    int rc = -1;

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0) {
        flux_log_error (h, "store: request decode failed");
        goto done;
    }
    if (size > ctx->blob_size_limit) {
        errno = EFBIG;
        goto done;
    }
    SHA1_Init (&sha1_ctx);
    SHA1_Update (&sha1_ctx, (uint8_t *)data, size);
    SHA1_Final (&sha1_ctx, hash);
    sha1_hashtostr (hash, blobref);

    /* Checking if object is already stored is very costly, so don't.
     */
    o = sp_object (ctx->db);
    if (sp_setstring (o, "key",  hash, SHA1_DIGEST_SIZE) < 0) {
        log_sophia_error (ctx, "store: sp_setstring key");
        errno = EINVAL;
        goto done;
    }
    if (sp_setstring (o, "value", data, size) < 0) {
        log_sophia_error (ctx, "store: sp_setstring value");
        errno = EINVAL;
        goto done;
    }
    if (sp_set (ctx->db, o) < 0) {
        log_sophia_error (ctx, "store: sp_set");
        errno = EINVAL; /* XXX */
        goto done;
    }
    rc = 0;
done:
    if (flux_respond_raw (h, msg, rc < 0 ? errno : 0,
                                  blobref, SHA1_STRING_SIZE) < 0)
        flux_log_error (h, "flux_respond");
}

static void stats_get_cb (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    flux_msgcounters_t mcs;
    JSON out = Jnew ();

    /* replicate stats returned by all modules by default */
    flux_get_msgcounters (h, &mcs);
    Jadd_int (out, "#request (tx)", mcs.request_tx);
    Jadd_int (out, "#request (rx)", mcs.request_rx);
    Jadd_int (out, "#response (tx)", mcs.response_tx);
    Jadd_int (out, "#response (rx)", mcs.response_rx);
    Jadd_int (out, "#event (tx)", mcs.event_tx);
    Jadd_int (out, "#event (rx)", mcs.event_rx);
    Jadd_int (out, "#keepalive (tx)", mcs.keepalive_tx);
    Jadd_int (out, "#keepalive (rx)", mcs.keepalive_rx);

    /* add sophia system objects and configuration values */
    void *cursor = sp_getobject (ctx->env, NULL);
    void *ptr = NULL;
    while ((ptr = sp_get (cursor, ptr))) {
        char *key = sp_getstring (ptr, "key", NULL);
        char *value = sp_getstring (ptr, "value", NULL);
        Jadd_str (out, key, value ? value : "");
    }
    sp_destroy (cursor);

    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        FLUX_LOG_ERROR (h);
    Jput (out);
}

int register_backing_store (flux_t h, bool value, const char *name)
{
    flux_rpc_t *rpc;
    JSON in = Jnew ();
    int saved_errno = 0;
    int rc = -1;

    Jadd_bool (in, "backing", value);
    Jadd_str (in, "name", name);
    if (!(rpc = flux_rpc (h, "content.backing", Jtostr (in),
                            FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    saved_errno = errno;
    Jput (in);
    flux_rpc_destroy (rpc);
    errno = saved_errno;
    return rc;
}

/* Intercept broker shutdown event.  If broker is shutting down,
 * avoid transferring data back to the content cache at unload time.
 */
void broker_shutdown_cb (flux_t h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    ctx->broker_shutdown = true;
    flux_log (h, LOG_DEBUG, "broker shutdown in progress");
}

/* Manage shutdown of this module, e.g. at module unload time.
 * Tell content cache to disable persistence,
 * then write everything back to it before exiting.
 */
void shutdown_cb (flux_t h, flux_msg_handler_t *w,
                  const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    void *cursor = NULL;
    void *o;
    flux_rpc_t *rpc;

    if (register_backing_store (h, false, "content-sophia") < 0) {
        flux_log_error (h, "dump: unregistering backing store");
        goto done;
    }
    if (ctx->broker_shutdown) {
        flux_log (h, LOG_INFO, "dump: skipping");
        goto done;
    }
    if (!(cursor = sp_cursor (ctx->env))) {
        log_sophia_error (ctx, "dump: sp_cursor");
        goto done;
    }
    if (!(o = sp_object (ctx->db))) {
        log_sophia_error (ctx, "dump: sp_object");
        goto done;
    }
    sp_setstring (o, "order", ">=", 0);
    while ((o = sp_get (cursor, o))) {
        void *data = NULL;
        int size = 0;
        char *blobref = NULL;
        int blobref_size;
        data = sp_getstring (o, "value", &size);
        if (!(rpc = flux_rpc_raw (h, "content.store", data, size,
                                                    FLUX_NODEID_ANY, 0))) {
            flux_log_error (ctx->h, "dump: store");
            continue;
        }
        if (flux_rpc_get_raw (rpc, NULL, &blobref, &blobref_size) < 0) {
            flux_log_error (h, "dump: store");
            flux_rpc_destroy (rpc);
            continue;
        }
        if (!blobref || blobref[blobref_size - 1] != '\0') {
            flux_log (h, LOG_ERR, "dump: store returned malformed blobref");
            flux_rpc_destroy (rpc);
            continue;
        }
        flux_rpc_destroy (rpc);
    }
done:
    if (cursor)
        sp_destroy (cursor);
    flux_reactor_stop (flux_get_reactor (h));
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",      load_cb },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",     store_cb },
    { FLUX_MSGTYPE_REQUEST, "content-backing.stats.get", stats_get_cb },
    { FLUX_MSGTYPE_REQUEST, "content-sophia.shutdown",   shutdown_cb },
    { FLUX_MSGTYPE_EVENT,   "shutdown",                  broker_shutdown_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t h, int argc, char **argv)
{
    ctx_t *ctx = getctx (h);
    if (!ctx)
        return -1;
    if (flux_event_subscribe (h, "shutdown") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        return -1;
    }
    if (register_backing_store (h, true, "content-sophia") < 0) {
        flux_log_error (h, "registering backing store");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
done:
    flux_msg_handler_delvec (htab);
    return 0;
}

MOD_NAME ("content-sophia");
MOD_SERVICE ("content-backing");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
