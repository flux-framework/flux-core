/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bootstrap.c - determine rank, size, and peer endpoints */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

#include "attr.h"
#include "bootstrap.h"

const char *bootstrap_method (struct bootstrap *boot)
{
    return boot ? upmi_describe (boot->upmi) : "unknown";
}

static void trace_upmi (void *arg, const char *text)
{
    fprintf (stderr, "bootstrap: %s\n", text);
}

void bootstrap_destroy (struct bootstrap *boot)
{
    if (boot) {
        int saved_errno = errno;
        upmi_destroy (boot->upmi);
        free (boot);
        errno = saved_errno;
    }
}

struct bootstrap *bootstrap_create (struct broker *ctx,
                                    struct upmi_info *info,
                                    flux_error_t *errp)
{
    struct bootstrap *boot;
    const char *upmi_method = NULL;
    int upmi_flags = UPMI_LIBPMI_NOFLUX;
    json_t *upmi_args = NULL;
    json_t *conf_obj;
    flux_error_t error;

    if (!(boot = calloc (1, sizeof (*boot)))) {
        errprintf (errp, "out of memory");
        return NULL;
    }

    if (getenv ("FLUX_PMI_DEBUG"))
        upmi_flags |= UPMI_TRACE;

    if (flux_conf_unpack (flux_get_conf (ctx->h), NULL, "o", &conf_obj) < 0
        || !(upmi_args = json_pack ("{s:O s:s}",
                                    "config", conf_obj,
                                    "hostname", ctx->hostname))) {
        errprintf (errp, "error preparing upmi_args");
        goto error;
    }
    (void)attr_get (ctx->attrs, "broker.boot-method", &upmi_method, NULL);
    if (!(boot->upmi = upmi_create_ex (upmi_method,
                                       upmi_flags,
                                       upmi_args,
                                       trace_upmi,
                                       NULL,
                                       errp)))
        goto error;
    if (upmi_initialize (boot->upmi, info, &error) < 0) {
        errprintf (errp,
                   "%s: initialize: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }
    json_decref (upmi_args);
    return boot;
error:
    ERRNO_SAFE_WRAP (json_decref, upmi_args);
    bootstrap_destroy (boot);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
