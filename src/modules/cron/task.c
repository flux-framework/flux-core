/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libsubprocess/zio.h"

#include "task.h"

struct cron_task {
    flux_t *              h;      /* flux handle used to create this task   */
    struct flux_match     match;  /* match object for message handler       */
    flux_msg_handler_t *  mh;     /* msg handler specific to this task      */

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
    unsigned int       running:1;
    unsigned int      timedout:1;
    unsigned int        exited:1;
    unsigned int stderr_closed:1;
    unsigned int stdout_closed:1;

    cron_task_io_f       io_cb;
    cron_task_state_f    state_cb;
    cron_task_state_f    timeout_cb;
    cron_task_complete_f completion_cb;
    void *               arg;
};


void cron_task_destroy (cron_task_t *t)
{
    flux_msg_handler_stop (t->mh);
    flux_msg_handler_destroy (t->mh);
    t->mh = NULL;
    flux_watcher_destroy (t->timeout_w);
    t->timeout_w = NULL;
    free (t->state);
    free (t);
}

cron_task_t *cron_task_new (flux_t *h, cron_task_complete_f cb, void *arg)
{
    cron_task_t *t = xzmalloc (sizeof (*t));
    memset (t, 0, sizeof (*t));
    t->h = h;
    t->match = FLUX_MATCH_RESPONSE;
    t->completion_cb = cb;
    t->arg = arg;
    t->state = xstrdup ("Initialized");
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

static bool cron_task_completed (cron_task_t *t)
{
    if (t->rexec_failed)
        return true;
    if (t->exited && t->stderr_closed && t->stdout_closed)
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
        t->state = xstrdup (fmt);
    va_end (ap);
}

static int io_handler (flux_t *h, cron_task_t *t,
    const char *json_str, json_object *resp)
{
    const char *stream = "stdout";
    void *data = NULL;
    bool eof;
    bool is_stderr = false;
    int len;

    if ((len = zio_json_decode (json_str, &data, &eof)) < 0) {
        flux_log_error (h, "io decode");
        free (data);
        return (-1);
    }
    (void) Jget_str (resp, "name", &stream);

    if (strcmp (stream, "stderr") == 0)
        is_stderr = true;

    if (t->io_cb)
        (*t->io_cb) (h, t, t->arg, is_stderr, data, len, eof);

    if (eof) {
        if (is_stderr)
            t->stderr_closed = 1;
        else
            t->stdout_closed = 1;
    }
    free (data);
    return (0);
}

static int state_handler (flux_t *h, cron_task_t *t, json_object *resp)
{
    const char *state;

    if (!Jget_str (resp, "state", &state)) {
        flux_log_error (h, "unable to get exec state");
        return -1;
    }
    cron_task_state_update (t, state);

    if (strcmp (state, "Running") == 0) {
        clock_gettime (CLOCK_REALTIME, &t->runningtime);
        t->running = 1;
        (void) Jget_int (resp, "pid", &t->pid);
        (void) Jget_int (resp, "rank", &t->rank);
    }
    else if (strcmp (state, "Exec Failure") == 0) {
        Jget_int (resp, "exec_errno", &t->exec_errno);
        t->exited = 1;
        t->stderr_closed = t->stdout_closed = 1;
        errno = t->exec_errno;
    }
    else if (strcmp (state, "Exited") == 0) {
        t->exited = 1;
        Jget_int (resp, "status", &t->status);
        if (WIFSIGNALED (t->status))
            cron_task_state_update (t, "%s", strsignal (WTERMSIG (t->status)));
        else if (WEXITSTATUS (t->status) != 0)
            cron_task_state_update (t, "Exit %d", WEXITSTATUS (t->status));
    }

    if (t->state_cb)
        (*t->state_cb) (h, t, t->arg);

    return (0);

}

static void cron_task_rexec_failed (cron_task_t *t, int errnum)
{
    t->rexec_failed = 1;
    t->rexec_errno = errno;
    cron_task_state_update (t, "Rexec Failure");
}

static void cron_task_handle_completion (cron_task_t *t)
{
    clock_gettime (CLOCK_REALTIME, &t->endtime);
    /*
     * Disable message handling for this task. Task will be destroyed
     *  later.
     */
    flux_msg_handler_stop (t->mh);
    flux_msg_handler_destroy (t->mh);
    t->mh = NULL;
    flux_watcher_destroy (t->timeout_w);
    t->timeout_w = NULL;

    /* Call completion handler for this entry */
    if (t->completion_cb)
        (*t->completion_cb) (t->h, t, t->arg);
}

static void exec_handler (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    struct cron_task *t = arg;
    const char *json_str;
    const char *topic;
    const char *type;
    json_object *resp = NULL;

    if ((flux_response_decode (msg, &topic, &json_str) < 0)
        || !(resp = Jfromstr (json_str))) {
        cron_task_rexec_failed (t, errno);
        flux_log_error (h, "cron_task: exec_handler");
    }
    else if (Jget_str (resp, "type", &type) && strcmp (type, "io") == 0) {
        if (io_handler (h, t, json_str, resp) < 0)
            goto out;
    }
    else if (state_handler (h, t, resp) < 0)
        goto out;

    if (cron_task_completed (t))
        cron_task_handle_completion (t);

out:
    if (resp)
        Jput (resp);
}

static flux_msg_t *kill_request_create (cron_task_t *t, int sig)
{
    int e = 0;
    flux_msg_t *msg;
    json_object *o = Jnew ();
    Jadd_int (o, "pid", t->pid);
    Jadd_int (o, "signum", sig);
    if (!(msg = flux_request_encode ("cmb.exec.signal", Jtostr (o)))
        || (flux_msg_set_nodeid (msg, t->rank, 0) < 0))
        e = errno;
    if (o)
        Jput (o);
    errno = e;
    return (msg);
}

int cron_task_kill (cron_task_t *t, int sig)
{
    flux_t *h = t->h;
    flux_msg_t *msg;

    if (!t->started || t->exited) {
        errno = EINVAL;
        return -1;
    }

    msg = kill_request_create (t, sig);
    if (!msg || flux_send (h, msg, 0) < 0)
        return (-1);
    return (0);
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

static json_object *exec_request_create (struct cron_task *t,
    const char *command,
    const char *cwd,
    char *const env[])
{
    json_object *o = Jnew ();
    json_object *cmd = Jnew_ar ();

    Jadd_ar_str (cmd, "sh");
    Jadd_ar_str (cmd, "-c");
    Jadd_ar_str (cmd, command);

    json_object_object_add (o, "cmdline", cmd);

    if (cwd)
        Jadd_str (o, "cwd", cwd);

    if (env) {
        json_object *enva = Jnew_ar ();
        const char *e = env[0];
        while (e != NULL)
            Jadd_ar_str (enva, e);
        json_object_object_add (o, "environ", enva);
    }
    return (o);
}

int cron_task_run (cron_task_t *t,
    int rank, const char *cmd, const char *cwd,
    char *const env[])
{
    flux_t *h = t->h;
    json_object *req = NULL;
    flux_msg_t *msg = NULL;
    int rc = -1;

    t->match.matchtag = flux_matchtag_alloc (h, FLUX_MATCHTAG_GROUP);
    if (t->match.matchtag == FLUX_MATCHTAG_NONE)
        return -1;
    t->match.topic_glob = "cmb.exec";
    t->mh = flux_msg_handler_create (h, t->match, exec_handler, t);
    if (!t->mh)
        return -1;

    if (!(req = exec_request_create (t, cmd, cwd, env)))
        goto done;
    if (!(msg = flux_request_encode ("cmb.exec", Jtostr (req))))
        goto done;
    if (flux_msg_set_nodeid (msg, rank, 0) < 0)
        goto done;
    if (flux_msg_set_matchtag (msg, t->match.matchtag) < 0)
        goto done;
    if ((rc = flux_send (h, msg, 0)) < 0) {
        flux_log_error (h, "cron_task_run: flux_send");
        goto done;
    }
    t->started = 1;
    clock_gettime (CLOCK_REALTIME, &t->starttime);
    cron_task_state_update (t, "Started");
    rc = 0;
    flux_msg_handler_start (t->mh);

    if (t->timeout >= 0.0)
        cron_task_timeout_start (t);
done:
    if (req)
        Jput (req);
    if (rc < 0) {
        t->rexec_errno = errno;
        cron_task_state_update (t, "Rexec Failure");
    }
    flux_msg_destroy (msg);
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

static double timespec_to_double (struct timespec *tm)
{
    double s = tm->tv_sec;
    double ns = tm->tv_nsec/1.0e9 + .5e-9; // round 1/2 epsilon
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

json_object *cron_task_to_json (struct cron_task *t)
{
    json_object *o = Jnew ();

    Jadd_int (o, "rank", t->rank);
    Jadd_int (o, "pid", t->pid);
    Jadd_int (o, "status", t->status);
    if (t->rexec_errno)
        Jadd_int (o, "rexec_errno", t->rexec_errno);
    if (t->exec_errno)
        Jadd_int (o, "exec_errno", t->exec_errno);
    if (t->timedout)
        Jadd_bool (o, "timedout", true);
    if (cron_task_completed (t)) {
        int code = 0;
        if (WIFEXITED (t->status))
            code = WEXITSTATUS (t->status);
        else if (WIFSIGNALED (t->status))
            code = 128 + WTERMSIG (t->status);
        Jadd_int (o, "code", code);
    }
    Jadd_str (o, "state", cron_task_state_string (t));
    Jadd_double (o, "create-time", timespec_to_double (&t->createtime));
    if (t->started)
        Jadd_double (o, "start-time", timespec_to_double (&t->starttime));
    if (t->running)
        Jadd_double (o, "running-time", timespec_to_double (&t->runningtime));
    if (cron_task_completed (t))
        Jadd_double (o, "end-time", timespec_to_double (&t->endtime));

    return (o);
}

/* vi: ts=4 sw=4 expandtab
 */

