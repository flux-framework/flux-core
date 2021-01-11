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

static int schedutil_hello_job (flux_t *h,
                                flux_jobid_t id,
                                int priority,
                                uint32_t userid,
                                double t_submit,
                                schedutil_hello_cb_f *cb,
                                void *arg)
{
    char key[64];
    flux_future_t *f;
    const char *R;

    if (!h) {
        errno = EINVAL;
        return -1;
    }

    if (flux_job_kvs_key (key, sizeof (key), id, "R") < 0) {
        errno = EPROTO;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        return -1;
    if (flux_kvs_lookup_get (f, &R) < 0)
        goto error;
    if (cb (h, id, priority, userid, t_submit, R, arg) < 0)
        goto error;
    flux_future_destroy (f);
    return 0;
error:
    flux_log_error (h, "hello: error loading R for id=%ju",
                    (uintmax_t)id);
    flux_future_destroy (f);
    return -1;
}

int schedutil_hello (schedutil_t *util, schedutil_hello_cb_f *cb, void *arg)
{
    flux_future_t *f;
    json_t *jobs;
    json_t *entry;
    size_t index;
    int rc = -1;

    if (!util || !cb) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc (util->h, "job-manager.sched-hello",
                        NULL, FLUX_NODEID_ANY, 0)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:o}", "alloc", &jobs) < 0)
        goto error;
    json_array_foreach (jobs, index, entry) {
        flux_jobid_t id;
        int priority;
        uint32_t userid;
        double t_submit;
        json_int_t tmp;

        if (json_unpack (entry, "{s:I s:I s:i s:f}",
                                "id", &id,
                                "priority", &tmp,
                                "userid", &userid,
                                "t_submit", &t_submit) < 0) {
            errno = EPROTO;
            goto error;
        }
        priority = tmp;
        if (schedutil_hello_job (util->h,
                                 id,
                                 priority,
                                 userid,
                                 t_submit,
                                 cb,
                                 arg) < 0)
            goto error;
    }
    rc = 0;
error:
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
