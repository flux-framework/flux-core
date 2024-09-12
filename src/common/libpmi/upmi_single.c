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

#include "upmi.h"
#include "upmi_plugin.h"

struct plugin_ctx {
    json_t *kvs;
};

static const char *plugin_name = "single";

static void *plugin_ctx_create (void)
{
    struct plugin_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx)))
        || !(ctx->kvs = json_object ())) {
        return NULL;
    }
    return ctx;
}

static void plugin_ctx_destroy (struct plugin_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        json_decref (ctx->kvs);
        free (ctx);
        errno = saved_errno;
    }
}

static int op_put (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    const char *key;
    const char *value;
    json_t *o;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s}",
                                "key", &key,
                                "value", &value) < 0)
        return upmi_seterror (p, args, "error unpacking put arguments");
    if (!(o = json_string (value))
        || json_object_set_new (ctx->kvs, key, o) < 0) {
        json_decref (o);
        return upmi_seterror (p, args, "dictionary update error");
    }
    return 0;
}

static int op_get (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    const char *key;
    const char *value;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "key", &key) < 0)
        return upmi_seterror (p, args, "error unpacking get arguments");

    if (json_unpack (ctx->kvs, "{s:s}", key, &value) < 0)
        return upmi_seterror (p, args, "key not found");

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
    return 0;
}

static int op_abort (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    const char *msg;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "msg", &msg) < 0)
        return upmi_seterror (p, args, "error unpacking abort arguments");
    fprintf (stderr, "%s\n", msg);
    exit (1);
    //NOTREACHED
    return 0;
}

static int op_initialize (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:i s:s s:i}",
                              "rank", 0,
                              "name", plugin_name,
                              "size", 1) < 0)
        return -1;
    return 0;
}

static int op_finalize (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    return 0;
}

static int op_preinit (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct plugin_ctx *ctx;

    if (!(ctx = plugin_ctx_create ())
        || flux_plugin_aux_set (p,
                                plugin_name,
                                ctx,
                                (flux_free_f)plugin_ctx_destroy) < 0) {
        plugin_ctx_destroy (ctx);
        return upmi_seterror (p, args, "could not create upmi plugin context");
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

int upmi_single_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, plugin_name, optab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
