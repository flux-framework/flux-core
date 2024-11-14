/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* workcrew - asynchronous worker interface
 *
 * Spawn worker(s) to process jobspec.  Up to 'DEFAULT_WORKER_COUNT'
 * workers may be active at one time.  They are started lazily, on demand,
 * selected based on least backlog, and stopped after a period of inactivity.
 *
 * Jobspec input is provided to workcrew_process_job() as a JSON object,
 * and is internally encoded as a single-line string for a worker.
 *
 * The future returned by workcrew_process_job() is fulfilled with the result
 * of worker execution (success or failure and optional JSON object).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <jansson.h>
#include <assert.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "workcrew.h"
#include "worker.h"

/* The maximum number of concurrent workers.
 */
#define WORKCREW_SIZE 4

/* Start a new worker if backlog reaches this level for all active workers.
 */
static const int workcrew_max_backlog = 32;

/* Workers exit once they have been inactive for this many seconds.
 */
static const double workcrew_inactivity_timeout = 5.0;

struct workcrew {
    flux_t *h;
    struct worker *worker[WORKCREW_SIZE];
};

static void workcrew_killall (struct workcrew *crew)
{
    flux_future_t *cf = NULL;
    flux_future_t *f;
    int i;

    if (crew == NULL)
        return;
    if (!(cf = flux_future_wait_all_create ())) {
        flux_log_error (crew->h, "workcrew: error setting up for killall");
        return;
    }
    flux_future_set_flux (cf, crew->h);
    for (i = 0; i < WORKCREW_SIZE; i++) {
        if ((f = worker_kill (crew->worker[i], SIGKILL)))
            flux_future_push (cf, NULL, f);
    }
    /* Wait for up to 5s for response that signals have been delivered
     *  to all workers before continuing. This should ensure no workers
     *  are left around after removal of the job-ingest module.
     *  (report, but otherwise ignore errors)
     */
    if (flux_future_wait_for (cf, 5.) < 0
        || flux_future_get (cf, NULL) < 0) {
        flux_log_error (crew->h,
                        "workcrew: killall failed: %s",
                        future_strerror (cf, errno));
    }
    flux_future_destroy (cf);
}

int workcrew_stop_notify (struct workcrew *crew, process_exit_f cb, void *arg)
{
    int i;
    int count;

    if (crew == NULL)
        return 0;

    count = 0;
    for (i = 0; i < WORKCREW_SIZE; i++)
        count += worker_stop_notify (crew->worker[i], cb, arg);
    return count;
}

void workcrew_destroy (struct workcrew *crew)
{
    if (crew) {
        int saved_errno = errno;
        int i;
        workcrew_killall (crew);
        for (i = 0; i < WORKCREW_SIZE; i++)
            worker_destroy (crew->worker[i]);
        free (crew);
        errno = saved_errno;
    }
}

static int create_worker_argz (char **argzp,
                               size_t *argz_lenp,
                               const char *cmdname,
                               const char *plugins,
                               const char *args)
{
    int e;
    if ((e = argz_add (argzp, argz_lenp, "flux"))
            || (e = argz_add (argzp, argz_lenp, cmdname)))
        goto error;

    if (plugins) {
        if ((e = argz_add (argzp, argz_lenp, "--plugins"))
            || (e = argz_add (argzp, argz_lenp, plugins))) {
            goto error;
        }
    }
    if (args
        && (e = argz_add_sep (argzp, argz_lenp, args, ','))) {
        goto error;
    }
    return 0;
error:
    errno = e;
    return -1;
}

int workcrew_configure (struct workcrew *crew,
                        const char *cmdname,
                        const char *plugins,
                        const char *args,
                        const char *bufsize)
{
    int rc = -1;
    int argc;
    char **argv = NULL;
    char *argz = NULL;
    size_t argz_len = 0;

    if (create_worker_argz (&argz, &argz_len, cmdname, plugins, args) < 0)
        goto error;

    argc = argz_count (argz, argz_len);
    if (!(argv = calloc (1, sizeof (char *) * (argc + 1)))) {
        flux_log_error (crew->h, "failed to create argv");
        goto error;
    }
    argz_extract (argz, argz_len, argv);

    for (int i = 0; i < WORKCREW_SIZE; i++) {
        if (!crew->worker[i]) {
            char name[256];
            (void) snprintf (name, sizeof (name), "%s[%d]", cmdname, i);
            if (!(crew->worker[i] = worker_create (crew->h,
                                                   workcrew_inactivity_timeout,
                                                   name)))
                goto error;
        }
        if (worker_set_cmdline (crew->worker[i], argc, argv) < 0)
            goto error;
        if (bufsize && worker_set_bufsize (crew->worker[i], bufsize) < 0)
            goto error;
    }
    /* Close stdin of current workers and allow them to restart on demand.
     * This forces them to re-acquire their configuration, if any.
     */
    workcrew_stop_notify (crew, NULL, NULL);
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, argv);
    ERRNO_SAFE_WRAP (free, argz);
    return rc;
}

struct workcrew *workcrew_create (flux_t *h)
{
    struct workcrew *crew;
    if (!(crew = calloc (1, sizeof (*crew))))
        return NULL;
    crew->h = h;
    return crew;
}

/* Select worker with least backlog.  If none is running, or the best
 * has a backlog at or beyond threshold, select a non-running worker which
 * will be started by worker_request().
 */
static struct worker *select_best_worker (struct workcrew *crew)
{
    struct worker *best = NULL;
    struct worker *idle = NULL;
    int i;

    for (i = 0; i < WORKCREW_SIZE; i++) {
        if (worker_is_running (crew->worker[i])) {
            if (!best || (worker_queue_depth (crew->worker[i])
                        < worker_queue_depth (best)))
                best = crew->worker[i];
        }
        else if (!idle)
            idle = crew->worker[i];
    }
    if (idle && (!best || worker_queue_depth (best) >= workcrew_max_backlog))
        best = idle;

    return best;
}

/* Re-encode job info in compact form to eliminate any white space (esp \n),
 * then pass it to least busy worker, returning a future.
 */
flux_future_t *workcrew_process_job (struct workcrew *crew, json_t *job)
{
    flux_future_t *f;
    char *s;
    struct worker *w;

    if (!(s = json_dumps (job, JSON_COMPACT))) {
        errno = ENOMEM;
        goto error;
    }
    w = select_best_worker (crew);
    assert (w != NULL);
    if (!(f = worker_request (w, s)))
        goto error;
    free (s);
    return f;
error:
    ERRNO_SAFE_WRAP (free, s);
    return NULL;
}

json_t *workcrew_stats_get (struct workcrew *crew)
{
    json_t *o = NULL;

    if (crew) {
        int running = 0;
        int requests = 0;
        int errors = 0;
        int backlog = 0;
        int trash = 0;
        json_t *pids = json_array ();

        for (int i = 0; i < WORKCREW_SIZE; i++) {
            running += worker_is_running (crew->worker[i]) ? 1 : 0;
            requests += worker_request_count (crew->worker[i]);
            errors += worker_error_count (crew->worker[i]);
            trash += worker_trash_count (crew->worker[i]);
            backlog += worker_queue_depth (crew->worker[i]);
            if (pids) {
                json_t *pid = json_integer (worker_pid (crew->worker[i]));
                if (json_array_append_new (pids, pid ? pid : json_null ()) < 0)
                    json_decref (pid);
            }
        }
        o = json_pack ("{s:i s:i s:i s:i s:i s:O}",
                       "running", running,
                       "requests", requests,
                       "errors", errors,
                       "trash", trash,
                       "backlog", backlog,
                       "pids", pids ? pids : json_null ());
        json_decref (pids);
    }
    return o ? o : json_null ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
