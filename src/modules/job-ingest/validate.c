/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* validate - asynchronous job validation interface
 *
 * Spawn worker(s) to validate job.  Up to 'DEFAULT_WORKER_COUNT'
 * workers may be active at one time.  They are started lazily, on demand,
 * and stop after a period of inactivity (see "tunables" below).
 *
 * Jobspec is expected to be in encoded JSON form, with or without
 * whitespace or NULL termination.  The encoding is normalized before
 * it is sent to the worker on a single line.
 *
 * The future is fulfilled with the result of validation.  On success,
 * the container will be empty.  On failure, the reason the job
 * did not pass validation (suitable for returning to the submitting user)
 * will be assigned to the future's extended error string.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <argz.h>
#include <jansson.h>
#include <assert.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "validate.h"
#include "worker.h"

/* Tunables:
 */

/* The maximum number of concurrent workers.
 */
#define MAX_WORKER_COUNT 4

/* Start a new worker if backlog reaches this level for all active workers.
 */
const int worker_queue_threshold = 32;

/* Workers exit once they have been inactive for this many seconds.
 */
const double worker_inactivity_timeout = 5.0;



struct validate {
    flux_t *h;
    struct worker *worker[MAX_WORKER_COUNT];
};

static void validate_killall (struct validate *v)
{
    flux_future_t *cf = NULL;
    flux_future_t *f;
    int i;

    if (v == NULL)
        return;
    if (!(cf = flux_future_wait_all_create ())) {
        flux_log_error (v->h, "validate_destroy: flux_future_wait_all_create");
        return;
    }
    flux_future_set_flux (cf, v->h);
    for (i = 0; i < MAX_WORKER_COUNT; i++) {
        if ((f = worker_kill (v->worker[i], SIGKILL)))
            flux_future_push (cf, NULL, f);
    }
    /* Wait for up to 5s for response that signals have been delivered
     *  to all workers before continuing. This should ensure no workers
     *  are left around after removal of the job-ingest module.
     *  (report, but otherwise ignore errors)
     */
    if (flux_future_wait_for (cf, 5.) < 0
        || flux_future_get (cf, NULL) < 0)
        flux_log_error (v->h, "validate_destroy: killing workers");
    flux_future_destroy (cf);
}

int validate_stop_notify (struct validate *v, process_exit_f cb, void *arg)
{
    int i;
    int count;

    if (v == NULL)
        return 0;

    count = 0;
    for (i = 0; i < MAX_WORKER_COUNT; i++)
        count += worker_stop_notify (v->worker[i], cb, arg);
    return count;
}

void validate_destroy (struct validate *v)
{
    if (v) {
        int saved_errno = errno;
        int i;
        validate_killall (v);
        for (i = 0; i < MAX_WORKER_COUNT; i++)
            worker_destroy (v->worker[i]);
        free (v);
        errno = saved_errno;
    }
}

static int validator_argz_create (char **argzp,
                                  size_t *argz_lenp,
                                  const char *validator_plugins,
                                  const char *validator_args)
{
    int e;
    if ((e = argz_add (argzp, argz_lenp, "flux"))
            || (e = argz_add (argzp, argz_lenp, "job-validator")))
        goto error;

    if (validator_plugins) {
        if ((e = argz_add (argzp, argz_lenp, "--plugins"))
            || (e = argz_add (argzp, argz_lenp, validator_plugins))) {
            goto error;
        }
    }
    if (validator_args
        && (e = argz_add_sep (argzp, argz_lenp, validator_args, ','))) {
        goto error;
    }
    return 0;
error:
    errno = e;
    return -1;
}

int validate_configure (struct validate *v,
                        const char *validator_plugins,
                        const char *validator_args)
{
    int rc = -1;
    int argc;
    char **argv = NULL;
    char *argz = NULL;
    size_t argz_len = 0;

    if (validator_argz_create (&argz,
                               &argz_len,
                               validator_plugins,
                               validator_args) < 0)
        goto error;

    argc = argz_count (argz, argz_len);
    if (!(argv = calloc (1, sizeof (char *) * (argc + 1)))) {
        flux_log_error (v->h, "failed to create argv");
        goto error;
    }
    argz_extract (argz, argz_len, argv);

    for (int i = 0; i < MAX_WORKER_COUNT; i++) {
        if (!v->worker[i]) {
            char name[256];
            (void) snprintf (name, sizeof (name), "validator[%d]", i);
            if (!(v->worker[i] = worker_create (v->h,
                                                worker_inactivity_timeout,
                                                name)))
                goto error;
        }
        if (worker_set_cmdline (v->worker[i], argc, argv) < 0)
            goto error;
    }
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, argv);
    ERRNO_SAFE_WRAP (free, argz);
    return rc;
}

struct validate *validate_create (flux_t *h)
{
    struct validate *v;
    if (!(v = calloc (1, sizeof (*v))))
        return NULL;
    v->h = h;
    return v;
}

/* Select worker with least backlog.  If none is running, or the best
 * has a backlog at or beyond threshold, activate a new one, if possible.
 */
struct worker *select_best_worker (struct validate *v)
{
    struct worker *best = NULL;
    struct worker *idle = NULL;
    int i;

    for (i = 0; i < MAX_WORKER_COUNT; i++) {
        if (worker_is_running (v->worker[i])) {
            if (!best || (worker_queue_depth (v->worker[i])
                        < worker_queue_depth (best)))
                best = v->worker[i];
        }
        else if (!idle)
            idle = v->worker[i];
    }
    if (idle && (!best || worker_queue_depth (best) >= worker_queue_threshold))
        best = idle;

    return best;
}

/* Re-encode job info in compact form to eliminate any white space (esp \n),
 * then pass it to least busy validation worker, returning a future.
 */
flux_future_t *validate_job (struct validate *v, json_t *job)
{
    flux_future_t *f;
    char *s;
    struct worker *w;

    if (!(s = json_dumps (job, JSON_COMPACT))) {
        errno = ENOMEM;
        goto error;
    }
    w = select_best_worker (v);
    assert (w != NULL);
    if (!(f = worker_request (w, s)))
        goto error;
    free (s);
    return f;
error:
    ERRNO_SAFE_WRAP (free, s);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
