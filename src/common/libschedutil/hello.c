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
#include <czmq.h>

#include "src/common/libutil/errno_safe.h"

#include "schedutil_private.h"
#include "init.h"

struct job_info {
    flux_jobid_t id;
    int priority;
    uint32_t userid;
    double t_submit;
};

static struct job_info *job_info_copy (const struct job_info *info)
{
    struct job_info *cpy;

    if (!(cpy = malloc (sizeof (*cpy))))
        return NULL;
    *cpy = *info;
    return cpy;
}

static void job_info_destroy (void *info)
{
    ERRNO_SAFE_WRAP (free, info);
}

static int hello_finalize (schedutil_t *util)
{
    flux_future_t *f;
    while ((f = zlist_pop (util->f_hello)))
        flux_future_destroy (f);
    return util->ops->ready (util->h, util->cb_arg);
}

static void hello_job_continuation (flux_future_t *f, void *arg)
{
    schedutil_t *util = arg;
    struct job_info *info = flux_future_aux_get (f, "jobinfo");
    const char *R;

    if (flux_kvs_lookup_get (f, &R) < 0) {
        flux_log_error (util->h, "hello: KVS lookup of R failed id=%ju",
                                 (uintmax_t)info->id);
        goto error;
    }
    if (util->ops->hello (util->h,
                          info->id,
                          info->priority,
                          info->userid,
                          info->t_submit,
                          R,
                          util->cb_arg) < 0) {
        flux_log_error (util->h, "hello: hello callback failed id=%ju",
                                 (uintmax_t)info->id);
        goto error;
    }
    if (--util->hello_job_count == 0 && hello_finalize (util) < 0) {
        flux_log_error (util->h, "hello: ready callback failed");
        goto error;
    }
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (util->h));
}

static int schedutil_hello_job (schedutil_t *util, const struct job_info *info)
{
    char key[64];
    flux_future_t *f;
    struct job_info *cpy;

    if (!util) {
        errno = EINVAL;
        return -1;
    }
    if (flux_job_kvs_key (key, sizeof (key), info->id, "R") < 0) {
        errno = EPROTO;
        return -1;
    }
    if (!(f = flux_kvs_lookup (util->h, NULL, 0, key)))
        return -1;
    if (flux_future_then (f, -1, hello_job_continuation, util) < 0)
        goto error;
    if (!(cpy = job_info_copy (info)))
        goto error;
    if (flux_future_aux_set (f, "jobinfo", cpy, job_info_destroy) < 0) {
        job_info_destroy (cpy);
        goto error;
    }
    if (zlist_append (util->f_hello, f) < 0) {
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

static void hello_continuation (flux_future_t *f, void *arg)
{
    schedutil_t *util = arg;
    json_t *jobs;
    json_t *entry;
    size_t index;

    if (flux_rpc_get_unpack (f, "{s:o}", "alloc", &jobs) < 0) {
        flux_log_error (util->h, "hello: error handling response");
        goto error;
    }
    util->hello_job_count = json_array_size (jobs);
    json_array_foreach (jobs, index, entry) {
        struct job_info info;

        if (json_unpack (entry, "{s:I s:i s:i s:f}",
                                "id", &info.id,
                                "priority", &info.priority,
                                "userid", &info.userid,
                                "t_submit", &info.t_submit) < 0) {
            errno = EPROTO;
            flux_log_error (util->h, "hello: error handling response");
            goto error;
        }
        if (schedutil_hello_job (util, &info) < 0) {
            flux_log_error (util->h, "hello: error requesting R for id=%ju",
                                     (uintmax_t)info.id);
            goto error;
        }
    }
    if (util->ops->ready (util->h, util->cb_arg) < 0) {
        flux_log_error (util->h, "hello: ready callback failed");
        goto error;
    }
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (util->h));
}

int su_hello_begin (schedutil_t *util)
{
    flux_future_t *f;

    if (!util || !util->ops->hello || !util->ops->ready) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_rpc (util->h, "job-manager.sched-hello",
                        NULL, FLUX_NODEID_ANY, 0)))
        return -1;
    if (flux_future_then (f, -1, hello_continuation, util) < 0)
        goto error;
    if (zlist_append (util->f_hello, f) < 0) {
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
