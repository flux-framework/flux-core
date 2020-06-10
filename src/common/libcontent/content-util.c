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

#include "content-util.h"

int content_register_backing_store (flux_t *h, const char *name)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "content.register-backing",
                             0,
                             0,
                             "{s:s}",
                             "name",
                             name))) {
        flux_log_error (h, "register-backing");
        return -1;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "register-backing: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int content_unregister_backing_store (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h, "content.unregister-backing", NULL, 0, 0))) {
        flux_log_error (h, "unregister-backing");
        return -1;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "unregister-backing: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int content_register_service (flux_t *h, const char *name)
{
    flux_future_t *f;

    if (!(f = flux_service_register (h, name))) {
        flux_log_error (h, "register %s", name);
        return -1;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "register %s: %s", name, future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
