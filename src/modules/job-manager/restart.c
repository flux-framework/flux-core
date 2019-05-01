/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* restart - reload active jobs from the KVS */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <argz.h>
#include <envz.h>
#include <flux/core.h>

#include "src/common/libutil/fluid.h"

#include "job.h"
#include "restart.h"
#include "event.h"

struct restart_ctx {
    struct queue *queue;
    struct event_ctx *event_ctx;
    flux_t *h;
};

/* restart_map callback should return -1 on error to stop map with error,
 * or 0 on success.  'job' is only valid for the duration of the callback.
 */
typedef int (*restart_map_f)(struct job *job, void *arg);

int restart_count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static int depthfirst_map_one (flux_t *h, const char *key, int dirskip,
                               restart_map_f cb, void *arg)
{
    flux_jobid_t id;
    flux_future_t *f;
    const char *eventlog;
    struct job *job = NULL;
    char path[64];
    int rc = -1;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    if (flux_job_kvs_key (path, sizeof (path), id, "eventlog") < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, path)))
        goto done;
    if (flux_kvs_lookup_get (f, &eventlog) < 0)
        goto done;
    if (!(job = job_create_from_eventlog (id, eventlog)))
        goto done;
    if (cb (job, arg) < 0)
        goto done;
    rc = 1;
done:
    flux_future_destroy (f);
    job_decref (job);
    return rc;
}

static int depthfirst_map (flux_t *h, const char *key,
                           int dirskip, restart_map_f cb, void *arg)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = restart_count_char (key + dirskip, '.');
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

/* reload_map_f callback
 * The job state/flags has been recreated by replaying the job's eventlog.
 * Enqueue the job and kick off actions appropriate for job's current state.
 */
static int restart_map_cb (struct job *job, void *arg)
{
    struct restart_ctx *ctx = arg;

    if (queue_insert (ctx->queue, job, &job->queue_handle) < 0)
        return -1;
    if (event_job_action (ctx->event_ctx, job) < 0) {
        flux_log_error (ctx->h, "%s: event_job_action id=%llu",
                        __FUNCTION__, (unsigned long long)job->id);
    }
    return 0;
}

/* Load any active jobs present in the KVS at startup.
 */
int restart_from_kvs (flux_t *h, struct queue *queue,
                      struct event_ctx *event_ctx)
{
    const char *dirname = "job";
    int dirskip = strlen (dirname);
    int count;
    struct restart_ctx ctx;

    ctx.h = h;
    ctx.queue = queue;
    ctx.event_ctx = event_ctx;

    count = depthfirst_map (h, dirname, dirskip, restart_map_cb, &ctx);
    if (count < 0)
        return -1;
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__, count);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
