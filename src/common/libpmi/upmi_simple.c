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

struct plugin_ctx {
    struct pmi_simple_client *client;
    char kvsname[1024];
};

static const char *plugin_name = "simple";

static void plugin_ctx_destroy (struct plugin_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        pmi_simple_client_destroy (ctx->client);
        free (ctx);
        errno = saved_errno;
    }
}

static struct plugin_ctx *plugin_ctx_create (void)
{
    struct plugin_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->client = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                                     getenv ("PMI_RANK"),
                                                     getenv ("PMI_SIZE"),
                                                     getenv ("PMI_SPAWNED")))) {
        plugin_ctx_destroy (ctx);
        return NULL;
    }
    return ctx;
}

static int op_put (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    const char *key;
    const char *value;
    int result;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s}",
                                "key", &key,
                                "value", &value) < 0)
        return upmi_seterror (p, args, "error unpacking put arguments");

    result = pmi_simple_client_kvs_put (ctx->client, ctx->kvsname, key, value);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    return 0;
}

static int op_get (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;
    const char *key;
    char value[1024];

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "key", &key) < 0)
        return upmi_seterror (p, args, "error unpacking get arguments");

    result = pmi_simple_client_kvs_get (ctx->client,
                                        ctx->kvsname,
                                        key,
                                        value,
                                        sizeof (value));
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:s}",
                              "value", value) < 0)
        return -1;
    return 0;
}

static int op_barrier (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;

    result = pmi_simple_client_barrier (ctx->client);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));
    return 0;
}

static int op_abort (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    const char *msg;
    int result;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "msg", &msg) < 0)
        return upmi_seterror (p, args, "error unpacking abort arguments");
    result = pmi_simple_client_abort (ctx->client, 1, msg);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));
    return 0;
}

static int op_initialize (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;

    result = pmi_simple_client_init (ctx->client);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    result = pmi_simple_client_kvs_get_my_name (ctx->client,
                                                ctx->kvsname,
                                                sizeof (ctx->kvsname));
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:i s:s s:i}",
                              "rank", ctx->client->rank,
                              "name", ctx->kvsname,
                              "size", ctx->client->size) < 0)
        return -1;
    return 0;
}

static int op_finalize (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;

    result = pmi_simple_client_finalize (ctx->client);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));
    return 0;
}

static int op_preinit (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct plugin_ctx *ctx;
    const char *vars[] = { "PMI_FD", "PMI_RANK", "PMI_SIZE" }; // required

    for (int i = 0; i < ARRAY_SIZE (vars); i++) {
        if (!getenv (vars[i]))
            return upmi_seterror (p, args, "%s not found in environ", vars[i]);
    }
    if (!(ctx = plugin_ctx_create ())
        || flux_plugin_aux_set (p,
                                plugin_name,
                                ctx,
                                (flux_free_f)plugin_ctx_destroy) < 0) {
        plugin_ctx_destroy (ctx);
        return upmi_seterror (p, args, "create context: %s", strerror (errno));
    }
    return 0;
}

static const struct flux_plugin_handler optab[] = {
    { "upmi.put",           op_put,         NULL },
    { "upmi.get",           op_get,         NULL },
    { "upmi.barrier",       op_barrier,     NULL },
    { "upmi.abort",         op_abort,       NULL },
    { "upmi.initialize",    op_initialize,  NULL },
    { "upmi.finalize",      op_finalize,    NULL },
    { "upmi.preinit",       op_preinit,     NULL },
    { 0 },
};



int upmi_simple_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, plugin_name, optab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
