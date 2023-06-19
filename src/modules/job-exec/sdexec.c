/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux job systemd exec implementation
 *
 * DESCRIPTION
 *
 * This exec module runs job-shells under systemd using the libsdprocess
 * library.
 *
 * CONFIGURATION
 *
 * To enable use of this exec implementation, it must be enabled via
 * the following flux configuration:
 *
 * [exec]
 * method = systemd
 *
 * The following configurations are supported under
 * attributes.system.exec.sd in the jobspec.
 *
 * "test":b - use sdexec for this specific job (used for testing)
 *
 * "test_exec_fail":b - assume sdprocess_exec() failed (used for testing)
 *
 * "stdoutlog":s - send stdout to {"eventlog","systemd"}
 *
 * "stderrlog":s - send stderr to {"eventlog","systemd"}
 *
 * "no_cleanup":b - do not cleanup systemd after running process
 *                  (useful for debugging)
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libsdprocess/sdprocess.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/command.h"
#include "job-exec.h"
#include "exec_config.h"

enum {
    SDEXEC_LOG_EVENTLOG,
    SDEXEC_LOG_SYSTEMD,
};

struct sdexec
{
    flux_t *h;
    struct jobinfo *job;
    flux_cmd_t *cmd;

    int start_errno;
    int test_exec_fail;
    int stdoutlog;
    int stderrlog;
    int no_cleanup;

    sdprocess_t *sdp;
    int stdin_fds[2];
    int stdout_fds[2];
    int stderr_fds[2];
    flux_watcher_t *w_stdout;
    flux_watcher_t *w_stderr;

    bool jobinfo_tasks_complete_called;
};

static void sdexec_destroy (void *data)
{
    struct sdexec *se = data;
    if (se) {
        if (se->sdp) {
            if (!se->no_cleanup) {
                if (sdprocess_systemd_cleanup (se->sdp) < 0)
                    flux_log_error (se->h, "sdprocess_systemd_cleanup");
            }
            sdprocess_destroy (se->sdp);
        }
        flux_cmd_destroy (se->cmd);
        close (se->stdin_fds[0]);
        close (se->stdin_fds[1]);
        close (se->stdout_fds[0]);
        close (se->stdout_fds[1]);
        close (se->stderr_fds[0]);
        close (se->stderr_fds[1]);
        if (se->w_stdout) {
            flux_watcher_stop (se->w_stdout);
            flux_watcher_destroy (se->w_stdout);
        }
        if (se->w_stderr) {
            flux_watcher_stop (se->w_stderr);
            flux_watcher_destroy (se->w_stderr);
        }
        free (se);
    }
}

/* systemd can break with some environments that export weird
 * environment variables, such as
 *
 * BASH_FUNC_ml%%=() {  eval $($LMOD_DIR/ml_cmd "$@") }
 *
 * So we manually add the common ones we are confident flux needs.
 */

static int add_env (struct sdexec *se, const char *var)
{
    char *val;
    assert (var);
    if ((val = getenv (var))) {
        if (flux_cmd_setenvf (se->cmd,
                              1,
                              var,
                              "%s",
                              val) < 0) {
            flux_log_error (se->h, "flux_cmd_setenvf");
            return -1;
        }
    }
    return 0;
}

static int add_flux_env (struct sdexec *se)
{
    if (add_env (se, "PATH") < 0)
        return -1;
    if (add_env (se, "PYTHONPATH") < 0)
        return -1;
    if (add_env (se, "MANPATH") < 0)
        return -1;
    if (add_env (se, "LUA_PATH") < 0)
        return -1;
    if (add_env (se, "LUA_CPATH") < 0)
        return -1;
    if (add_env (se, "FLUX_CONNECTOR_PATH") < 0)
        return -1;
    if (add_env (se, "FLUX_EXEC_PATH") < 0)
        return -1;
    if (add_env (se, "FLUX_MODULE_PATH") < 0)
        return -1;
    if (add_env (se, "FLUX_PMI_LIBRARY_PATH") < 0)
        return -1;
    return 0;
}

static void set_stdlog (struct sdexec *se,
                        const char *logstr,
                        int *logp,
                        const char *var)
{
    if (logstr) {
        assert (logp);
        if (!strcasecmp (logstr, "eventlog"))
            (*logp) = SDEXEC_LOG_EVENTLOG;
        else if (!strcasecmp (logstr, "systemd"))
            (*logp) = SDEXEC_LOG_SYSTEMD;
        else {
            (*logp) = SDEXEC_LOG_EVENTLOG;
            flux_log (se->h, LOG_ERR,
                      "invalid %s value '%s', defaulting to eventlog",
                      var,
                      logstr);
        }
    }
}

static struct sdexec *sdexec_create (flux_t *h,
                                     struct jobinfo *job,
                                     const char *job_shell)
{
    struct sdexec *se = NULL;
    const char *local_uri;
    const char *stdoutlog = NULL;
    const char *stderrlog = NULL;

    if (!(se = calloc (1, sizeof (*se))))
        return NULL;
    se->h = h;
    se->job = job;

    if (!(se->cmd = flux_cmd_create (0, NULL, NULL))) {
        flux_log_error (job->h, "flux_cmd_create");
        goto cleanup;
    }

    if (job->multiuser) {
        if (flux_cmd_argv_append (se->cmd, config_get_imp_path ()) < 0
            || flux_cmd_argv_append (se->cmd, "exec") < 0) {
            flux_log_error (job->h, "flux_cmd_argv_append");
            goto cleanup;
        }
        if (flux_cmd_setenvf (se->cmd,
                              1,
                              "FLUX_IMP_EXEC_HELPER",
                              "flux imp-exec-helper %ju",
                              (uintmax_t) job->id) < 0) {
            flux_log_error (job->h, "flux_cmd_setenvf");
            goto cleanup;
        }
    }

    if (flux_cmd_argv_append (se->cmd, job_shell) < 0
        || flux_cmd_argv_append (se->cmd, "--reconnect") < 0
        || flux_cmd_argv_appendf (se->cmd, "%ju", (uintmax_t) job->id) < 0) {
        flux_log_error (job->h, "flux_cmd_argv_append");
        goto cleanup;
    }

    if (add_flux_env (se) < 0)
        goto cleanup;

    /* XXX
     *
     * need to add if not in environment?
     *
     * XDG_RUNTIME_DIR=/run/user/8556
     * DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/8556/bus
     *
     */

    if (flux_cmd_setenvf (se->cmd,
                          1,
                          "FLUX_KVS_NAMESPACE",
                          "%s",
                          job->ns) < 0) {
        flux_log_error (job->h, "flux_cmd_setenvf");
        goto cleanup;
    }

    if (!(local_uri = flux_attr_get (se->h, "local-uri"))) {
        flux_log_error (job->h, "flux_cmd_setenvf");
        goto cleanup;
    }

    if (flux_cmd_setenvf (se->cmd, 1, "FLUX_URI", "%s", local_uri) < 0) {
        flux_log_error (job->h, "flux_cmd_setenvf");
        goto cleanup;
    }

    se->stdoutlog = SDEXEC_LOG_EVENTLOG;
    se->stderrlog = SDEXEC_LOG_EVENTLOG;

    (void) json_unpack_ex (job->jobspec, NULL, 0,
                           "{s:{s?{s?{s?{s?b s?s s?s s?b}}}}}",
                           "attributes", "system", "exec", "sd",
                           "test_exec_fail", &se->test_exec_fail,
                           "stdoutlog", &stdoutlog,
                           "stderrlog", &stderrlog,
                           "no_cleanup", &se->no_cleanup);

    set_stdlog (se, stdoutlog, &se->stdoutlog, "stdout");
    set_stdlog (se, stderrlog, &se->stderrlog, "stderr");

    se->stdin_fds[0] = -1;
    se->stdin_fds[1] = -1;
    se->stdout_fds[0] = -1;
    se->stdout_fds[1] = -1;
    se->stderr_fds[0] = -1;
    se->stderr_fds[1] = -1;

    return se;
cleanup:
    sdexec_destroy (se);
    return NULL;
}

static int sdexec_init (struct jobinfo *job)
{
    flux_t *h = job->h;
    flux_error_t err;
    struct sdexec *se = NULL;
    int enable = 0;

    (void)json_unpack_ex (job->jobspec, NULL, 0,
                          "{s:{s:{s:{s:{s:b}}}}}",
                          "attributes", "system", "exec",
                          "sd", "test", &enable);

    if (!enable) {
        const char *method = NULL;
        if (flux_conf_unpack (flux_get_conf (h),
                              &err,
                              "{s?:{s?s}}",
                              "exec",
                                "method", &method) < 0) {
            flux_log (h, LOG_ERR,
                      "error reading job-exec config: %s",
                      err.text);
            return 0;
        }

        if (method && strcasecmp (method, "systemd") == 0)
            enable = 1;
    }

    if (!enable)
        return 0;

    if (job->multiuser && !config_get_imp_path ()) {
        flux_log (job->h,
                  LOG_ERR,
                  "unable run multiuser job with no IMP configured!");
        return -1;
    }

    if (!(se = sdexec_create (h, job, config_get_job_shell (job))))
        return -1;

    job->data = se;
    return 1;
}

static void output_stream (struct sdexec *se,
                           int fd,
                           const char *stream)
{
    const char *cmd = flux_cmd_arg (se->cmd, 0);
    char buf[1024];
    int len;
    if ((len = read (fd, buf, 1024)) < 0) {
        if (errno != EWOULDBLOCK)
            jobinfo_fatal_error (se->job, errno, "read");
    }
    else if (len > 0)
        jobinfo_log_output (se->job,
                            0,
                            basename (cmd),
                            stream,
                            buf,
                            len);
}

static void state_cb (sdprocess_t *sdp, sdprocess_state_t state, void *arg)
{
    struct sdexec *se = arg;

    /* Under libsdprocess, it is conceivable a process starts and
     * exits before the state watcher is setup.  This is unlikely
     * except when failure is immediate (e.g. invalid command
     * specified, or bad job shell path).  So we must call
     * jobinfo_started() regardless if the state passed into here is
     * ACTIVE or EXITED.
     */
    if (!se->job->reattach && !se->job->running)
        jobinfo_started (se->job);

    if (state == SDPROCESS_EXITED
             && !se->jobinfo_tasks_complete_called) {

        /* Since we are calling jobinfo_tasks_complete(), the
         * stdout/stderr fd watchers may be stopped.  So if there is
         * any lingering data on the stdout/stderr streams, they may
         * not be output.  This is possible because the reactor may
         * call this state callback before stdio callbacks below.
         *
         * Note that fds may be uninitialized (still set to -1) if
         * flux was restarted and this job was reattached.
         */

        if (se->stdoutlog == SDEXEC_LOG_EVENTLOG
            && se->stdout_fds[0] >= 0)
            output_stream (se, se->stdout_fds[0], "stdout");
        if (se->stderrlog == SDEXEC_LOG_EVENTLOG
            && se->stderr_fds[0] >= 0)
            output_stream (se, se->stderr_fds[0], "stderr");

        jobinfo_tasks_complete (se->job,
                                resource_set_ranks (se->job->R),
                                sdprocess_wait_status (se->sdp));
        se->jobinfo_tasks_complete_called = true;
    }
}

static void output_cb (struct sdexec *se,
                       int revents,
                       int fd,
                       const char *stream)
{
    if (revents & FLUX_POLLIN)
        output_stream (se, fd, stream);
}

static void stdout_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct sdexec *se = arg;
    output_cb (se, revents, se->stdout_fds[0], "stdout");
}

static void stderr_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct sdexec *se = arg;
    output_cb (se, revents, se->stderr_fds[0], "stderr");
}

static int sdexec_reattach (struct sdexec *se,
                            struct jobinfo *job,
                            const char *unitname)
{
    int rv = -1;

    if (!(se->sdp = sdprocess_find_unit (se->h, unitname))) {
        se->start_errno = errno;
        jobinfo_fatal_error (job, errno, "sdprocess_find_unit");
        goto cleanup;
    }

    if (sdprocess_state (se->sdp, state_cb, se) < 0) {
        jobinfo_fatal_error (job, errno, "sdprocess_state");
        goto cleanup;
    }

    jobinfo_reattached (job);
    rv = 0;
cleanup:
    return rv;
}

static int sdexec_launch (struct sdexec *se,
                          struct jobinfo *job,
                          const char *unitname)
{
    char **cmdv = NULL;
    char **envv = NULL;
    int rv = -1;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, se->stdin_fds) < 0) {
        jobinfo_fatal_error (job, errno, "socketpair");
        goto cleanup;
    }

    if (se->stdoutlog == SDEXEC_LOG_EVENTLOG) {
        if (socketpair (PF_LOCAL,
                        SOCK_STREAM | SOCK_NONBLOCK,
                        0,
                        se->stdout_fds) < 0) {
            jobinfo_fatal_error (job, errno, "socketpair");
            goto cleanup;
        }
    }
    if (se->stderrlog == SDEXEC_LOG_EVENTLOG) {
        if (socketpair (PF_LOCAL,
                        SOCK_STREAM | SOCK_NONBLOCK,
                        0,
                        se->stderr_fds) < 0) {
            jobinfo_fatal_error (job, errno, "socketpair");
            goto cleanup;
        }
    }

    cmdv = cmd_argv_expand (se->cmd);
    envv = cmd_env_expand (se->cmd);

    if (se->test_exec_fail) {
        /* we select a somewhat random errno for this test exec fail */
        errno = EPERM;
        jobinfo_fatal_error (job, errno, "test sdprocess_exec");
        goto cleanup;
    }

    /* if stdout_fds and stderr_fds were not created above, they stay
     * at their default of -1, leading stdout/stderr to be routed to
     * journald in most systemd configurations (i.e. the
     * SDEXEC_LOG_SYSTEMD setting).
     */
    if (!(se->sdp = sdprocess_exec (se->h,
                                    unitname,
                                    cmdv,
                                    envv,
                                    se->stdin_fds[1],
                                    se->stdout_fds[1],
                                    se->stderr_fds[1]))) {
        se->start_errno = errno;
        jobinfo_fatal_error (job, errno, "sdprocess_exec");
        goto cleanup;
    }

    if (sdprocess_state (se->sdp, state_cb, se) < 0) {
        jobinfo_fatal_error (job, errno, "sdprocess_state");
        goto cleanup;
    }

    if (se->stdoutlog == SDEXEC_LOG_EVENTLOG) {
        if (!(se->w_stdout = flux_fd_watcher_create (flux_get_reactor (se->h),
                                                     se->stdout_fds[0],
                                                     FLUX_POLLIN,
                                                     stdout_cb,
                                                     se))) {
            jobinfo_fatal_error (job, errno, "flux_fd_watcher_create");
            goto cleanup;
        }
        flux_watcher_start (se->w_stdout);
    }

    if (se->stderrlog == SDEXEC_LOG_EVENTLOG) {
        if (!(se->w_stderr = flux_fd_watcher_create (flux_get_reactor (se->h),
                                                     se->stderr_fds[0],
                                                     FLUX_POLLIN,
                                                     stderr_cb,
                                                     se))) {
            jobinfo_fatal_error (job, errno, "flux_fd_watcher_create");
            goto cleanup;
        }
        flux_watcher_start (se->w_stderr);
    }

    rv = 0;
cleanup:
    free (cmdv);
    free (envv);
    return rv;
}

static int sdexec_start (struct jobinfo *job)
{
    struct sdexec *se = job->data;
    char *unitname = NULL;
    uint32_t rank;
    int rv = -1;

    if (flux_get_rank (se->h, &rank) < 0) {
        jobinfo_fatal_error (job, errno, "flux_get_rank");
        goto cleanup;
    }

    if (asprintf (&unitname,
                  "flux-sdexec-%u-%ju",
                  rank,
                  (uintmax_t)job->id) < 0) {
        jobinfo_fatal_error (job, errno, "asprintf");
        goto cleanup;
    }

    if (job->reattach)
        rv = sdexec_reattach (se, job, unitname);
    else
        rv = sdexec_launch (se, job, unitname);
cleanup:
    free (unitname);
    return rv;
}

static void kill_completion_cb (flux_subprocess_t *p)
{
    struct sdexec *se = flux_subprocess_aux_get (p, "sdexec::kill");
    assert (se != NULL);
    if (flux_subprocess_exit_code (p))
        flux_log_error (se->h, "imp kill failure");
    flux_subprocess_destroy (p);
    jobinfo_decref (se->job);
}

static void kill_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct sdexec *se = flux_subprocess_aux_get (p, "sdexec::kill");
    const char *cmd = flux_cmd_arg (se->cmd, 0);
    const char *s;
    int len;

    assert (se != NULL);

    if (!(s = flux_subprocess_getline (p, stream, &len))) {
        flux_log_error (se->h, "flux_subprocess_getline");
        return;
    }
    if (len)
        jobinfo_log_output (se->job,
                            0,
                            basename (cmd),
                            stream,
                            s,
                            len);
}

/* Observations have shown that if a systemd process fails and
 * a terminating signal such as SIGTERM is sent immediately
 * afterwards, the state callback may not be called upon
 * process exit.  The assumption is that if the process ended
 * in two different ways (signal and failure) in very short
 * succession, that systemd is not sure what prompted the
 * failure and thus does not initiate the callback.  Unclear
 * if this is a bug in systemd or not.
 *
 * For example, this could happen if the user input a bad
 * command.  The job-shell could immediately fail (exit code
 * == 127), generate an exception, leading to job-exec sending
 * a SIGTERM.
 *
 * If job-exec has reached the point of using SIGKILL and
 * jobinfo_tasks_complete() has not yet been called, assume
 * the state callback will never be called.  We'll go ahead and
 * call jobinfo_tasks_complete() here instead.
 */
static void sdexec_handle_exit_race (struct sdexec *se)
{
    int wait_status;

    if (se->jobinfo_tasks_complete_called)
        return;

    flux_log (se->h,
              LOG_DEBUG,
              "Calling jobinfo_tasks_complete() due to SIGKILL");

    wait_status = sdprocess_wait_status (se->sdp);
    if (wait_status < 0) {
        flux_log (se->h,
                  LOG_ERR,
                  "wait status unavailable, set to SIGKILL");
        wait_status = __W_EXITCODE (0, SIGKILL);
    }
    jobinfo_tasks_complete (se->job,
                            resource_set_ranks (se->job->R),
                            wait_status);
    se->jobinfo_tasks_complete_called = true;
}

static int sdexec_kill_multiuser (struct sdexec *se, int signum)
{
    flux_cmd_t *cmd = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion =   kill_completion_cb,
        .on_stdout =       kill_output_cb,
        .on_stderr =       kill_output_cb,
    };
    int save_errno, pid, rv = -1;
    flux_subprocess_t *p = NULL;
    uint32_t rank;

    if ((pid = sdprocess_pid (se->sdp)) < 0) {
        /* Always a chance the shell has exited already and there
         * is no "main" PID to retrieve.  For example, this could
         * happen if the user input a bad command and the shell
         * exited immediately.
         */
        if (errno == EPERM) {
            if (signum == SIGKILL)
                sdexec_handle_exit_race (se);
            return 0;
        }
        else
            flux_log_error (se->h, "sdprocess_pid");
        return -1;
    }

    if (!(cmd = flux_cmd_create (0, NULL, NULL))) {
        flux_log_error (se->h, "flux_cmd_create");
        goto cleanup;
    }

    if (flux_cmd_argv_append (cmd, config_get_imp_path ()) < 0
        || flux_cmd_argv_append (cmd, "kill") < 0
        || flux_cmd_argv_appendf (cmd, "%d", signum) < 0
        || flux_cmd_argv_appendf (cmd, "%d", pid) < 0) {
        flux_log_error (se->h, "flux_cmd_argv_append");
        goto cleanup;
    }

    if (flux_cmd_setcwd (cmd, config_get_cwd (se->job)) < 0) {
        flux_log_error (se->h, "flux_cmd_setcwd");
        goto cleanup;
    }

    if (flux_get_rank (se->h, &rank) < 0) {
        flux_log_error (se->h, "flux_get_rank");
        goto cleanup;
    }

    if (!(p = flux_rexec_ex (se->h,
                             "rexec",
                             rank,
                             0,
                             cmd,
                             &ops,
                             flux_llog,
                             se->h))) {
        flux_log_error (se->h, "flux_rexec_ex");
        goto cleanup;
    }

    /* increment jobinfo refcount, so that sdexec_exit() will not be
     * called until after it is decremented in kill_completion_cb()
     * (i.e. after the kill subprocess has completed).
     */
    jobinfo_incref (se->job);

    if (flux_subprocess_aux_set (p, "sdexec::kill", se, NULL) < 0) {
        flux_log_error (se->h, "flux_subprocess_aux_set");
        jobinfo_decref (se->job);
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    flux_cmd_destroy (cmd);
    errno = save_errno;
    return rv;
}

static int sdexec_kill_single (struct sdexec *se, int signum)
{
    int ret = sdprocess_kill (se->sdp, signum);
    if (ret < 0)
        flux_log_error (se->h, "sdprocess_kill");
    if (signum == SIGKILL)
        sdexec_handle_exit_race (se);
    return ret;
}

static int sdexec_kill (struct jobinfo *job, int signum)
{
    struct sdexec *se = job->data;

    if (!se->sdp)
        return 0;

    if (job->multiuser)
        return sdexec_kill_multiuser (se, signum);
    return sdexec_kill_single (se, signum);
}

static void sdexec_exit (struct jobinfo *job)
{
    struct sdexec *se = job->data;
    sdexec_destroy (se);
    job->data = NULL;
}

static int sdexec_cancel (struct jobinfo *job)
{
    struct sdexec *se = job->data;

    /* sdp can be NULL if sdprocess_create() or sdprocess_find_unit()
     * fails, such as if systemd is not setup correctly.  If sdp is
     * non-NULL, job-exec must call 'sdexec_kill' to properly kill
     * the process that has been started.
     */
    if (!se->sdp) {
        /* use errno from startup failure for exit code if available,
         * otherwise just use EPERM */
        int tmp_errno = se->start_errno ? se->start_errno : EPERM;
        int wait_status = __W_EXITCODE (0, tmp_errno);
        jobinfo_tasks_complete (se->job,
                                resource_set_ranks (se->job->R),
                                wait_status);
    }
    return 0;
}

struct exec_implementation sdexec = {
    .name =     "sdexec",
    .init =     sdexec_init,
    .exit =     sdexec_exit,
    .start =    sdexec_start,
    .kill =     sdexec_kill,
    .cancel =   sdexec_cancel,
};

/* vi: ts=4 sw=4 expandtab
 */
