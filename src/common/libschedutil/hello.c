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

#include "hello.h"
#include "jobkey.h"

static int schedutil_hello_job (flux_t *h, flux_jobid_t id,
                                hello_f *cb, void *arg)
{
    char key[64];
    flux_future_t *f;
    const char *s;

    if (schedutil_jobkey (key, sizeof (key), true, id, "R") < 0) {
        errno = EPROTO;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        return -1;
    if (flux_kvs_lookup_get (f, &s) < 0)
        goto error;
    if (cb (h, s, arg) < 0)
        goto error;
    flux_future_destroy (f);
    return 0;
error:
    flux_log_error (h, "hello: error loading R for id=%llu",
                    (unsigned long long)id);
    flux_future_destroy (f);
    return -1;
}

int schedutil_hello (flux_t *h, hello_f *cb, void *arg)
{
    flux_future_t *f;
    json_t *ids;
    json_t *id;
    size_t index;

    if (!h || !cb) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc (h, "job-manager.sched-hello",
                        NULL, FLUX_NODEID_ANY, 0)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:o}", "alloc", &ids) < 0)
        goto error;
    json_array_foreach (ids, index, id) {
        flux_jobid_t jobid = json_integer_value (id);
        if (schedutil_hello_job (h, jobid, cb, arg) < 0)
            goto error;
    }
    flux_future_destroy (f);
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
