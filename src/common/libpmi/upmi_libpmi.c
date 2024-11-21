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
#include <dlfcn.h>
#if HAVE_LINK_H
#include <link.h>
#endif

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "simple_client.h"
#include "pmi_strerror.h"
#include "pmi.h"
#include "upmi.h"
#include "upmi_plugin.h"

struct plugin_ctx {
    void *dso;
    int (*init) (int *spawned);
    int (*finalize) (void);
    int (*abort) (int exit_code, const char *error_msg);
    int (*get_size) (int *size);
    int (*get_rank) (int *rank);
    int (*barrier) (void);
    int (*kvs_get_my_name) (char *kvsname, int length);
    int (*kvs_put) (const char *kvsname, const char *key, const char *value);
    int (*kvs_commit) (const char *kvsname);
    int (*kvs_get) (const char *kvsname, const char *key, char *value, int len);
    int rank;
    int size;
    char kvsname[1024];
};

static const char *plugin_name = "libpmi";

static const char *dlinfo_name (void *dso)
{
#if HAVE_LINK_H
    struct link_map *p = NULL;
    (void)dlinfo (dso, RTLD_DI_LINKMAP, &p);
    if (p && p->l_name)
        return p->l_name;
#endif
    return "unknown";
}

static int dlopen_wrap (const char *path,
                        int flags,
                        bool noflux,
                        void **result,
                        flux_error_t *error)
{
    void *dso;

    dlerror ();
    if (!(dso = dlopen (path, flags))) {
        char *errstr = dlerror ();
        if (errstr)
            errprintf (error, "%s", errstr); // dlerror() already includes path
        else
            errprintf (error, "%s: dlopen failed", path);
        return -1;
    }
    if (noflux) {
        if (dlsym (dso, "flux_pmi_library") != NULL) {
            errprintf (error,
                       "%s: dlopen found Flux library (%s)",
                       path,
                       dlinfo_name (dso));
            goto error;
        }
    }
    *result = dso;
    return 0;
error:
    dlclose (dso);
    return -1;
}

static void plugin_ctx_destroy (struct plugin_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        if (ctx->dso)
            dlclose (ctx->dso);
        free (ctx);
        errno = saved_errno;
    }
}

static struct plugin_ctx *plugin_ctx_create (const char *path,
                                             bool noflux,
                                             flux_error_t *error)
{
    struct plugin_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    if (!path)
        path = "libpmi.so";
    // Use RTLD_GLOBAL due to flux-framework/flux-core#432
    if (dlopen_wrap (path,
                     RTLD_NOW | RTLD_GLOBAL,
                     noflux,
                     &ctx->dso,
                     error) < 0)
        goto error;
    if (!(ctx->init = dlsym (ctx->dso, "PMI_Init"))
        || !(ctx->finalize = dlsym (ctx->dso, "PMI_Finalize"))
        || !(ctx->get_size = dlsym (ctx->dso, "PMI_Get_size"))
        || !(ctx->get_rank = dlsym (ctx->dso, "PMI_Get_rank"))
        || !(ctx->barrier = dlsym (ctx->dso, "PMI_Barrier"))
        || !(ctx->kvs_get_my_name = dlsym (ctx->dso, "PMI_KVS_Get_my_name"))
        || !(ctx->kvs_put = dlsym (ctx->dso, "PMI_KVS_Put"))
        || !(ctx->kvs_commit = dlsym (ctx->dso, "PMI_KVS_Commit"))
        || !(ctx->kvs_get = dlsym (ctx->dso, "PMI_KVS_Get"))
        || !(ctx->abort = dlsym (ctx->dso, "PMI_Abort"))) {
        errprintf (error, "%s:  missing required PMI_* symbols", path);
        goto error;
    }

    /* Cray's libpmi requires workarounds implemented in the libpmi2 plugin.
     * We shouldn't land here but if we do, fail early.
     * See flux-framework/flux-core#504
     */
    if (dlsym (ctx->dso, "PMI_CRAY_Get_app_size") != NULL) {
        errprintf (error, "refusing to use quirky cray libpmi.so");
        goto error;
    }

    return ctx;
error:
    if (ctx->dso)
        dlclose (ctx->dso);
    free (ctx);
    return NULL;
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

    result = ctx->kvs_put (ctx->kvsname, key, value);
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

    result = ctx->kvs_get (ctx->kvsname, key, value, sizeof (value));
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

    result = ctx->kvs_commit (ctx->kvsname);
    if (result != PMI_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    result = ctx->barrier ();
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
    int result;
    const char *msg;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "msg", &msg) < 0)
        return upmi_seterror (p, args, "error unpacking abort arguments");
    result = ctx->abort (1, msg);
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

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:i s:s s:i}",
                              "rank", ctx->rank,
                              "name", ctx->kvsname,
                              "size", ctx->size) < 0)
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

    result = ctx->finalize ();
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
    flux_error_t error;
    const char *path = NULL;
    int noflux = 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s?s s?b}",
                                "path", &path,
                                "noflux", &noflux) < 0)
        return upmi_seterror (p, args, "error unpacking preinit arguments");
    if (!(ctx = plugin_ctx_create (path, noflux, &error)))
        return upmi_seterror (p, args, "%s", error.text);
    if (flux_plugin_aux_set (p,
                             plugin_name,
                             ctx,
                             (flux_free_f)plugin_ctx_destroy) < 0) {
        plugin_ctx_destroy (ctx);
        return upmi_seterror (p, args, "%s", strerror (errno));
    }

    /* Call PMI_Init() and basic info functions now so that upmi can fall
     * through to the next plugin on failure.
     */
    int result;
    int spawned;
    const char *name = dlinfo_name (ctx->dso);

    result = ctx->init (&spawned);
    if (result != PMI_SUCCESS)
        goto error;
    result = ctx->kvs_get_my_name (ctx->kvsname, sizeof (ctx->kvsname));
    if (result != PMI_SUCCESS)
        goto error;
    result = ctx->get_rank (&ctx->rank);
    if (result != PMI_SUCCESS)
        goto error;
    result = ctx->get_size (&ctx->size);
    if (result != PMI_SUCCESS)
        goto error;

    char note[1024];
    snprintf (note, sizeof (note), "using %s", name);
    flux_plugin_arg_pack (args,
                          FLUX_PLUGIN_ARG_OUT,
                          "{s:s}",
                          "note", note);
    return 0;
error:
    return upmi_seterror (p, args, "%s: %s", name, pmi_strerror (result));
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

int upmi_libpmi_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, plugin_name, optab) < 0)
        return -1;
    return 0;
}


// vi:ts=4 sw=4 expandtab
