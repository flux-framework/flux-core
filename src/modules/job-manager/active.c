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

static int count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static int decode_eventlog (const char *s, double *t_submit, int *flagsp)
{
    struct flux_kvs_eventlog *eventlog;
    const char *event;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    double t;
    int flags = 0;

    if (!(eventlog = flux_kvs_eventlog_decode (s)))
        return -1;
    if (!(event = flux_kvs_eventlog_first (eventlog)))
        goto error;
    if (flux_kvs_event_decode (event, &t, name, sizeof (name), NULL, 0) < 0)
        goto error;
    if (strcmp (name, "submit") != 0)
        goto error_inval;
    while ((event = flux_kvs_eventlog_next (eventlog))) {
        if (flux_kvs_event_decode (event, NULL, name, sizeof (name),
                                                                NULL, 0) < 0)
            goto error;
        /* Set flags based on events here */
    }
    *t_submit = t;
    *flagsp = flags;
    flux_kvs_eventlog_destroy (eventlog);
    return 0;
error_inval:
    errno = EINVAL;
error:
    flux_kvs_eventlog_destroy (eventlog);
    return -1;
}

static flux_future_t *lookup_job_attr (flux_t *h, const char *jobdir,
                                       const char *name)
{
    char key[80];

    if (snprintf (key, sizeof (key), "%s.%s", jobdir, name) >= sizeof (key)) {
        errno = EINVAL;
        return NULL;
    }
    return flux_kvs_lookup (h, NULL, 0, key);
}

static int depthfirst_map_one (flux_t *h, const char *key, int dirskip,
                               active_map_f cb, void *arg)
{
    flux_jobid_t id;
    flux_future_t *f = NULL;
    uint32_t userid;
    int priority;
    const char *eventlog;
    struct job *job;
    double t_submit;
    int flags;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    /* userid */
    if (!(f = lookup_job_attr (h, key, "userid")))
        goto error;
    if (flux_kvs_lookup_get_unpack (f, "i", &userid) < 0)
        goto error_future;
    flux_future_destroy (f);

    /* priority */
    if (!(f = lookup_job_attr (h, key, "priority")))
        goto error;
    if (flux_kvs_lookup_get_unpack (f, "i", &priority) < 0)
        goto error_future;
    flux_future_destroy (f);

    /* t_submit, inactive (via eventlog) */
    if (!(f = lookup_job_attr (h, key, "eventlog")))
        goto error;
    if (flux_kvs_lookup_get (f, &eventlog) < 0)
        goto error_future;
    if (decode_eventlog (eventlog, &t_submit, &flags) < 0)
        goto error_future;
    flux_future_destroy (f);

    /* make callback */
    if (!(job = job_create (id, priority, userid, t_submit, flags)))
        goto error;
    if (cb (job, arg) < 0) {
        job_decref (job);
        goto error;
    }
    job_decref (job);
    return 1;
error_future:
    flux_future_destroy (f);
error:
    return -1;
}

static int depthfirst_map (flux_t *h, const char *key,
                           int dirskip, active_map_f cb, void *arg)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = count_char (key + dirskip, '.');
    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_READDIR, key)))
        return -1;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && path_level == 0)
            rc = 0;
        goto done;
    }
    if (!(itr = flux_kvsitr_create (dir)))
        goto done;
    while ((name = flux_kvsitr_next (itr))) {
        char *nkey;
        int n;
        if (!flux_kvsdir_isdir (dir, name))
            continue;
        if (!(nkey = flux_kvsdir_key_at (dir, name)))
            goto done_destroyitr;
        if (path_level == 3) // orig 'key' = .A.B.C, thus 'nkey' is complete
            n = depthfirst_map_one (h, nkey, dirskip, cb, arg);
        else
            n = depthfirst_map (h, nkey, dirskip, cb, arg);
        if (n < 0) {
            int saved_errno = errno;
            free (nkey);
            errno = saved_errno;
            goto done_destroyitr;
        }
        count += n;
        free (nkey);
    }
    rc = count;
done_destroyitr:
    flux_kvsitr_destroy (itr);
done:
    flux_future_destroy (f);
    return rc;
}

int active_map (flux_t *h, active_map_f cb, void *arg)
{
    const char *dirname = "job.active";

    return depthfirst_map (h, dirname, strlen (dirname), cb, arg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
