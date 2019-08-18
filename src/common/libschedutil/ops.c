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
#include <stdlib.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errno_safe.h"
#include "schedutil_private.h"
#include "init.h"
#include "ops.h"

static void alloc_continuation (flux_future_t *f, void *arg)
{
    schedutil_t *util = arg;
    flux_t *h = util->h;
    const flux_msg_t *msg = flux_future_aux_get (f, "schedutil::msg");

    if (util == NULL) {
        errno = EINVAL;
        goto error;
    }
    const char *jobspec;

    if (flux_kvs_lookup_get (f, &jobspec) < 0) {
        flux_log_error (h, "sched.alloc lookup R");
        goto error;
    }
    util->alloc_cb (h, msg, jobspec, util->cb_arg);
    if (schedutil_remove_outstanding_future (util, f) < 0)
        flux_log_error (h, "sched.alloc unable to remove outstanding future");
    flux_future_destroy (f);
    return;
error:
    flux_log_error (h, "sched.alloc");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.alloc respond_error");
    flux_future_destroy (f);
}

static void alloc_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    schedutil_t *util = arg;
    flux_jobid_t id;
    char key[64];
    flux_future_t *f;

    if (util == NULL) {
        errno = EINVAL;
        goto error;
    }

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (flux_job_kvs_key (key, sizeof (key), id, "jobspec") < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        goto error;
    if (flux_future_aux_set (f,
                             "schedutil::msg",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error_future;
    }
    if (!schedutil_hang_responses (util)) {
        if (flux_future_then (f, -1, alloc_continuation, util) < 0)
            goto error_future;
    }
    // else: intentionally do not register a continuation to force a permanent
    // outstanding request for testing
    if (schedutil_add_outstanding_future (util, f) < 0)
        flux_log_error (h, "sched.alloc unable to add outstanding future");

    return;
error_future:
    flux_future_destroy (f);
error:
    flux_log_error (h, "sched.alloc");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.alloc respond_error");
}

static void free_continuation (flux_future_t *f, void *arg)
{
    schedutil_t *util = arg;
    const flux_msg_t *msg = flux_future_aux_get (f, "schedutil::msg");
    flux_t *h = util->h;
    const char *R;

    if (flux_kvs_lookup_get (f, &R) < 0) {
        flux_log_error (h, "sched.free lookup R");
        goto error;
    }
    util->free_cb (h, msg, R, util->cb_arg);
    if (schedutil_remove_outstanding_future (util, f) < 0)
        flux_log_error (h, "sched.free unable to remove outstanding future");
    flux_future_destroy (f);
    return;
error:
    flux_log_error (h, "sched.free");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.free respond_error");
    flux_future_destroy (f);
}

static void free_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    schedutil_t *util = arg;
    flux_jobid_t id;
    flux_future_t *f;
    char key[64];

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (flux_job_kvs_key (key, sizeof (key), id, "R") < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        goto error;
    if (flux_future_aux_set (f,
                             "schedutil::msg",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error_future;
    }
    if (!schedutil_hang_responses (util)) {
        if (flux_future_then (f, -1, free_continuation, util) < 0)
            goto error_future;
    }
    /* else: intentionally do not register a continuation to force
     * a permanent outstanding request for testing
     */
    if (schedutil_add_outstanding_future (util, f) < 0)
        flux_log_error (h, "sched.free unable to add outstanding future");

    return;
error_future:
    flux_future_destroy (f);
error:
    flux_log_error (h, "sched.free");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.free respond_error");
}

static void exception_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    schedutil_t *util = arg;
    flux_jobid_t id;
    const char *type;
    int severity;

    if (flux_event_unpack (msg, NULL, "{s:I s:s s:i}",
                                      "id", &id,
                                      "type", &type,
                                      "severity", &severity) < 0) {
        flux_log_error (h, "job-exception event");
        return;
    }
    util->exception_cb (h, id, type, severity, util->cb_arg);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "sched.alloc", alloc_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "sched.free", free_cb, 0},
    { FLUX_MSGTYPE_EVENT,  "job-exception", exception_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

/* Register dynamic service named 'sched'
 */
static int service_register (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_service_register (h, "sched")))
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_log (h, LOG_DEBUG, "service_register");
    flux_future_destroy (f);
    return 0;
}

/* Unregister dynamic service name 'sched'
 */
static void service_unregister (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_service_unregister (h, "sched"))) {
        flux_log_error (h, "service_unregister");
        return;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "service_unregister");
        flux_future_destroy (f);
        return;
    }
    flux_log (h, LOG_DEBUG, "service_unregister");
    flux_future_destroy (f);
}

int schedutil_ops_register (schedutil_t *util)
{
    flux_t *h = util->h;

    if (!util) {
        errno = EINVAL;
        return -1;
    }
    if (service_register (h) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, util, &util->handlers) < 0)
        goto error;
    if (flux_event_subscribe (h, "job-exception") < 0)
        goto error;
    return 0;
error:
    schedutil_ops_unregister (util);
    return -1;
}

void schedutil_ops_unregister (schedutil_t *util)
{
    if (!util)
        return;

    service_unregister (util->h);
    (void)flux_event_unsubscribe (util->h, "job-exception");
    flux_msg_handler_delvec (util->handlers);
 }

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
