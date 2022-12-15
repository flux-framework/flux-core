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
#include <pmix.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"

#include "pmi_strerror.h"
#include "pmi.h"
#include "upmi.h"
#include "upmi_plugin.h"

struct upmi_pmix {
    struct upmi *upmi;
    pmix_proc_t myproc;
};

static const char *op_getname (void)
{
    return "pmix";
}

static void *op_create (struct upmi *upmi,
                        const char *path,
                        flux_error_t *error)
{
    struct upmi_pmix *ctx;

    if (!getenv ("PMIX_SERVER_URI") && !getenv ("PMIX_SERVER_URI2")) {
        errprintf (error, "PMIX_SERVER variables are missing from environment");
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    ctx->upmi = upmi;
    return ctx;
}

static void op_destroy (void *data)
{
    struct upmi_pmix *ctx = data;

    if (ctx) {
        int saved_errno = errno;
        free (ctx);
        errno = saved_errno;
    }
}

static int get_size (struct upmi_pmix *ctx, int *sizep, flux_error_t *error)
{
    pmix_status_t result;
    pmix_value_t *val;
    pmix_info_t inf[1];
    bool val_optional = 1;
    pmix_proc_t proc;

    PMIX_INFO_CONSTRUCT (&inf[0]);
    PMIX_INFO_LOAD (&inf[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);

    proc = ctx->myproc;
    proc.rank = PMIX_RANK_WILDCARD;

    result = PMIx_Get (&proc, PMIX_JOB_SIZE, inf, 1, &val);

    PMIX_INFO_DESTRUCT (&inf[0]);

    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    if (val->type != PMIX_UINT32) {
        errprintf (error, "PMIX_JOB_SIZE value is not of type UINT32");
        PMIX_VALUE_RELEASE (val);
        return -1;
    }
    *sizep = val->data.uint32;
    PMIX_VALUE_RELEASE (val);
    return 0;
}

static int op_initialize (void *data,
                          struct upmi_info *info,
                          flux_error_t *error)
{
    struct upmi_pmix *ctx = data;
    pmix_status_t result;

    result = PMIx_Init (&ctx->myproc, NULL, 0);
    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    if (get_size (ctx, &info->size, error) < 0)
        return -1;
    info->rank = ctx->myproc.rank;
    info->name = ctx->myproc.nspace;
    return 0;
}

static int op_finalize (void *data, flux_error_t *error)
{
    pmix_status_t result;

    result = PMIx_Finalize (NULL, 0);
    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    return 0;
}

static int op_put (void *data,
                   const char *key,
                   const char *value,
                   flux_error_t *error)
{
    pmix_status_t result;
    pmix_value_t val;

    val.type = PMIX_STRING;
    val.data.string = (char *)value;

    result = PMIx_Put (PMIX_GLOBAL, key, &val);
    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    return 0;
}

static int op_get (void *data,
                   const char *key,
                   int rank,
                   char **value,
                   flux_error_t *error)
{
    struct upmi_pmix *ctx = data;
    pmix_proc_t proc;
    pmix_value_t *val;
    pmix_status_t result;
    pmix_info_t info[1];
    bool val_optional = 1;
    char *cpy;

    /* If rank < 0, assume that value was stored by the
     * enclosing instance using PMIx_server_register_nspace()
     * or equivalent, so that it's either in the client cache,
     * or fails immediately.
     */
    if (rank < 0) {
        proc = ctx->myproc;
        proc.rank = PMIX_RANK_UNDEF;

        PMIX_INFO_CONSTRUCT (&info[0]);
        PMIX_INFO_LOAD (&info[0], PMIX_OPTIONAL, &val_optional, PMIX_BOOL);

        result = PMIx_Get (&proc, key, info, 1, &val);

        PMIX_INFO_DESTRUCT (&info[0]);
    }
    else {
        proc = ctx->myproc;
        proc.rank = rank;

        result = PMIx_Get (&proc, key, NULL, 0, &val);
    }
    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    if (val->type != PMIX_STRING || val->data.string == NULL) {
        errprintf (error, "value is not a string type");
        PMIX_VALUE_RELEASE (val);
        return -1;
    }

    if (!(cpy = strdup (val->data.string))) {
        errprintf (error, "out of memory");
        PMIX_VALUE_RELEASE (val);
        return -1;
    }
    PMIX_VALUE_RELEASE (val);
    *value = cpy;
    return 0;
}

static int op_barrier (void *data, flux_error_t *error)
{
    pmix_status_t result;
    pmix_info_t buf;
    pmix_info_t *info = &buf;
    bool val = 1;
    int ninfo = 1;

    result = PMIx_Commit ();
    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }

    PMIX_INFO_CONSTRUCT (info);
    PMIX_INFO_LOAD (info, PMIX_COLLECT_DATA, &val, PMIX_BOOL);

    result = PMIx_Fence (NULL, 0, info, ninfo);

    PMIX_INFO_DESTRUCT (info);

    if (result != PMIX_SUCCESS) {
        errprintf (error, "%s", PMIx_Error_string (result));
        return -1;
    }
    return 0;
}

const struct upmi_plugin upmi_pmix = {
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
