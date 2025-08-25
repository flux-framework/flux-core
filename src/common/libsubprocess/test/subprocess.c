/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/subprocess_private.h"
#include "src/common/libsubprocess/server.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

extern char **environ;

int completion_cb_count;
int completion_fail_cb_count;
int stdout_output_cb_count;
int stderr_output_cb_count;
int env_passed_cb_count;
int completion_sigterm_cb_count;
int output_processes_cb_count;
int parent_pid;
int child_pid;
int stdout_eof_cb_count;
int stderr_eof_cb_count;
int state_change_cb_count;
int stopped_cb_count;
int timer_cb_count;

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

void completion_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 0,
        "subprocess exit code is 0, got %d", flux_subprocess_exit_code (p));
    completion_cb_count++;
}

void test_corner_cases (flux_reactor_t *r)
{
    flux_t *h = NULL;
    char *avgood[] = { "true", NULL };
    char *avbad[] = { NULL };
    flux_cmd_t *cmd;

    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open on loop works");

    ok (!subprocess_server_create (NULL, NULL, NULL, NULL, NULL)
        && errno == EINVAL,
        "subprocess_server_create fails with NULL pointer inputs");
    ok (subprocess_server_shutdown (NULL, 0) == NULL
        && errno == EINVAL,
        "subprocess_server_shutdown fails with NULL pointer input");

    ok (flux_local_exec (NULL, 0, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with NULL pointer inputs");
    ok (flux_local_exec (r, 1234, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with invalid flag");
    ok (flux_rexec (NULL, 0, 0, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with NULL pointer inputs");
    ok (flux_rexec (h, 0, 1, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with invalid flag");

    ok ((cmd = flux_cmd_create (0, avbad, NULL)) != NULL,
        "flux_cmd_create with 0 args works");
    ok (flux_local_exec (r, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with cmd with zero args");
    ok (flux_rexec (h, 0, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with zero args");
    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, avgood, NULL)) != NULL,
        "flux_cmd_create with true works");
    ok (flux_rexec (h, -10, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with invalid rank");
    flux_cmd_destroy (cmd);

    lives_ok ({flux_subprocess_stream_start (NULL, NULL);},
        "flux_subprocess_stream_start doesn't crash with NULL pointer inputs");
    lives_ok ({flux_subprocess_stream_stop (NULL, NULL);},
        "flux_subprocess_stream_stop doesn't crash with NULL pointer inputs");

    ok (flux_subprocess_write (NULL, "stdin", "foo", 3) < 0
        && errno == EINVAL,
        "flux_subprocess_write fails with NULL pointer input");
    ok (flux_subprocess_close (NULL, "stdin") < 0
        && errno == EINVAL,
        "flux_subprocess_close fails with NULL pointer input");
    ok (flux_subprocess_read (NULL, "stdout", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read fails with NULL pointer inputs");
    ok (flux_subprocess_read_line (NULL, "stdout", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read_line fails with NULL pointer inputs");
    ok (flux_subprocess_read_trimmed_line (NULL, "stdout", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read_trimmed_line fails with NULL pointer inputs");
    ok (flux_subprocess_read_stream_closed (NULL, "stdout") == false,
        "flux_subprocess_read_stream_closed returns false with NULL pointer input");
    ok (flux_subprocess_kill (NULL, 0) == NULL
        && errno == EINVAL,
        "flux_subprocess_kill fails with NULL pointer input");
    ok ((int)flux_subprocess_state (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_state fails with NULL pointer input");
    ok (flux_subprocess_rank (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_rank fails with NULL pointer input");
    ok (flux_subprocess_fail_errno (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_fail_errno fails with NULL pointer input");
    ok (flux_subprocess_fail_error (NULL) != NULL,
        "flux_subprocess_fail_error works with NULL pointer input");
    ok (flux_subprocess_status (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_status fails with NULL pointer input");
    ok (flux_subprocess_exit_code (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_exit_code fails with NULL pointer input");
    ok (flux_subprocess_signaled (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_signaled fails with NULL pointer input");
    ok (flux_subprocess_pid (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_pid fails with NULL pointer input");
    ok (flux_subprocess_get_cmd (NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_get_cmd fails with NULL pointer input");
    ok (flux_subprocess_get_reactor (NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_get_reactor fails with NULL pointer input");
    ok (flux_subprocess_aux_set (NULL, "foo", "bar", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_aux_set fails with NULL pointer input");
    ok (flux_subprocess_aux_get (NULL, "foo") == NULL
        && errno == EINVAL,
        "flux_subprocess_aux_get fails with NULL pointer input");

    ok ((cmd = flux_cmd_create (1, avgood, NULL)) != NULL,
        "flux_cmd_create with true works");
    ok (flux_local_exec (r,
                         FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF,
                         cmd,
                         NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with invalid flag");
    flux_cmd_destroy (cmd);

    flux_close (h);
}

void test_post_exec_errors (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok (flux_subprocess_write (p, NULL, NULL, 0) < 0
        && errno == EINVAL,
        "flux_subprocess_write returns EINVAL on bad input");
    ok (flux_subprocess_write (p, "foo", "foo", 3) < 0
        && errno == EINVAL,
        "flux_subprocess_write returns EINVAL on bad stream");
    ok (flux_subprocess_close (p, "foo") < 0
        && errno == EINVAL,
        "flux_subprocess_close returns EINVAL on bad stream");
    ok (flux_subprocess_read (p, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad input");
    ok (flux_subprocess_read (p, "foo", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad stream");
    ok (flux_subprocess_read_line (p, "foo", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read_line returns EINVAL on bad stream");
    ok (flux_subprocess_read_trimmed_line (p, "foo", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_read_trimmed_line returns EINVAL on bad stream");
    ok (flux_subprocess_read_stream_closed (p, "foo") == false,
        "flux_subprocess_read_stream_closed returns false on bad stream");
    ok (flux_subprocess_kill (p, 0) == NULL
        && errno == EINVAL,
        "flux_subprocess_kill returns EINVAL on illegal signum");
    ok (flux_subprocess_rank (p) < 0,
        "flux_subprocess_rank fails b/c subprocess is local");
    ok (flux_subprocess_fail_errno (p) < 0,
        "subprocess fail errno fails b/c subprocess not failed");
    ok (flux_subprocess_fail_error (p) != NULL,
        "subprocess fail error works when subprocess not failed");
    ok (flux_subprocess_status (p) < 0,
        "subprocess status fails b/c subprocess not yet exited");
    ok (flux_subprocess_exit_code (p) < 0,
        "subprocess exit_code fails b/c subprocess not yet exited");
    ok (flux_subprocess_signaled (p) < 0,
        "subprocess signaled fails b/c subprocess not yet exited");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");

    ok (flux_subprocess_write (p, "stdin", "foo", 3) < 0
        && errno == EPIPE,
        "flux_subprocess_write returns EPIPE b/c process already completed");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd, *cmd2;
    flux_reactor_t *r2;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok ((flux_subprocess_pid (p) > (pid_t) 0),
        "flux_local_exec() started pid %ld", (pid_t) flux_subprocess_pid (p));
    ok ((cmd2 = flux_subprocess_get_cmd (p)) != NULL,
        "flux_subprocess_get_cmd success");
    ok ((r2 = flux_subprocess_get_reactor (p)) != NULL,
        "flux_subprocess_get_reactor success");
    ok (r == r2,
        "flux_subprocess_get_reactor returns correct reactor");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void completion_fail_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 1,
        "subprocess exit code is 1");
    completion_fail_cb_count++;
}

void test_basic_fail (flux_reactor_t *r)
{
    char *av[] = { "false", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_fail_cb
    };
    completion_fail_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_fail_cb_count == 1, "completion fail callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_flag_no_setpgrp (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_NO_SETPGRP, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_flag_fork_exec (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_FORK_EXEC, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void env_passed_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    ok (!strcasecmp (stream, "stdout"),
        "env_passed_cb called with correct stream");

    if (!env_passed_cb_count) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (strstarts (buf, "FOOBAR=foobaz"),
            "environment variable FOOBAR in subprocess");
        ok (len == 14,
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    env_passed_cb_count++;
}

void test_env_passed (flux_reactor_t *r)
{
    char *av[] = { "/usr/bin/env", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setenvf (cmd, 1, "FOOBAR", "foobaz") == 0,
        "flux_cmd_setenvf");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = env_passed_cb
    };
    completion_cb_count = 0;
    env_passed_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (env_passed_cb_count == 2, "channel fd callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void completion_sigterm_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_signaled (p) == SIGTERM,
        "subprocess terminated by SIGTERM");
    flux_reactor_stop (flux_subprocess_get_reactor (p));
    completion_sigterm_cb_count++;

    errno = 0;
    ok (flux_subprocess_kill (p, SIGTERM) == NULL && errno == ESRCH,
        "flux_subprocess_kill fails with ESRCH");

}

void test_kill (flux_reactor_t *r)
{
    char *av[]  = { "/bin/sleep", "1000", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_future_t *f = NULL;

    ok ((cmd = flux_cmd_create (2, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_sigterm_cb
    };
    completion_sigterm_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    f = flux_subprocess_kill (p, SIGTERM);
    ok (f != NULL, "flux_subprocess_kill returns future_t");
    ok (flux_future_wait_for (f, 0.) == 0,
        "future fulfilled immediately for local process");

    ok (flux_future_get (f, NULL) == 0, "flux_future_get (f) returns 0");
    ok (flux_reactor_run (r, 0) == 0, "reactor_run exits normally");
    ok (completion_sigterm_cb_count == 1, "completion sigterm callback called 1 time");

    flux_subprocess_destroy (p);
    flux_future_destroy (f);
    flux_cmd_destroy (cmd);
}

void output_processes_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    if (output_processes_cb_count == 0
        || output_processes_cb_count == 1) {
        len = flux_subprocess_read_trimmed_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_trimmed_line read valid data");

        if (len > 0 && buf) {
            if (output_processes_cb_count == 0)
                parent_pid = atoi (buf);
            else
                child_pid = atoi (buf);
        }

        if (output_processes_cb_count == 1
            && !flux_subprocess_aux_get (p, "nokill")) {
            flux_future_t *f = NULL;
            f = flux_subprocess_kill (p, SIGTERM);
            ok (f != NULL, "flux_subprocess_kill returns future_t");
            /* ignore response, we're not going to wait for it */
            flux_future_destroy (f);
        }
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    output_processes_cb_count++;
}

int wait_kill (int pid)
{
    int ret;
    int i;

    /* we'll try for at most a second (10 * .1 seconds) */
    ret = kill (parent_pid, 0);
    for (i = 0; i < 10 && ret == 0; i++) {
        usleep (100000);
        ret = kill (parent_pid, 0);
    }
    return ret;
}

void test_kill_setpgrp (flux_reactor_t *r)
{
    char *av[]  = { TEST_SUBPROCESS_DIR "test_fork_sleep", "100", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    int ret;

    ok ((cmd = flux_cmd_create (2, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_sigterm_cb,
        .on_stdout = output_processes_cb,
    };
    completion_sigterm_cb_count = 0;
    output_processes_cb_count = 0;
    parent_pid = -1;
    child_pid = -1;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_reactor_run (r, 0) == 0, "reactor_run exits normally");
    ok (completion_sigterm_cb_count == 1, "completion sigterm callback called 1 time");
    ok (output_processes_cb_count == 3, "output processes callback called 3 times");
    /* checking if a pid has been killed at this point is a tad racy,
     * so if necessary loop a second to wait for the kill to happen
     */
    ret = wait_kill (parent_pid);
    ok (ret < 0
        && errno == ESRCH,
        "kill fails with ESRCH, parent pid killed %d", parent_pid);
    ret = wait_kill (child_pid);
    ok (ret < 0
        && errno == ESRCH,
        "kill fails with ESRCH, child pid killed %d", child_pid);
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void kill_on_exit (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state == FLUX_SUBPROCESS_EXITED) {
        flux_future_t *f = flux_subprocess_kill (p, SIGTERM);
        /* Note: in local subprocess case, future is returned from
         * flux_subprocess_kill() fulfilled.
         */
        ok (f && flux_future_get (f, NULL) == 0,
            "flux_subprocess_kill() works after parent exited: (%d) %s",
            errno,
            strerror (errno));
        flux_future_destroy (f);
    }
}

void completion_parent_normal_exit (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 0,
        "subprocess terminated normally");
    flux_reactor_stop (flux_subprocess_get_reactor (p));
    completion_sigterm_cb_count++;

    errno = 0;
    ok (flux_subprocess_kill (p, SIGTERM) == NULL && errno == ESRCH,
        "flux_subprocess_kill fails with ESRCH");

}
void test_kill_setpgrp_parent_exited (flux_reactor_t *r)
{
    char *av[]  = { TEST_SUBPROCESS_DIR "test_fork_sleep", "100", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    int ret;

    ok ((cmd = flux_cmd_create (2, av, environ)) != NULL,
        "flux_cmd_create");
    ok (flux_cmd_setenvf (cmd, 1, "TEST_FORK_SLEEP_NOWAIT", "t") == 0,
        "setenv TEST_FORK_SLEEP_NOWAIT=t");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_parent_normal_exit,
        .on_stdout = output_processes_cb,
        .on_state_change = kill_on_exit,
    };
    completion_sigterm_cb_count = 0;
    output_processes_cb_count = 0;
    parent_pid = -1;
    child_pid = -1;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    /* Don't kill subprocess in output handler:
     */
    ok (flux_subprocess_aux_set (p, "nokill", (void *) 0x1, NULL) == 0,
        "flux_subprocess_aux_set (\"nokill\")");
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_reactor_run (r, 0) == 0, "reactor_run exits normally");

    ok (completion_sigterm_cb_count == 1,
        "completion sigterm callback called 1 time");
    ok (output_processes_cb_count == 3,
        "output processes callback called 3 times");

    /* checking if a pid has been killed at this point is a tad racy,
     * so if necessary loop a second to wait for the kill to happen
     */
    ret = wait_kill (parent_pid);
    ok (ret < 0 && errno == ESRCH,
        "kill fails with ESRCH, parent pid killed %d", parent_pid);
    ret = wait_kill (child_pid);
    ok (ret < 0 && errno == ESRCH,
        "kill fails with ESRCH, child pid killed %d", child_pid);
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void eof_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_eof_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stderr_eof_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ok (flux_subprocess_read_stream_closed (p, stream),
        "flux_subprocess_read_stream_closed saw EOF on %s", stream);

    len = flux_subprocess_read (p, stream, &buf);
    ok (len == 0,
        "flux_subprocess_read on %s read EOF", stream);

    (*counter)++;
}

void test_kill_eofs (flux_reactor_t *r)
{
    char *av[]  = { "/bin/sleep", "1000", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_future_t *f = NULL;

    ok ((cmd = flux_cmd_create (2, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_sigterm_cb,
        .on_stdout = eof_cb,
        .on_stderr = eof_cb,
    };
    completion_sigterm_cb_count = 0;
    stdout_eof_cb_count = 0;
    stderr_eof_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    f = flux_subprocess_kill (p, SIGTERM);
    ok (f != NULL, "flux_subprocess_kill returns future_t");
    ok (flux_future_wait_for (f, 0.) == 0,
        "future fulfilled immediately for local process");

    ok (flux_future_get (f, NULL) == 0, "flux_future_get (f) returns 0");
    ok (flux_reactor_run (r, 0) == 0, "reactor_run exits normally");
    ok (completion_sigterm_cb_count == 1, "completion sigterm callback called 1 time");
    ok (stdout_eof_cb_count == 1, "stdout eof callback called 1 times");
    ok (stderr_eof_cb_count == 1, "stderr eof callback called 1 times");
    flux_subprocess_destroy (p);
    flux_future_destroy (f);
    flux_cmd_destroy (cmd);
}

void state_change_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state_change_cb_count == 0)
        ok (state == FLUX_SUBPROCESS_RUNNING,
            "subprocess state == RUNNING in state change handler");
    else
        ok (state == FLUX_SUBPROCESS_EXITED,
            "subprocess state == EXITED in state change handler");
    state_change_cb_count++;
}

void test_state_change (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_change_cb
    };
    completion_cb_count = 0;
    state_change_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (state_change_cb_count == 2, "state change callback called 3 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void state_change_stopped_cb (flux_subprocess_t *p,
                              flux_subprocess_state_t state)
{
    diag ("state_change_stopped: state = %s",
          flux_subprocess_state_string (state));
    if (state == FLUX_SUBPROCESS_STOPPED) {
        flux_future_t *f = flux_subprocess_kill (p, SIGKILL);
        ok (true, "subprocess state == STOPPED in state change handler");
        flux_future_destroy (f);
        stopped_cb_count++;
    }
}

void test_state_change_stopped (flux_reactor_t *r)
{
    char *av[] = { "/bin/sleep", "30", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (2, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_state_change = state_change_stopped_cb
    };
    stopped_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    flux_future_t *f = flux_subprocess_kill (p, SIGSTOP);
    ok (f != NULL,
        "flux_subprocess_kill SIGSTOP");
    flux_future_destroy (f);

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (stopped_cb_count == 1, "subprocess was stopped");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_state_strings (void)
{
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_INIT), "Init"),
        "flux_subprocess_state_string returns correct string");
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_RUNNING), "Running"),
        "flux_subprocess_state_string returns correct string");
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_EXITED), "Exited"),
        "flux_subprocess_state_string returns correct string");
    ok (!flux_subprocess_state_string (100),
        "flux_subprocess_state_string returns NULL on bad state");
    is (flux_subprocess_state_string (FLUX_SUBPROCESS_STOPPED),
        "Stopped");
}

void test_exec_fail (flux_reactor_t *r)
{
    char path [4096];
    char *av_eacces[]  = { "/", NULL };
    char *av_enoent[]  = { "/usr/bin/foobarbaz", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av_eacces, NULL)) != NULL, "flux_cmd_create");

    /*  Set cwd to force use of fork/exec */
    ok (flux_cmd_setcwd (cmd, getcwd (path, sizeof (path))) == 0,
        "flux_cmd_setcwd");

    p = flux_local_exec (r, 0, cmd, NULL);
    ok (p == NULL
        && errno == EACCES,
        "flux_local_exec failed with EACCES");

    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, av_enoent, NULL)) != NULL, "flux_cmd_create");

    /*  Set cwd to force use of fork/exec */
    ok (flux_cmd_setcwd (cmd, getcwd (path, sizeof (path))) == 0,
        "flux_cmd_setcwd");

    p = flux_local_exec (r, 0, cmd, NULL);
    ok (p == NULL
        && errno == ENOENT,
        "flux_local_exec failed with ENOENT");

    flux_cmd_destroy (cmd);
}

void test_context (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    char *extra = "mydata";
    char *tmp;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok (flux_subprocess_aux_set (p, "extra", extra, NULL) == 0,
        "flux_subprocess_aux_set success");
    ok ((tmp = flux_subprocess_aux_get (p, "extra")) != NULL,
        "flux_subprocess_aux_get success");
    ok (tmp == extra,
        "flux_subprocess_aux_get returned correct pointer");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_refcount (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    char *extra = "mydata";
    char *tmp;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok (flux_subprocess_aux_set (p, "extra", extra, NULL) == 0,
        "flux_subprocess_aux_set success");
    subprocess_incref (p);

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    subprocess_decref (p);

    /* normally this should fail, but we've increased the refcount so
     * subprocess should not be destroyed */
    ok ((tmp = flux_subprocess_aux_get (p, "extra")) != NULL,
        "flux_subprocess_aux_get success");
    ok (tmp == extra,
        "flux_subprocess_aux_get returned correct pointer");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void shmem_hook_cb (flux_subprocess_t *p, void *arg)
{
    int *shmem_count = arg;
    (*shmem_count)++;
}

void test_pre_exec_hook (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    int *shmem_count;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    /* pre_exec run in child, so we use shared memory */
    shmem_count = mmap (NULL,
                        sizeof (int),
                        PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_SHARED,
                        -1,
                        0);
    ok (shmem_count != NULL, "mmap success");
    (*shmem_count) = 0;

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
    };
    flux_subprocess_hooks_t hooks = {
        .pre_exec = shmem_hook_cb,
        .pre_exec_arg = shmem_count
    };
    completion_cb_count = 0;
    p = flux_local_exec_ex (r,
                            FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH,
                            cmd,
                            &ops,
                            &hooks,
                            NULL,
                            NULL);
    ok (p != NULL, "flux_local_exec_ex");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok ((*shmem_count) == 1, "pre_exec hook called correctly");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    munmap (shmem_count, sizeof (int));
}

void count_hook_cb (flux_subprocess_t *p, void *arg)
{
    int *count = arg;
    (*count)++;
}

void test_post_fork_hook (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    int hook_count = 0;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
    };
    flux_subprocess_hooks_t hooks = {
        .post_fork = count_hook_cb,
        .post_fork_arg = &hook_count
    };
    completion_cb_count = 0;
    p = flux_local_exec_ex (r, 0, cmd, &ops, &hooks, NULL, NULL);
    ok (p != NULL, "flux_local_exec_ex");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (hook_count == 1, "post_fork hook cb called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void destroy_in_completion_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 0,
        "subprocess exit code is 0");
    completion_cb_count++;
    flux_subprocess_destroy (p);
}

void test_destroy_in_completion (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd, *cmd2;
    flux_reactor_t *r2;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = destroy_in_completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok ((flux_subprocess_pid (p) > (pid_t) 0),
        "flux_local_exec() started pid %ld", (pid_t) flux_subprocess_pid (p));
    ok ((cmd2 = flux_subprocess_get_cmd (p)) != NULL,
        "flux_subprocess_get_cmd success");
    ok ((r2 = flux_subprocess_get_reactor (p)) != NULL,
        "flux_subprocess_get_reactor success");
    ok (r == r2,
        "flux_subprocess_get_reactor returns correct reactor");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_cmd_destroy (cmd);
}

void fail_completion_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 127,
        "subprocess exit code is 127, got %d", flux_subprocess_exit_code (p));
    completion_cb_count++;
}

void fail_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stderr_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }
    else
        ok (false, "fail_output_cb called multiple times");

    (*counter)++;
}

void test_fail_notacommand (flux_reactor_t *r)
{
    char *av[] = { "notacommand", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = fail_completion_cb,
        .on_stdout = fail_output_cb,
        .on_stderr = fail_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    /* Per manpage:
     *
     * If posix_spawn() or posix_spawnp() fail for any of the reasons
     *  that would cause fork() or one of the exec family of functions
     *  to fail, an error value shall be returned as described by
     *  fork() and exec, respectively (or, if the error occurs after
     *  the calling process successfully returns, the child process
     *  shall exit with exit status 127).
     *
     * So we can't assume flux_local_exec() returns an error on posix_spawn().
     */
    if (p == NULL) {
        ok (p == NULL, "flux_local_exec failed");
        ok (errno == ENOENT, "flux_local_exec returned ENOENT");
    }
    else {
        ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
            "subprocess state == RUNNING after flux_local_exec");

        int rc = flux_reactor_run (r, 0);
        ok (rc == 0, "flux_reactor_run returned zero status");
        ok (completion_cb_count == 1, "completion callback called 1 time");
        ok (stdout_output_cb_count == 1, "stdout output callback called 1 times");
        ok (stderr_output_cb_count == 1, "stderr output callback called 1 times");
        flux_subprocess_destroy (p);
    }
    flux_cmd_destroy (cmd);
}

void test_fail_notacommand_fork (flux_reactor_t *r)
{
    char *av[] = { "notacommand", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = fail_completion_cb,
    };
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_FORK_EXEC, cmd, &ops);
    ok (p == NULL, "flux_local_exec failed");
    ok (errno == ENOENT, "flux_local_exec returned ENOENT");
    flux_cmd_destroy (cmd);
}

int fdcleanup_fdcount;

void fdcleanup_output (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    len = flux_subprocess_read_line (p, stream, &buf);

    if (len > 0 && buf != NULL) {
        diag ("%s: %.*s", stream, len, buf);
        if (streq (stream, "stdout"))
            fdcleanup_fdcount = strtoul (buf, NULL, 10);
    }
}

/* This test ensures that subprocs aren't gifted with bonus file
 * descriptors. */
void test_fdcleanup (flux_reactor_t *r,
                     const char *desc,
                     int flags,
                     int expected_fdcount)
{
    char *av[] = {
        "sh",
        "-c",
        "ls -1 /dev/fd/ | wc -w",
        NULL
    };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (ARRAY_SIZE (av) - 1, av, NULL)) != NULL,
        "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_stdout =  fdcleanup_output,
        .on_stderr =  fdcleanup_output,
        .on_completion = completion_cb,
    };
    p = flux_local_exec (r, flags, cmd, &ops);
    ok (p != NULL, "flux_local_exec %s", desc);
    completion_cb_count = 0;
    fdcleanup_fdcount = 0;
    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* N.B. It is believed there are two file descriptors that are
     * racy here, so the fdcleanup_fdcount may be as high as 2 more
     * than the expected_fdcount.
     *
     * 1) The `ls` in this test may result in a file descriptor in
     * /proc/$$/fd (i.e. a file descriptor for reading
     * /proc/SOMEPID/fd)
     *
     * 2) We are racing with the removal of the sync_fd in the
     * subprocess.
     */
    ok ((fdcleanup_fdcount >= expected_fdcount)
        && (fdcleanup_fdcount <= (expected_fdcount + 2)),
        "%d file descriptors are open (expected %d-%d)",
        fdcleanup_fdcount,
        expected_fdcount,
        expected_fdcount + 2);

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *r;
    int start_fdcount, end_fdcount;

    plan (NO_PLAN);

    start_fdcount = fdcount ();

    // Create shared reactor for all tests
    ok ((r = flux_reactor_create (0)) != NULL,
        "flux_reactor_create");

    diag ("corner_cases");
    test_corner_cases (r);
    diag ("post_exec_errors");
    test_post_exec_errors (r);

    diag ("basic");
    test_basic (r);
    diag ("basic_fail");
    test_basic_fail (r);
    diag ("env_passed");
    test_env_passed (r);
    diag ("flag_no_setpgrp");
    test_flag_no_setpgrp (r);
    diag ("flag_fork_exec");
    test_flag_fork_exec (r);
    diag ("kill");
    test_kill (r);
    diag ("kill_setpgrp");
    test_kill_setpgrp (r);
    diag ("kill_setpgrp_parent_exited");
    test_kill_setpgrp_parent_exited (r);
    diag ("kill_eofs");
    test_kill_eofs (r);
    diag ("state_change");
    test_state_change (r);
    diag ("state_change_stopped");
    test_state_change_stopped (r);
    diag ("state_strings");
    test_state_strings ();
    diag ("exec_fail");
    test_exec_fail (r);
    diag ("context");
    test_context (r);
    diag ("refcount");
    test_refcount (r);
    diag ("pre_exec_hook");
    test_pre_exec_hook (r);
    diag ("post_fork_hook");
    test_post_fork_hook (r);
    diag ("test_destroy_in_completion");
    test_destroy_in_completion (r);
    diag ("fail_notacommand");
    test_fail_notacommand (r);
    diag ("fail_notacommand_fork");
    test_fail_notacommand_fork (r);
    diag ("test_fdcleanup fork-exec");
    test_fdcleanup (r, "fork-exec", FLUX_SUBPROCESS_FLAGS_FORK_EXEC, 3);
    diag ("test_fdcleanup posix-spawn");
    test_fdcleanup (r, "posix-spawn", 0, 3);

    flux_reactor_destroy (r);

    end_fdcount = fdcount ();

    ok (start_fdcount == end_fdcount,
        "no file descriptors leaked");

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
