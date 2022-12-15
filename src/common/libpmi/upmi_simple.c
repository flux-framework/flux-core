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

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "simple_client.h"
#include "pmi_strerror.h"
#include "pmi.h"
#include "upmi.h"
#include "upmi_plugin.h"

struct upmi_simple {
    struct upmi *upmi;
    struct pmi_simple_client *client;
    char kvsname[1024];
};

static const char *op_getname (void)
{
    return "simple";
}

static void *op_create (struct upmi *upmi,
                        const char *path,
                        flux_error_t *error)
{
    struct upmi_simple *ctx;
    const char *vars[] = { "PMI_FD", "PMI_RANK", "PMI_SIZE" }; // required

    for (int i = 0; i < ARRAY_SIZE (vars); i++) {
        if (!getenv (vars[i])) {
            errprintf (error, "%s is missing from the environment", vars[i]);
            return NULL;
        }
    }
    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    ctx->upmi = upmi;
    ctx->client = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                               getenv ("PMI_RANK"),
                                               getenv ("PMI_SIZE"),
                                               getenv ("PMI_SPAWNED"));
    if (!ctx->client) {
        errprintf (error, "%s", strerror (errno));
        free (ctx);
        return NULL;
    }
    return ctx;
}

static void op_destroy (void *data)
{
    struct upmi_simple *ctx = data;

    if (ctx) {
        int saved_errno = errno;
        pmi_simple_client_destroy (ctx->client);
        free (ctx);
        errno = saved_errno;
    }
}

static int op_initialize (void *data,
                          struct upmi_info *info,
                          flux_error_t *error)
{
    struct upmi_simple *ctx = data;
    int result;

    result = pmi_simple_client_init (ctx->client);
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
        return -1;
    }
    result = pmi_simple_client_kvs_get_my_name (ctx->client,
                                                ctx->kvsname,
                                                sizeof (ctx->kvsname));
    if (result != PMI_SUCCESS) {
        errprintf (error, "fetch KVS name: %s", pmi_strerror (result));
        return -1;
    }
    info->rank = ctx->client->rank;
    info->size = ctx->client->size;
    info->name = ctx->kvsname;
    return 0;
}

static int op_finalize (void *data, flux_error_t *error)
{
    struct upmi_simple *ctx = data;
    int result;

    result = pmi_simple_client_finalize (ctx->client);
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
        return -1;
    }
    return 0;
}

static int op_put (void *data,
                   const char *key,
                   const char *val,
                   flux_error_t *error)
{
    struct upmi_simple *ctx = data;
    int result;

    result = pmi_simple_client_kvs_put (ctx->client, ctx->kvsname, key, val);
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
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
    char buf[1024];
    char *cpy;

    struct upmi_simple *ctx = data;
    int result;

    result = pmi_simple_client_kvs_get (ctx->client,
                                        ctx->kvsname,
                                        key,
                                        buf,
                                        sizeof (buf));
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
        return -1;
    }
    if (!(cpy = strdup (buf))) {
        errprintf (error, "out of memory");
        return -1;
    }
    *value = cpy;
    return 0;
}

static int op_barrier (void *data, flux_error_t *error)
{
    struct upmi_simple *ctx = data;
    int result;

    result = pmi_simple_client_barrier (ctx->client);
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
        return -1;
    }
    return 0;
}

const struct upmi_plugin upmi_simple = {
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
