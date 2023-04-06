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
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <libs3.h>
#include <stdlib.h>

#include "src/common/libutil/errno_safe.h"

#include "s3.h"

#if HAVE_S3_AUTH_REGION
#define S3_create_bucket(proto, access, secret, host, buck, acl, loc, req, cb, data) \
    S3_create_bucket(proto, access, secret, NULL, host, buck, acl, loc, req, cb, data)
#endif

#if HAVE_S3_TIMEOUT_ARG
#define S3_create_bucket(proto, access, secret, host, buck, acl, loc, req, cb, data) \
    S3_create_bucket(proto, access, secret, NULL, host, buck, NULL, acl, loc, req, 0, cb, data)
#define S3_put_object(ctx, key, size, prop, req, cb, data) \
    S3_put_object(ctx, key, size, prop, req, 0, cb, data)
#define S3_get_object(ctx, key, cond, start, cnt, req, cb, data) \
    S3_get_object(ctx, key, cond, start, cnt, req, 0, cb, data)
#endif

static S3Protocol protocol = S3ProtocolHTTP;
static S3UriStyle uri_style = S3UriStylePath;

/* Data needed by the get object callback function
 */
struct cb_data {
    size_t size;
    void *data;
    int count;
    S3Status status;
};

static S3Status response_props_cb (const S3ResponseProperties *properties,
                                   void *data)
{
    return S3StatusOK;
}

static void response_complete_cb (S3Status status,
                                  const S3ErrorDetails *error,
                                  void *data)
{
    struct cb_data *ctx = data;
    ctx->status = status;
}

/* Writes a chunk of 'data' to s3, returning
 * the size of the data written.
 */
static int put_object_cb (int buff_size, char *buff, void *data)
{
    struct cb_data *ctx = data;
    int size = (buff_size < ctx->size - ctx->count ? buff_size
                                                   : ctx->size - ctx->count);

    memcpy (buff, ctx->data + ctx->count, size);
    ctx->count += size;

    return size;
}

/* Gets the object from s3 storing it in in 'data->datap' as well
 * as updating 'data->sizep' to hold the size of the data read.
 */
static S3Status get_object_cb (int buff_size, const char *buff, void *data)
{
    void *tmp = NULL;
    struct cb_data *ctx = data;

    if (! (tmp = realloc (ctx->data, buff_size + ctx->size)))
        return S3StatusOutOfMemory;
    ctx->data = tmp;

    memcpy (ctx->data + ctx->size, buff, buff_size);
    ctx->size += buff_size;

    return S3StatusOK;
}

int s3_init (struct s3_config *cfg, const char **errstr)
{
    S3Status status = S3_initialize ("s3", S3_INIT_ALL, cfg->hostname);

    if (cfg->is_virtual_host)
        uri_style = S3UriStyleVirtualHost;
    if (cfg->is_secure)
        protocol = S3ProtocolHTTPS;

    if (status != S3StatusOK) {
        errno = ECONNREFUSED;
        if (errstr)
            *errstr = S3_get_status_name (status);

        return -1;
    }

    return 0;
}

void s3_cleanup (void)
{
    S3_deinitialize ();
}

int s3_bucket_create (struct s3_config *cfg, const char **errstr)
{
    int retries = cfg->retries;
    S3Status status = S3_validate_bucket_name (cfg->bucket, uri_style);

    if (status != S3StatusOK) {
        errno = ECONNREFUSED;
        if (errstr)
            *errstr = S3_get_status_name (status);

        return -1;
    }

    S3ResponseHandler resp_hndl = {
        .propertiesCallback = &response_props_cb,
        .completeCallback = &response_complete_cb
    };

    struct cb_data ctx = {
        .size = 0,
        .data = NULL,
        .count = 0,
        .status = status,
    };

    do {
        S3_create_bucket (protocol,
                          cfg->access_key,
                          cfg->secret_key,
                          NULL, // hostName (NULL=use hostName passed
                                //  to S3_initialize())
                          cfg->bucket,
                          S3CannedAclPrivate,
                          NULL, // locationConstraint
                          NULL, // requestContext (NULL for synchronous
                                //  operation)
                          &resp_hndl,
                          &ctx);
        retries--;
    } while (S3_status_is_retryable (ctx.status) && retries > 0);

    if (ctx.status != S3StatusOK
        && ctx.status != S3StatusErrorBucketAlreadyOwnedByYou) {
        errno = EREMOTEIO;
        if (errstr)
            *errstr = S3_get_status_name (status);

        return -1;
    }

    return 0;
}

int s3_put (struct s3_config *cfg,
            const char *key,
            const void *data,
            size_t size,
            const char **errstr)
{
    int retries = cfg->retries;
    S3Status status = S3StatusOK;

    S3ResponseHandler resp_hndl = {
        .propertiesCallback = &response_props_cb,
        .completeCallback = &response_complete_cb
    };

    S3BucketContext bucket_ctx = {
        .hostName = NULL,
        .bucketName = cfg->bucket,
        .protocol = protocol,
        .uriStyle = uri_style,
        .accessKeyId = cfg->access_key,
        .secretAccessKey = cfg->secret_key
    };

    S3PutObjectHandler put_obj_hndl ={
        .responseHandler = resp_hndl,
        .putObjectDataCallback = &put_object_cb
    };

    if (strlen (key) == 0
        || strchr (key, '/')
        || !strcmp (key, "..")
        || !strcmp (key, ".")) {
        errno = EINVAL;
        if (errstr)
            *errstr = "invalid key";

        return -1;
    }

    struct cb_data ctx = {
        .size = size,
        .data = (void *) data,
        .count = 0,
        .status = status
    };

    do {
        S3_put_object (&bucket_ctx,
                       key,
                       size,
                       NULL, // putPorperties (NULL for none)
                       NULL, // requestContext (NULL for synchronous operation)
                       &put_obj_hndl,
                       &ctx);
        retries--;
    } while (S3_status_is_retryable (ctx.status) && retries > 0);

    if (ctx.status != S3StatusOK) {
        errno = EREMOTEIO;
        if (errstr)
            *errstr = S3_get_status_name (ctx.status);

        return -1;
    }

    return 0;
}

int s3_get (struct s3_config *cfg,
            const char *key,
            void **datap,
            size_t *sizep,
            const char **errstr)
{
    int retries = cfg->retries;
    size_t size = 0;
    S3Status status = S3StatusOK;

    S3ResponseHandler resp_hndl = {
        .propertiesCallback = &response_props_cb,
        .completeCallback = &response_complete_cb
    };

    S3BucketContext bucket_ctx = {
        .hostName = NULL,
        .bucketName = cfg->bucket,
        .protocol = protocol,
        .uriStyle = uri_style,
        .accessKeyId = cfg->access_key,
        .secretAccessKey = cfg->secret_key
    };


    S3GetObjectHandler get_obj_hndl = {
        .responseHandler = resp_hndl,
        .getObjectDataCallback =  &get_object_cb
    };

    struct cb_data ctx = {
        .size = size,
        .data = NULL,
        .count = 0,
        .status = status
    };

    if (strlen (key) == 0
        || strchr (key, '/')
        || !strcmp (key, "..")
        || !strcmp (key, ".")) {
        errno = EINVAL;
        if (errstr)
            *errstr = "invalid key";

        return -1;
    }

    do {
        S3_get_object (&bucket_ctx,
                       key,
                       NULL, // getConditions (NULL for none)
                       0,    // startByte
                       0,    // byteCount (0 indicates the entire object
                             //  should be read)
                       NULL, // requestContext (NULL for synchronous operation)
                       &get_obj_hndl, &ctx);
        retries--;
    } while (S3_status_is_retryable (ctx.status) && retries > 0);

    if (ctx.status != S3StatusOK) {
        free (ctx.data);
        if (ctx.status == S3StatusErrorNoSuchKey)
            errno = ENOENT;
        else
            errno = EREMOTEIO;

        if (errstr)
            *errstr = S3_get_status_name (ctx.status);

        return -1;
    }

    *datap = ctx.data;
    *sizep = ctx.size;

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
