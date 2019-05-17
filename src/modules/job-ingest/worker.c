/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* worker - spawn subprocess filter to outsource work
 *
 * Start a coprocess that reads work from stdin (one line at a time),
 * then emits a one-line JSON result on stdout.  Stderr is logged.
 *
 * Each line of INPUT is a free form string with no embedded newlines.
 *
 * Each line of OUTPUT is an encoded JSON object with no embedded newlines.
 * Failure is indicated by errnum != 0 and optional error string:
 *  {"errnum":i ?"errstr":s}
 * Success is indicated by errnum = 0 and optional data object:
 *  {"errnum:0, ?"data":o}.
 *
 * Work is requested by calling worker_request() with an input string.
 * A future is returned that is fulfilled when a result is received.
 *
 * Work may be submitted even when the worker is busy.  The worker emits
 * work results in the order received.  Internally, the worker maintains
 * a queue of futures, and each time a result is received, the future at
 * the head of queue is fulfilled.
 *
 * The broker exec service is used to spawn workers on the local rank,
 * using the libsubprocess API.
 *
 * Caveats:
 * - Work is sent to the coprocess with flux_subprocess_write() regardless
 *   of the current queue depth, which may challenge subprocess buffer
 *   management.
 *
 * - Worker is stopped by closing STDIN, but worker_stop() may return
 *   before it is known to have stopped, leaving the broker to clean up
 *   with no possibility of reporting errors to the caller.
 *
 * - Subprocess state change and completion callbacks just log at this point,
 *   and don't initiate cleanup should the subprocess prematurely exit.
 */

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <unistd.h>
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>
#include "worker.h"

const char *worker_auxkey = "flux::worker";

struct worker {
    flux_t *h;
    char *name;
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    zlist_t *queue;  // queue of futures (head is currently running)
    flux_watcher_t *timer;
    double inactivity_timeout;
    zlist_t *trash;
};

static int worker_start (struct worker *w);
static void worker_stop (struct worker *w);

/* Subprocess completed.
 * Destroy the subprocess, but don't use w->p since that may be a diferent
 * one, if worker_stop() was followed immediately by worker_start().
 * Remove from w->trash to avoid double-free in worker_destroy()
 */
static void worker_completion_cb (flux_subprocess_t *p)
{
    struct worker *w = flux_subprocess_aux_get (p, worker_auxkey);
    int rc;

    if ((rc = flux_subprocess_exit_code (p)) >= 0) {
        if (rc == 0)
            flux_log (w->h, LOG_DEBUG, "%s: exited normally", w->name);
        else
            flux_log (w->h, LOG_ERR, "%s: exited with rc=%d", w->name, rc);
    } else if ((rc = flux_subprocess_signaled (p)) >= 0)
        flux_log (w->h, LOG_ERR, "%s: killed by %s", w->name, strsignal (rc));
    else
        flux_log (w->h, LOG_ERR, "%s: completed (not signal or exit)", w->name);
    flux_subprocess_destroy (p);
    zlist_remove (w->trash, p);
}

/* Subprocess state change.
 */
static void worker_state_cb (flux_subprocess_t *p,
                             flux_subprocess_state_t state)
{
    struct worker *w = flux_subprocess_aux_get (p, worker_auxkey);

    switch (state) {
    case FLUX_SUBPROCESS_RUNNING:
        flux_log (w->h,
                  LOG_DEBUG,
                  "%s: running (pid=%d)",
                  w->name,
                  (int)flux_subprocess_pid (p));
        break;
    case FLUX_SUBPROCESS_EXEC_FAILED:
    case FLUX_SUBPROCESS_FAILED:
        flux_log (w->h,
                  LOG_ERR,
                  "%s: %s",
                  w->name,
                  flux_subprocess_state_string (state));
        break;
    case FLUX_SUBPROCESS_STARTED:
    case FLUX_SUBPROCESS_EXITED:
    case FLUX_SUBPROCESS_INIT:
        break;  // ignore
    }
}

static void worker_timeout (flux_reactor_t *r,
                            flux_watcher_t *timer,
                            int revents,
                            void *arg)
{
    struct worker *w = arg;
    flux_log (w->h, LOG_DEBUG, "%s: inactivity timeout", w->name);
    worker_stop (w);
}

/* Worker queue is empty - start inactivity timer.
 */
static void worker_inactive (struct worker *w)
{
    flux_timer_watcher_reset (w->timer, w->inactivity_timeout, 0.);
    flux_watcher_start (w->timer);
}

/* Worker queue is no longer empty - stop inactivity timer/start worker
 */
static void worker_active (struct worker *w)
{
    flux_watcher_stop (w->timer);
    if (worker_start (w) < 0)
        flux_log_error (w->h, "%s: worker_start", w->name);
}

/* Fulfill future 'f' with result 's'.
 * Ensure that any errors in parsing 's' are passed on to 'f' as well.
 */
static void worker_fulfill_future (struct worker *w,
                                   flux_future_t *f,
                                   const char *s)
{
    json_t *o;
    int errnum;
    const char *errstr = NULL;  // optional
    json_t *data = NULL;        // optional
    char *s_data = NULL;

    if (!(o = json_loads (s, 0, NULL))) {
        flux_log (w->h, LOG_ERR, "%s: json_loads '%s' failed", w->name, s);
        errnum = EINVAL;
        goto error;
    }
    if (json_unpack (o,
                     "{s:i s?:s s?:o}",
                     "errnum",
                     &errnum,
                     "errstr",
                     &errstr,
                     "data",
                     &data)
        < 0) {
        flux_log (w->h, LOG_ERR, "%s: json_unpack '%s' failed", w->name, s);
        errnum = EINVAL;
        goto error;
    }
    if (errnum != 0)
        goto error;
    if (data) {
        if (!(s_data = json_dumps (data, JSON_COMPACT))) {
            flux_log (w->h, LOG_ERR, "%s: json_dumps result failed", w->name);
            errnum = EINVAL;
            goto error;
        }
    }
    flux_future_fulfill (f, s_data, (flux_free_f)free);
    json_decref (o);
    return;
error:
    flux_future_fulfill_error (f, errnum, errstr);
    json_decref (o);
}

/* Subprocess output available
 * STDERR is logged
 * STDOUT fulfills future at the top of the worker's queue.
 */
static void worker_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct worker *w = flux_subprocess_aux_get (p, worker_auxkey);
    const char *s;
    int len;

    if (!(s = flux_subprocess_read_trimmed_line (p, stream, &len))) {
        flux_log_error (w->h, "%s: subprocess_read_trimmed_line", w->name);
        return;
    }
    if (len == 0)  // EOF
        return;
    if (!strcmp (stream, "STDOUT")) {
        flux_future_t *f;

        if (!(f = zlist_pop (w->queue))) {
            flux_log (w->h,
                      LOG_ERR,
                      "%s: dropping orphan response: '%s'",
                      w->name,
                      s);
            return;
        }
        worker_fulfill_future (w, f, s);
        flux_future_decref (f);
        if (zlist_size (w->queue) == 0)
            worker_inactive (w);
    } else if (!strcmp (stream, "STDERR")) {
        flux_log (w->h, LOG_DEBUG, "%s: %s", w->name, s ? s : "");
    }
}

flux_subprocess_ops_t worker_ops = {
    .on_completion = worker_completion_cb,
    .on_state_change = worker_state_cb,
    .on_channel_out = NULL,
    .on_stdout = worker_output_cb,
    .on_stderr = worker_output_cb,
};

flux_future_t *worker_request (struct worker *w, const char *s)
{
    int bufsz = strlen (s) + 1;
    char *buf;
    flux_future_t *f;
    int saved_errno;

    if (strchr (s, '\n')) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    flux_future_set_flux (f, w->h);
    if (!(buf = malloc (bufsz)))
        goto error;
    memcpy (buf, s, bufsz - 1);
    buf[bufsz - 1] = '\n';
    worker_active (w);
    if (flux_subprocess_write (w->p, "STDIN", buf, bufsz) != bufsz)
        goto error;
    if (zlist_append (w->queue, f) < 0)
        goto error;
    flux_future_incref (f);  // queue takes a reference on the future
    free (buf);
    return f;
error:
    saved_errno = errno;
    free (buf);
    flux_future_destroy (f);
    errno = saved_errno;
    return NULL;
}

/* Stop a worker by closing its STDIN.
 * This should cause it to exit, then worker_completion_cb() will destroy it.
 * Just in case we have to destroy the worker before then, add it to w->trash.
 */
static void worker_stop (struct worker *w)
{
    if (w->p) {
        int saved_errno = errno;
        if (flux_subprocess_close (w->p, "STDIN") < 0) {
            flux_log_error (w->h, "%s: flux_subprocess_close", w->name);
            return;
        }
        (void)zlist_append (w->trash, w->p);
        w->p = NULL;
        errno = saved_errno;
    }
}

static int worker_start (struct worker *w)
{
    if (!w->p) {
        if (!(w->p =
                  flux_rexec (w->h, FLUX_NODEID_ANY, 0, w->cmd, &worker_ops))) {
            return -1;
        }
        if (flux_subprocess_aux_set (w->p, worker_auxkey, w, NULL) < 0) {
            worker_stop (w);
            return -1;
        }
    }
    return 0;
}

int worker_queue_depth (struct worker *w)
{
    return zlist_size (w->queue);
}

bool worker_is_running (struct worker *w)
{
    return (w->p ? true : false);
}

void worker_destroy (struct worker *w)
{
    if (w) {
        int saved_errno = errno;
        flux_subprocess_t *p;
        flux_future_t *f;

        worker_stop (w);  // puts w->p in w->trash
        flux_cmd_destroy (w->cmd);
        while ((f = zlist_pop (w->queue)))
            flux_future_decref (f);
        zlist_destroy (&w->queue);
        while ((p = zlist_pop (w->trash)))
            flux_subprocess_destroy (p);
        zlist_destroy (&w->trash);
        flux_watcher_destroy (w->timer);
        free (w->name);
        free (w);
        errno = saved_errno;
    }
}

struct worker *worker_create (flux_t *h,
                              double inactivity_timeout,
                              int argc,
                              char **argv)
{
    struct worker *w;
    char path[PATH_MAX + 1];
    flux_reactor_t *r = flux_get_reactor (h);

    if (!(w = calloc (1, sizeof (*w))))
        return NULL;
    w->h = h;
    w->inactivity_timeout = inactivity_timeout;
    if (!(w->timer = flux_timer_watcher_create (r,
                                                inactivity_timeout,
                                                0.,
                                                worker_timeout,
                                                w)))
        goto error;
    if (!(w->trash = zlist_new ()))
        goto error;
    if (!(w->name = strdup (basename (argv[0]))))
        goto error;
    if (!(w->queue = zlist_new ()))
        goto error;
    if (!(w->cmd = flux_cmd_create (argc, argv, environ))) {
        flux_log_error (h, "flux_cmd_create");
        goto error;
    }
    if (flux_cmd_setcwd (w->cmd, getcwd (path, sizeof (path))) < 0) {
        flux_log_error (h, "flux_cmd_setcwd");
        goto error;
    }
    return w;
error:
    worker_destroy (w);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
