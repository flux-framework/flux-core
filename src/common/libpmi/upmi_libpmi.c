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

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "simple_client.h"
#include "pmi_strerror.h"
#include "pmi.h"
#include "upmi.h"
#include "upmi_plugin.h"

struct upmi_libpmi {
    void *dso;
    struct upmi *upmi;
    int (*init) (int *spawned);
    int (*finalize) (void);
    int (*get_size) (int *size);
    int (*get_rank) (int *rank);
    int (*barrier) (void);
    int (*kvs_get_my_name) (char *kvsname, int length);
    int (*kvs_put) (const char *kvsname, const char *key, const char *value);
    int (*kvs_commit) (const char *kvsname);
    int (*kvs_get) (const char *kvsname, const char *key, char *value, int len);
    char kvsname[1024];
};

static const char *op_getname (void)
{
    return "libpmi";
}

static int dlopen_wrap (const char *path,
                        int flags,
                        struct upmi *upmi,
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
    if (upmi_has_flag (upmi, UPMI_LIBPMI_NOFLUX)) {
        if (dlsym (dso, "flux_pmi_library") != NULL) {
            errprintf (error, "%s: dlopen found Flux library", path);
            goto error;
        }
    }
    *result = dso;
    return 0;
error:
    dlclose (dso);
    return -1;
}

static void *op_create (struct upmi *upmi,
                        const char *path,
                        flux_error_t *error)
{
    struct upmi_libpmi *ctx;
    int spawned;
    int result;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    ctx->upmi = upmi;
    if (!path)
        path = "libpmi.so";
    // Use RTLD_GLOBAL due to flux-framework/flux-core#432
    if (dlopen_wrap (path,
                     RTLD_NOW | RTLD_GLOBAL,
                     upmi,
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
        || !(ctx->kvs_get = dlsym (ctx->dso, "PMI_KVS_Get"))) {
        errprintf (error, "%s:  missing required PMI_* symbols", path);
        goto error;
    }
    /* Since upmi_create (path=NULL) is used to search for a working PMI,
     * we should check if the library is working.
     * N.B. Flux's PMI_Init() fails when there is no process manager, while
     * Slurm's will succeed outside of a Slurm job and act as a singleton.
     */
    result = ctx->init (&spawned);
    if (result != PMI_SUCCESS) {
        errprintf (error, "init %s", pmi_strerror (result));
        goto error;
    }
    return ctx;
error:
    if (ctx->dso)
        dlclose (ctx->dso);
    free (ctx);
    return NULL;
}

static void op_destroy (void *data)
{
    struct upmi_libpmi *ctx = data;

    if (ctx) {
        int saved_errno = errno;
        if (ctx->dso)
            dlclose (ctx->dso);
        free (ctx);
        errno = saved_errno;
    }
}

static int op_initialize (void *data,
                          struct upmi_info *info,
                          flux_error_t *error)
{
    struct upmi_libpmi *ctx = data;
    int result;
    int rank;
    int size;

    /* N.B. PMI_Init() was called in op_create()
     */

    result = ctx->kvs_get_my_name (ctx->kvsname, sizeof (ctx->kvsname));
    if (result != PMI_SUCCESS) {
        errprintf (error, "fetch KVS name: %s", pmi_strerror (result));
        return -1;
    }

    result = ctx->get_rank (&rank);
    if (result != PMI_SUCCESS) {
        errprintf (error, "fetch rank: %s", pmi_strerror (result));
        return -1;
    }

    result = ctx->get_size (&size);
    if (result != PMI_SUCCESS) {
        errprintf (error, "fetch size: %s", pmi_strerror (result));
        return -1;
    }
    info->rank = rank;
    info->size = size;
    info->name = ctx->kvsname;

    return 0;
}

static int op_finalize (void *data, flux_error_t *error)
{
    struct upmi_libpmi *ctx = data;
    int result;

    result = ctx->finalize ();
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
    struct upmi_libpmi *ctx = data;
    int result;

    result = ctx->kvs_put (ctx->kvsname, key, val);
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

    struct upmi_libpmi *ctx = data;
    int result;
    char *cpy;

    result = ctx->kvs_get (ctx->kvsname, key, buf, sizeof (buf));
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
    struct upmi_libpmi *ctx = data;
    int result;

    result = ctx->kvs_commit (ctx->kvsname);
    if (result != PMI_SUCCESS) {
        errprintf (error, "kvs commit: %s", pmi_strerror (result));
        return -1;
    }

    result = ctx->barrier ();
    if (result != PMI_SUCCESS) {
        errprintf (error, "%s", pmi_strerror (result));
        return -1;
    }
    return 0;
}

const struct upmi_plugin upmi_libpmi = {
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
