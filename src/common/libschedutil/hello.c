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

#include "src/common/libjob/idf58.h"
#include "schedutil_private.h"
#include "init.h"
#include "hello.h"


static void raise_exception (flux_t *h, flux_jobid_t id, const char *note)
{
    flux_future_t *f;

    flux_log (h,
              LOG_INFO,
              "raising fatal exception on running job id=%s",
              idf58 (id));

    if (!(f = flux_job_raise (h, id, "scheduler-restart", 0, note))
        || flux_future_get (f, NULL) < 0) {
        flux_log_error (h,
                        "error raising fatal exception on %s: %s",
                        idf58 (id),
                        future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

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
        raise_exception (util->h,
                         id,
                         "failed to reallocate R for running job");
    flux_future_destroy (f);
    return 0;
error:
    flux_log_error (util->h,
                    "hello: error loading R for id=%s",
                    idf58 (id));
    flux_future_destroy (f);
    return -1;
}

int schedutil_hello (schedutil_t *util)
{
    flux_future_t *f;
    int rc = -1;
    int partial_ok = 0;

    if (!util || !util->ops->hello) {
        errno = EINVAL;
        return -1;
    }
    if ((util->flags & SCHEDUTIL_HELLO_PARTIAL_OK))
        partial_ok = 1;
    if (!(f = flux_rpc_pack (util->h,
                             "job-manager.sched-hello",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:b}",
                             "partial-ok", partial_ok)))
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
