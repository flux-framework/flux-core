/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/log.h"

#include "task.h"

struct cron_task {
    flux_t *              h;      /* flux handle used to create this task   */
    flux_subprocess_t    *p;      /* flux subprocess */

    int                   rank;   /* rank on which task is being run        */
    pid_t                 pid;    /* remote process id                      */
    char *                state;  /* state string returned by cmb.exec      */

    double               timeout;
    flux_watcher_t *   timeout_w;

    int                   status; /* exit status if state is Exited         */
    int              rexec_errno; /* any errno returned by rexec service    */
    int               exec_errno; /* any errno returned by remote exec(2)   */

    struct timespec   createtime; /* Time at which task was created         */
    struct timespec    starttime; /* Time at which exec request was sent    */
    struct timespec  runningtime; /* Time at which task state was Running   */
    struct timespec      endtime; /* Time at which task exited/failed       */

    unsigned int       started:1;
    unsigned int  rexec_failed:1;
    unsigned int   exec_failed:1;
    unsigned int       running:1;
    unsigned int      timedout:1;
    unsigned int        exited:1;
    unsigned int     completed:1;

    cron_task_io_f       io_cb;
    cron_task_state_f    state_cb;
    cron_task_state_f    timeout_cb;
    cron_task_finished_f finished_cb;
    void *               arg;
};


void cron_task_destroy (cron_task_t *t)
{
    flux_subprocess_destroy (t->p);
    flux_watcher_destroy (t->timeout_w);
    t->timeout_w = NULL;
    free (t->state);
    free (t);
}

cron_task_t *cron_task_new (flux_t *h, cron_task_finished_f cb, void *arg)
{
    cron_task_t *t = calloc (1, sizeof (*t));
    if (t == NULL)
        return NULL;
    if ((t->state = strdup ("Initialized")) == NULL) {
        free (t);
        return NULL;
    }
    t->h = h;
    t->finished_cb = cb;
    t->arg = arg;
    clock_gettime (CLOCK_REALTIME, &t->createtime);
    t->timeout = 0.0;
    return (t);
}

void cron_task_on_io (cron_task_t *t, cron_task_io_f cb)
{
    t->io_cb = cb;
}

void cron_task_on_state_change (cron_task_t *t, cron_task_state_f cb)
{
    t->state_cb = cb;
}

static bool cron_task_finished (cron_task_t *t)
{
    if (t->rexec_failed)
        return true;
    if (t->exec_failed)
        return true;
    if (t->completed)
        return true;
    return false;
}

static void cron_task_state_update (cron_task_t *t, const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start (ap, fmt);
    free (t->state);
    if ((rc = vasprintf (&t->state, fmt, ap) < 0))
        t->state = strdup (fmt);
    va_end (ap);
}

static void timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    cron_task_t *t = arg;
    t->timedout = 1;
    if (t->timeout_cb)
        t->timeout_cb (t->h, t, t->arg);
    else
        cron_task_kill (t, SIGTERM);
}

static void cron_task_timeout_start (cron_task_t *t)
{
    flux_watcher_t *w;
    flux_reactor_t *r;
    if (t->timeout <= 0.0)
        return;
    r = flux_get_reactor (t->h);
    if (!(w = flux_timer_watcher_create (r, t->timeout, 0.0, timeout_cb, t))) {
        flux_log_error (t->h, "task_timeout_start");
        return;
    }
    flux_watcher_start (w);
    t->timeout_w = w;
}

void cron_task_set_timeout (cron_task_t *t, double to, cron_task_state_f cb)
{
    t->timeout_cb = cb;
    t->timeout = to;
    if (t->started)
        cron_task_timeout_start (t);
}

static void cron_task_exec_failed (cron_task_t *t, int errnum)
{
    t->exec_failed = 1;
    t->exec_errno = errnum;
    cron_task_state_update (t, "Exec Failure");
}

static void cron_task_rexec_failed (cron_task_t *t, int errnum)
{
    t->rexec_failed = 1;
    t->rexec_errno = errnum;
    cron_task_state_update (t, "Rexec Failure");
}

static void cron_task_handle_finished (flux_subprocess_t *p, cron_task_t *t)
{
    clock_gettime (CLOCK_REALTIME, &t->endtime);
    flux_watcher_destroy (t->timeout_w);
    t->timeout_w = NULL;
    flux_subprocess_destroy (t->p);
    t->p = NULL;

    /* Call finished handler for this entry */
    if (t->finished_cb)
        (*t->finished_cb) (t->h, t, t->arg);
}

static void completion_cb (flux_subprocess_t *p)
{
    cron_task_t *t = flux_subprocess_aux_get (p, "task");

    assert (t);

    t->completed = 1;
    cron_task_handle_finished (p, t);
}

static void state_change_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    cron_task_t *t = flux_subprocess_aux_get (p, "task");

    assert (t);

    cron_task_state_update (t, flux_subprocess_state_string (state));

    if (state == FLUX_SUBPROCESS_STARTED) {
        t->started = 1;
        clock_gettime (CLOCK_REALTIME, &t->starttime);
        if (t->timeout >= 0.0)
            cron_task_timeout_start (t);
    }
    else if (state == FLUX_SUBPROCESS_RUNNING) {
        clock_gettime (CLOCK_REALTIME, &t->runningtime);
        t->running = 1;
        t->pid = flux_subprocess_pid (p);
        t->rank = flux_subprocess_rank (p);
    }
    else if (state == FLUX_SUBPROCESS_EXEC_FAILED) {
        cron_task_exec_failed (t, flux_subprocess_fail_errno (p));
        cron_task_handle_finished (p, t);
        errno = t->exec_errno;
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        cron_task_rexec_failed (t, flux_subprocess_fail_errno (p));
        cron_task_handle_finished (p, t);
        errno = t->rexec_errno;
    }
    else if (state == FLUX_SUBPROCESS_EXITED) {
        t->exited = 1;
        t->status = flux_subprocess_status (p);
        if (WIFSIGNALED (t->status))
            cron_task_state_update (t, "%s", strsignal (WTERMSIG (t->status)));
        else if (WEXITSTATUS (t->status) != 0)
            cron_task_state_update (t, "Exit %d", WEXITSTATUS (t->status));
    }

    if (t->state_cb)
        (*t->state_cb) (t->h, t, t->arg);
}

static void io_cb (flux_subprocess_t *p, const char *stream)
{
    cron_task_t *t = flux_subprocess_aux_get (p, "task");
    const char *ptr = NULL;
    int lenp;
    bool is_stderr = false;

    assert (t);

    if (!strcmp (stream, "STDERR"))
        is_stderr = true;

    if (!(ptr = flux_subprocess_read_trimmed_line (p, stream, &lenp))) {
        flux_log_error (t->h, "%s: flux_subprocess_read_trimmed_line",
                        __FUNCTION__);
        return;
    }

    if (!lenp) {
        if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
            flux_log_error (t->h, "%s: flux_subprocess_read",
                            __FUNCTION__);
            return;
        }
    }

    if (t->io_cb && lenp)
        (*t->io_cb) (t->h, t, t->arg, is_stderr, ptr, lenp);
}

int cron_task_kill (cron_task_t *t, int sig)
{
    flux_t *h = t->h;
    flux_future_t *f;

    if (!t->running || t->exited) {
        errno = EINVAL;
        return -1;
    }

    if (!(f = flux_subprocess_kill (t->p, sig))) {
        flux_log_error (h, "%s: flux_subprocess_kill", __FUNCTION__);
        return (-1);
    }
    /* ignore response */
    flux_future_destroy (f);
    return (0);
}


static flux_cmd_t *exec_cmd_create (struct cron_task *t,
    const char *command,
    const char *cwd,
    json_t *env)
{
    flux_cmd_t *cmd = NULL;
    char *tmp_cwd = NULL;

    if (!(cmd = flux_cmd_create (0, NULL, NULL))) {
        flux_log_error (t->h, "exec_cmd_create: flux_cmd_create");
        goto error;
    }
    if (flux_cmd_argv_append (cmd, "%s", "sh") < 0
        || flux_cmd_argv_append (cmd, "%s", "-c") < 0
        || flux_cmd_argv_append (cmd, "%s", command) < 0) {
        flux_log_error (t->h, "exec_cmd_create: flux_cmd_argv_append");
        goto error;
    }
    if (flux_cmd_setcwd (cmd, cwd) < 0) {
        flux_log_error (t->h, "exec_cmd_create: flux_cmd_setcwd");
        goto error;
    }
    if (env) {
        /* obj is a JSON object */
        const char *key;
        json_t *value;

        json_object_foreach (env, key, value) {
            const char *value_str = json_string_value (value);
            if (!value_str) {
                flux_log_error (t->h, "exec_cmd_create: json_string_value");
                errno = EPROTO;
                goto error;
            }
            if (flux_cmd_setenvf (cmd, 1, key, "%s", value_str) < 0) {
                flux_log_error (t->h, "exec_cmd_create: flux_cmd_setenvf");
                goto error;
            }
        }
    }

    free (tmp_cwd);
    return (cmd);
 error:
    free (tmp_cwd);
    flux_cmd_destroy (cmd);
    return (NULL);
}

int cron_task_run (cron_task_t *t,
    int rank, const char *command, const char *cwd,
    json_t *env)
{
    flux_t *h = t->h;
    flux_subprocess_t *p = NULL;
    flux_cmd_t *cmd;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_change_cb,
        .on_channel_out = NULL,
        .on_stdout = io_cb,
        .on_stderr = io_cb
    };
    int rc = -1;

    if (!(cmd = exec_cmd_create (t, command, cwd, env)))
        goto done;

    if (!(p = flux_rexec (h, rank, 0, cmd, &ops))) {
        cron_task_exec_failed (t, errno);
        goto done;
    }

    if (flux_subprocess_aux_set (p, "task", t, NULL) < 0) {
        flux_log_error (h, "flux_subprocess_aux_set");
        goto done;
    }

    t->p = p;
    rc = 0;
done:
    if (rc < 0) {
        t->rexec_errno = errno;
        cron_task_state_update (t, "Rexec Failure");
        flux_subprocess_destroy (p);
    }
    flux_cmd_destroy (cmd);
    return (rc);
}

const char *cron_task_state (cron_task_t *t)
{
    return (t->state);
}

int cron_task_status (cron_task_t *t)
{
    return (t->status);
}

static double round_timespec_to_double (struct timespec *tm)
{
    double s = tm->tv_sec;
    /*  Add .5ns (1/2 the minumim possible value change) to avoid
     *   underflow which represents something like .5 as .499999...
     *   (we don't care about overflow since we'll truncate fractional
     *    part to 9 significant digits *at the most* anyway)
     */
    double ns = tm->tv_nsec/1.0e9 + .5e-9;
    return (s + ns);
}

/*
 *  Enhance t->state with more information in specific cases:
 */
static const char * cron_task_state_string (cron_task_t *t)
{
    if (t->rexec_errno)
        return ("Rexec Failure");
    if (t->exec_errno)
        return ("Exec Failure");
    if (!t->started)
        return ("Deferred");
    if (!t->exited)
        return ("Running");
    if (t->timedout)
        return ("Timeout");
    if (t->status != 0)
        return ("Failed");
    return ("Exited");
}

/*
 *  Encode timespec as a floating point JSON representation in object `o`
 *   under key `name`.
 */
static int add_timespec (json_t *o, const char *name, struct timespec *tm)
{
    json_t *n = json_real (round_timespec_to_double (tm));
    if (n == NULL)
        return -1;
    return json_object_set_new (o, name, n);
}

json_t *cron_task_to_json (struct cron_task *t)
{
    json_t *o = json_pack ("{ s:i, s:i, s:i, s:s }",
                           "rank", t->rank,
                           "pid",  t->pid,
                           "status", t->status,
                           "state", cron_task_state_string (t));

    if (o == NULL)
        return NULL;

    if (add_timespec (o, "create-time", &t->createtime) < 0)
        goto fail;

    if (t->rexec_errno)
        json_object_set_new (o, "rexec_errno", json_integer (t->rexec_errno));
    if (t->exec_errno)
        json_object_set_new (o, "exec_errno", json_integer (t->exec_errno));
    if (t->timedout)
        json_object_set_new (o, "timedout", json_boolean (true));
    if (cron_task_finished (t)) {
        int code = 0;
        if (WIFEXITED (t->status))
            code = WEXITSTATUS (t->status);
        else if (WIFSIGNALED (t->status))
            code = 128 + WTERMSIG (t->status);
        json_object_set_new (o, "code", json_integer (code));
    }
    if (t->started && add_timespec (o, "start-time", &t->starttime) < 0)
        goto fail;
    if (t->running && add_timespec (o, "running-time", &t->runningtime) < 0)
        goto fail;
    if (cron_task_finished (t)
        && add_timespec (o, "end-time", &t->endtime) < 0)
        goto fail;

    return (o);
fail:
    json_decref (o);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */

