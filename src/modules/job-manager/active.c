/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* active - manipulate active jobs in the KVS
 *
 * Active jobs are stored in the KVS under "jobs.active" per RFC 16.
 *
 * To avoid the job.active directory becoming large and impacting KVS
 * performance over time, jobs are spread across subdirectries using
 * FLUID_STRING_DOTHEX encoding (see fluid.h).
 *
 * In general, an operation that alters the job state follows this pattern:
 * - prepare KVS transaction
 * - commit KVS transaction, with continuation
 * - on success: continuation updates in-memory job state and completes request
 * - on error: in-memory job state is unchanged and error is returned to caller
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>

#include "src/common/libjob/job.h"
#include "src/common/libutil/fluid.h"

#include "job.h"
#include "active.h"

int active_key (char *buf, int bufsz, struct job *job, const char *key)
{
    char idstr[32];
    int len;

    if (fluid_encode (idstr, sizeof (idstr), job->id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    len = snprintf (buf, bufsz, "job.active.%s%s%s",
                    idstr,
                    key ? "." : "",
                    key ? key : "");
    if (len >= bufsz)
        return -1;
    return len;
}

int active_eventlog_append (flux_kvs_txn_t *txn,
                            struct job *job,
                            const char *key,
                            const char *name,
                            const char *fmt, ...)
{
    va_list ap;
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    int n;
    char path[64];
    char *event = NULL;
    int saved_errno;

    va_start (ap, fmt);
    n = vsnprintf (context, sizeof (context), fmt, ap);
    va_end (ap);
    if (n >= sizeof (context))
        goto error_inval;
    if (active_key (path, sizeof (path), job, key) < 0)
        goto error_inval;
    if (!(event = flux_kvs_event_encode (name, context)))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, path, event) < 0)
        goto error;
    free (event);
    return 0;
error_inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    free (event);
    errno = saved_errno;
    return -1;
}

int active_pack (flux_kvs_txn_t *txn,
                 struct job *job,
                 const char *key,
                 const char *fmt, ...)
{
    va_list ap;
    int n;
    char path[64];

    if (active_key (path, sizeof (path), job, key) < 0)
        goto error_inval;
    va_start (ap, fmt);
    n = flux_kvs_txn_vpack (txn, 0, path, fmt, ap);
    va_end (ap);
    if (n < 0)
        goto error;
    return 0;
error_inval:
    errno = EINVAL;
error:
    return -1;
}

int active_unlink (flux_kvs_txn_t *txn, struct job *job)
{
    char path[64];

    if (active_key (path, sizeof (path), job, NULL) < 0)
        goto error_inval;
    if (flux_kvs_txn_unlink (txn, 0, path) < 0)
        goto error;
    return 0;
error_inval:
    errno = EINVAL;
error:
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
