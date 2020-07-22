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
#include <jansson.h>
#include <czmq.h>

#include "src/common/libutil/errno_safe.h"

#include "schedutil_private.h"
#include "init.h"

static int acquire_next (schedutil_t *util, flux_future_t *f)
{
    const char *up = NULL;
    const char *down = NULL;

    if (flux_rpc_get_unpack (f, "{s?s s?s}", "up", &up, "down", &down) < 0) {
        flux_log_error (util->h, "acquire: response");
        return -1;
    }
    if (util->ops->resource_up && up)
        util->ops->resource_up (util->h, up, util->cb_arg);
    if (util->ops->resource_down && down)
        util->ops->resource_down (util->h, down, util->cb_arg);
    return 0;
}

static int acquire_first (schedutil_t *util, flux_future_t *f)
{
    json_t *o;
    const char *up;
    char *resobj = NULL;

    if (flux_rpc_get_unpack (f, "{s:o s:s}", "resources", &o, "up", &up) < 0) {
        flux_log_error (util->h, "acquire: response");
        return -1;
    }
    if (!(resobj = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        flux_log_error (util->h, "acquire: error re-encoding resource object");
        return -1;
    }
    if (util->ops->resource_acquire)
        util->ops->resource_acquire (util->h, resobj, util->cb_arg);
    if (util->ops->resource_up)
        util->ops->resource_up (util->h, up, util->cb_arg);
    free (resobj);
    return 0;
}

static void acquire_continuation (flux_future_t *f, void *arg)
{
    schedutil_t *util = arg;

    if (!util->resource_acquired) {
        if (acquire_first (util, f) < 0)
            goto error;
        if (su_hello_begin (util) < 0)
            goto error;
        util->resource_acquired = true;
    }
    else {
        if (acquire_next (util, f) < 0)
            goto error;
    }
    flux_future_reset (f);
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (util->h));
}

int su_resource_begin (schedutil_t *util)
{
    flux_future_t *f;

    if (!util) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc (util->h, "resource.acquire",
                        NULL, FLUX_NODEID_ANY, FLUX_RPC_STREAMING)))
        return -1;
    if (flux_future_then (f, -1, acquire_continuation, util) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    util->f_res = f;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
