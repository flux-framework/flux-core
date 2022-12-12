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
#include "config.h"
#endif
#include <stdlib.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "simple_client.h"
#include "pmi_strerror.h"
#include "pmi.h"
#include "upmi.h"
#include "upmi_plugin.h"

struct upmi_single {
    struct upmi *upmi;
    json_t *kvs;
};

static const char *op_getname (void)
{
    return "single";
}

static void *op_create (struct upmi *upmi,
                        const char *path,
                        flux_error_t *error)
{
    struct upmi_single *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    ctx->upmi = upmi;
    return ctx;
}

static void op_destroy (void *data)
{
    struct upmi_single *ctx = data;

    if (ctx) {
        int saved_errno = errno;
        json_decref (ctx->kvs);
        free (ctx);
        errno = saved_errno;
    }
}

static int op_initialize (void *data,
                          struct upmi_info *info,
                          flux_error_t *error)
{
    info->rank = 0;
    info->size = 1;
    info->name = "single";
    return 0;
}

static int op_finalize (void *data, flux_error_t *error)
{
    return 0;
}

static int op_put (void *data,
                   const char *key,
                   const char *val,
                   flux_error_t *error)
{
    struct upmi_single *ctx = data;
    json_t *o;

    if (!ctx->kvs) {
        if (!(ctx->kvs = json_object ())) {
            errprintf (error, "out of memory");
            return -1;
        }
    }
    if (!(o = json_string (val))
        || json_object_set_new (ctx->kvs, key, o) < 0) {
        json_decref (o);
        errprintf (error, "out of memory");
        return -1;
    }
    return 0;
}

static int op_get (void *data,
                   const char *key,
                   int rank, // ignored here
                   char **value,
                   flux_error_t *error)
{
    struct upmi_single *ctx = data;
    const char *val;
    char *cpy;

    if (!ctx->kvs || json_unpack (ctx->kvs, "{s:s}", key, &val) < 0) {
        errprintf (error, "key not found");
        return -1;
    }
    if (!(cpy = strdup (val))) {
        errprintf (error, "out of memory");
        return -1;
    }
    *value = cpy;
    return 0;
}

static int op_barrier (void *data, flux_error_t *error)
{
    return 0;
}

const struct upmi_plugin upmi_single = {
    .getname = op_getname,
    .create = op_create,
    .destroy = op_destroy,
    .initialize = op_initialize,
    .finalize = op_finalize,
    .put = op_put,
    .get = op_get,
    .barrier = op_barrier,
};

// vi:ts=4 sw=4 expandtab
