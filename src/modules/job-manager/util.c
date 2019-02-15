/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* util - misc. job manager support
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>

#include "src/common/libjob/job.h"
#include "src/common/libutil/fluid.h"

#include "job.h"
#include "util.h"

int util_jobkey (char *buf, int bufsz, bool active,
                 struct job *job, const char *key)
{
    char idstr[32];
    int len;

    if (fluid_encode (idstr, sizeof (idstr), job->id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    len = snprintf (buf, bufsz, "job.%s.%s%s%s",
                    active ? "active" : "inactive",
                    idstr,
                    key ? "." : "",
                    key ? key : "");
    if (len >= bufsz)
        return -1;
    return len;
}

int util_eventlog_append (flux_kvs_txn_t *txn,
                          struct job *job,
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
    if (util_jobkey (path, sizeof (path), true, job, "eventlog") < 0)
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

int util_attr_pack (flux_kvs_txn_t *txn,
                    struct job *job,
                    const char *key,
                    const char *fmt, ...)
{
    va_list ap;
    int n;
    char path[64];

    if (util_jobkey (path, sizeof (path), true, job, key) < 0)
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
