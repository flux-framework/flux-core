/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"

#include "src/common/libcontent/content-util.h"

#include "s3.h"

struct content_s3 {
    flux_msg_handler_t **handlers;
    struct s3_config *cfg;
    flux_t *h;
    const char *hashfun;
};

/* Handle a content-backing.load request from the rank 0 broker's
 * content-cache service.  The raw request payload is a blobref string,
 * including NULL terminator.  The raw response payload is the blob content.
 * These payloads are specified in RFC 10.
 */
static void load_cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    struct content_s3 *ctx = arg;
    const char *blobref;
    int blobref_size;
    void *data = NULL;
    size_t size;
    const char *errstr = NULL;

    if (flux_request_decode_raw (msg,
                                 NULL,
                                 (const void **)&blobref,
                                 &blobref_size) < 0)
        goto error;
    if (!blobref || blobref[blobref_size - 1] != '\0'
                 || blobref_validate (blobref) < 0) {
        errno = EPROTO;
        errstr = "invalid blobref";
        goto error;
    }
    if (s3_get (ctx->cfg, blobref, &data, &size, &errstr) < 0)
        goto error;
    if (flux_respond_raw (h, msg, data, size) < 0)
        flux_log_error (h, "error responding to load request");
    free (data);
    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to load request");
    free (data);
}

/* Handle a content-backing.store request from the rank 0 broker's
 * content-cache service.  The raw request payload is the blob content.
 * The raw response payload is a blobref string including NULL terminator.
 * These payloads are specified in RFC 10.
 */
void store_cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    struct content_s3 *ctx = arg;
    const void *data;
    int size;
    char blobref[BLOBREF_MAX_STRING_SIZE];
    const char *errstr = NULL;

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0)
        goto error;
    if (blobref_hash (ctx->hashfun,
                      (uint8_t *)data,
                      size,
                      blobref,
                      sizeof (blobref)) < 0)
        goto error;
    if (s3_put (ctx->cfg, blobref, data, size, &errstr) < 0)
        goto error;
    if (flux_respond_raw (h, msg, blobref, strlen (blobref) + 1) < 0)
        flux_log_error (h, "error responding to store request");
    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to store request");
}

/* Handle a kvs-checkpoint.get request from the rank 0 kvs module.
 * The KVS stores its last root reference here for restart purposes.
 *
 * N.B. filedb_get() calls read_all() which ensures that the returned buffer
 * is padded with an extra NULL not included in the returned length,
 * so it is safe to use the result as a string argument in flux_respond_pack().
 */
void checkpoint_get_cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    const char *errstr = NULL;
    struct content_s3 *ctx = arg;
    const char *key;
    void *data = NULL;
    char *dup = NULL;
    size_t size;

    if (flux_request_unpack (msg, NULL, "{s:s}", "key", &key) < 0)
        goto error;

    if (s3_get (ctx->cfg, key, &data, &size, &errstr) < 0)
        goto error;

    if (!(dup = strndup (data, size)))
        goto error;

    if (flux_respond_pack (h,
                           msg,
                           "{s:s}",
                           "value",
                           size > 0 ? dup : "", 0) < 0) {
        errno = EIO;
        flux_log_error (h, "error responding to kvs-checkpoint.get request (pack)");
    }
    free (data);
    free (dup);
    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to kvs-checkpoint.get request");
    free (data);
    free (dup);
}

/* Handle a kvs-checkpoint.put request from the rank 0 kvs module.
 * The KVS stores its last root reference here for restart purposes.
 */
void checkpoint_put_cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    struct content_s3 *ctx = arg;
    const char *key;
    const char *value;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:s}",
                             "key",
                             &key,
                             "value",
                             &value) < 0)
        goto error;
    if (s3_put (ctx->cfg, key, value, strlen (value), &errstr) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to kvs-checkpoint.put request (pack)");
    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to kvs-checkpoint.put request");
}

/* Table of message handler callbacks registered below.
 * The topic strings in the table consist of <service name>.<method>.
 */
static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",    load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",   store_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs-checkpoint.get", checkpoint_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "kvs-checkpoint.put", checkpoint_put_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Destroy module context.
 */
static void content_s3_destroy (struct content_s3 *ctx)
{
    if(ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx->cfg);
        free (ctx);
        errno = saved_errno;
    }

    s3_cleanup();
}

/* Create the s3 context, initalize the connection, and
 * create the working bucket
 */
static struct content_s3 *content_s3_create (flux_t *h)
{
    const char *errstr = NULL;
    struct content_s3 *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;

    if (!(ctx->hashfun = flux_attr_get (h, "content.hash"))) {
        flux_log_error (h, "content.hash");
        goto error;
    }

    if (!(ctx->cfg = calloc (1, sizeof (*ctx->cfg))))
        goto error;
    
    ctx->cfg->retries = 5;
    ctx->cfg->bucket = getenv("S3_BUCKET");
    ctx->cfg->access_key = getenv("S3_ACCESS_KEY_ID");
    ctx->cfg->secret_key = getenv("S3_SECRET_ACCESS_KEY");
    ctx->cfg->hostname = getenv("S3_HOSTNAME");

    if (s3_init (ctx->cfg, &errstr) < 0) {
        flux_log_error (h, "content-s3 init");
        goto error;
    }

    if (s3_bucket_create (ctx->cfg, &errstr) < 0) {
        flux_log_error (h, "content-s3 create bucket");
        goto error;
    }

    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;

    return ctx;

error:
    content_s3_destroy(ctx);
    return ctx;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct content_s3 *ctx;
    int rc = -1;

    if (!(ctx = content_s3_create (h))) {
        flux_log_error (h, "content_s3_create failed");
        return -1;
    }
    if (content_register_backing_store (h, "content-s3") < 0)
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

    rc = 0;
done:
    content_s3_destroy (ctx);
    return rc;
}

MOD_NAME ("content-s3");

/*
 * vi:ts=4 sw=4 expandtab
 */
