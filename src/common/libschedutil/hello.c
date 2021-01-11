/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "schedutil_private.h"
#include "init.h"
#include "hello.h"

static int schedutil_hello_job (schedutil_t *util,
                                const flux_msg_t *msg)
{
    char key[64];
    flux_future_t *f = NULL;
    const char *R;
    flux_jobid_t id;

    if (flux_msg_unpack (msg, "{s:I}", "id", &id) < 0)
        goto error;
    if (flux_job_kvs_key (key, sizeof (key), id, "R") < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_kvs_lookup (util->h, NULL, 0, key)))
        goto error;
    if (flux_kvs_lookup_get (f, &R) < 0)
        goto error;
    if (util->ops->hello (util->h,
                          msg,
                          R,
                          util->cb_arg) < 0)
        goto error;
    flux_future_destroy (f);
    return 0;
error:
    flux_log_error (util->h, "hello: error loading R for id=%ju",
                    (uintmax_t)id);
    flux_future_destroy (f);
    return -1;
}

int schedutil_hello (schedutil_t *util)
{
    flux_future_t *f;
    int rc = -1;

    if (!util || !util->ops->hello) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc (util->h, "job-manager.sched-hello",
                        NULL, FLUX_NODEID_ANY, FLUX_RPC_STREAMING)))
        return -1;
    while (1) {
        const flux_msg_t *msg;
        if (flux_future_get (f, (const void **)&msg) < 0) {
            if (errno == ENODATA)
                break;
            goto error;
        }
        if (schedutil_hello_job (util, msg) < 0)
            goto error;
        flux_future_reset (f);
    }
    rc = 0;
error:
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
