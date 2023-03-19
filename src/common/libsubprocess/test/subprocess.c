/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <assert.h>

#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/server.h"

extern char **environ;

int completion_cb_count;
int completion_fail_cb_count;
int stdout_output_cb_count;
int stderr_output_cb_count;
int stdout_output_cb_len_count;
int stderr_output_cb_len_count;
int output_default_stream_cb_count;
int multiple_lines_stdout_output_cb_count;
int multiple_lines_stderr_output_cb_count;
int stdin_closed_stdout_cb_count;
int stdin_closed_stderr_cb_count;
int env_passed_cb_count;
int completion_sigterm_cb_count;
int output_processes_cb_count;
int parent_pid;
int child_pid;
int stdout_eof_cb_count;
int stderr_eof_cb_count;
int state_change_cb_count;
int stopped_cb_count;
int channel_fd_env_cb_count;
int channel_in_cb_count;
int channel_in_and_out_cb_count;
int multiple_lines_channel_cb_count;
int channel_nul_terminate_cb_count;
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

void test_basic_errors (flux_reactor_t *r)
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
        "subprocess_server_shutdown fails with NULL pointer inputs");

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
        "flux_cmd_create with 0 args works");
    ok (flux_rexec (h, -10, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with invalid rank");
    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, avgood, NULL)) != NULL,
        "flux_cmd_create with 0 args works");
    ok (flux_cmd_setcwd (cmd, "foobar") == 0,
        "flux_cmd_setcwd works");
    ok (flux_cmd_setopt (cmd, "stdout_STREAM_STOP", "true") == 0,
        "flux_cmd_setopt works");
    ok (flux_rexec (h, 0, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with STREAM_STOP option");
    flux_cmd_destroy (cmd);

    ok (flux_subprocess_stream_start (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_start fails with NULL pointer inputs");
    ok (flux_subprocess_stream_stop (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_stop fails with NULL pointer inputs");
    ok (flux_subprocess_stream_status (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_status fails with NULL pointer inputs");

    ok (flux_subprocess_write (NULL, "stdin", "foo", 3) < 0
        && errno == EINVAL,
        "flux_subprocess_write fails with NULL pointer inputs");
    ok (flux_subprocess_close (NULL, "stdin") < 0
        && errno == EINVAL,
        "flux_subprocess_close fails with NULL pointer inputs");
    ok (flux_subprocess_read (NULL, "stdout", -1, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read fails with NULL pointer inputs");
    ok (flux_subprocess_read_line (NULL, "stdout", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read_line fails with NULL pointer inputs");
    ok (flux_subprocess_read_trimmed_line (NULL, "stdout", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read_trimmed_line fails with NULL pointer inputs");
    ok (flux_subprocess_read_stream_closed (NULL, "stdout") < 0
        && errno == EINVAL,
        "flux_subprocess_read_stream_closed fails with NULL pointer inputs");
    ok (flux_subprocess_kill (NULL, 0) == NULL
        && errno == EINVAL,
        "flux_subprocess_kill fails with NULL pointer inputs");
    ok ((int)flux_subprocess_state (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_state fails with NULL pointer inputs");
    ok (flux_subprocess_rank (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_rank fails with NULL pointer inputs");
    ok (flux_subprocess_fail_errno (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_fail_errno fails with NULL pointer inputs");
    ok (flux_subprocess_status (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_status fails with NULL pointer inputs");
    ok (flux_subprocess_exit_code (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_exit_code fails with NULL pointer inputs");
    ok (flux_subprocess_signaled (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_signaled fails with NULL pointer inputs");
    ok (flux_subprocess_pid (NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_pid fails with NULL pointer inputs");
    ok (flux_subprocess_get_cmd (NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_get_cmd fails with NULL pointer inputs");
    ok (flux_subprocess_get_reactor (NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_get_reactor fails with NULL pointer inputs");
    ok (flux_subprocess_aux_set (NULL, "foo", "bar", NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_aux_set fails with NULL pointer inputs");
    ok (flux_subprocess_aux_get (NULL, "foo") == NULL
        && errno == EINVAL,
        "flux_subprocess_aux_get fails with NULL pointer inputs");

    flux_close (h);
}

void test_errors (flux_reactor_t *r)
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
    ok (flux_subprocess_stream_start (p, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_start returns EINVAL on bad stream");
    ok (flux_subprocess_stream_stop (p, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_stop returns EINVAL on bad stream");
    ok (flux_subprocess_stream_status (p, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_stream_status returns EINVAL on bad stream");
    ok (flux_subprocess_write (p, NULL, NULL, 0) < 0
        && errno == EINVAL,
        "flux_subprocess_write returns EINVAL on bad input");
    ok (flux_subprocess_write (p, "foo", "foo", 3) < 0
        && errno == EINVAL,
        "flux_subprocess_write returns EINVAL on bad stream");
    ok (flux_subprocess_close (p, "foo") < 0
        && errno == EINVAL,
        "flux_subprocess_close returns EINVAL on bad stream");
    ok (flux_subprocess_read (p, NULL, 0, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad input");
    ok (flux_subprocess_read (p, "foo", -1, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad stream");
    ok (flux_subprocess_read_line (p, "foo", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read_line returns EINVAL on bad stream");
    ok (flux_subprocess_read_trimmed_line (p, "foo", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read_trimmed_line returns EINVAL on bad stream");
    ok (flux_subprocess_read_stream_closed (p, "foo") < 0
        && errno == EINVAL,
        "flux_subprocess_read_stream_closed returns EINVAL on bad stream");
    ok (flux_subprocess_kill (p, 0) == NULL
        && errno == EINVAL,
        "flux_subprocess_kill returns EINVAL on illegal signum");
    ok (flux_subprocess_rank (p) < 0,
        "flux_subprocess_rank fails b/c subprocess is local");
    ok (flux_subprocess_fail_errno (p) < 0,
        "subprocess fail errno fails b/c subprocess not failed");
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

void output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    char cmpbuf[1024];
    int lenp = 0;
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
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        sprintf (cmpbuf, "%s:hi\n", stream);

        ok (!strcmp (ptr, cmpbuf),
            "flux_subprocess_read_line returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (lenp == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    (*counter)++;
}

void test_basic_stdout (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic_stderr (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-E", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stderr = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");
    ok ((flux_subprocess_pid (p) > (pid_t) 0),
        "flux_local_exec() started pid %ld", (pid_t) flux_subprocess_pid (p));

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 0, "stdout output callback called 0 times");
    ok (stderr_output_cb_count == 2, "stderr output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic_stdout_and_stderr (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb,
        .on_stderr = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 2, "stderr output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic_default_output (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void output_default_stream_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    char cmpbuf[1024];
    int lenp = 0;

    if (output_default_stream_cb_count == 0) {
        ptr = flux_subprocess_read_line (p, "stdout", &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", "stdout");

        sprintf (cmpbuf, "%s:hi\n", stream);

        ok (!strcmp (ptr, cmpbuf),
            "flux_subprocess_read_line returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (lenp == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", "stdout");

        ptr = flux_subprocess_read (p, "stdout", -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", "stdout");
    }

    output_default_stream_cb_count++;
}

void test_basic_stdin (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O",  NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (3, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "stdin", "hi", 2) == 2,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void output_no_newline_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    char cmpbuf[1024];
    int lenp = 0;
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
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read_line on %s read 0 lines", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read on %s read success", stream);

        sprintf (cmpbuf, "%s:hi", stream);

        ok (!strcmp (ptr, cmpbuf),
            "flux_subprocess_read returned correct data");
        /* 1 + 2 + 1 for ':', "hi" */
        ok (lenp == (strlen (stream) + 1 + 2),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    (*counter)++;
}

void test_basic_no_newline (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", "-n", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (6, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_no_newline_cb,
        .on_stderr = output_no_newline_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 2, "stderr output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void output_trimmed_line_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    char cmpbuf[1024];
    int lenp = 0;
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
        ptr = flux_subprocess_read_trimmed_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_trimmed_line on %s success", stream);

        sprintf (cmpbuf, "%s:hi", stream);

        ok (!strcmp (ptr, cmpbuf),
            "flux_subprocess_read_trimmed_line returned correct data");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    (*counter)++;
}

void test_basic_trimmed_line (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_trimmed_line_cb,
        .on_stderr = output_trimmed_line_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 2, "stderr output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void multiple_lines_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &multiple_lines_stdout_output_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &multiple_lines_stderr_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "foo\n"),
            "flux_subprocess_read_line returned correct data");
        ok (lenp == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else if ((*counter) == 1) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "bar\n"),
            "flux_subprocess_read_line returned correct data");
        ok (lenp == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else if ((*counter) == 2) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "bo\n"),
            "flux_subprocess_read_line returned correct data");
        ok (lenp == 3,
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    (*counter)++;
}

void test_basic_multiple_lines (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-E", "-n", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = multiple_lines_output_cb,
        .on_stderr = multiple_lines_output_cb
    };
    completion_cb_count = 0;
    multiple_lines_stdout_output_cb_count = 0;
    multiple_lines_stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "stdin", "foo\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "stdin", "bar\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "stdin", "bo\n", 3) == 3,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (multiple_lines_stdout_output_cb_count == 4, "stdout output callback called 4 times");
    ok (multiple_lines_stderr_output_cb_count == 4, "stderr output callback called 4 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void stdin_closed_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdin_closed_stdout_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stdin_closed_stderr_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ok (flux_subprocess_read_stream_closed (p, stream) > 0,
        "flux_subprocess_read_stream_closed saw EOF on %s", stream);

    ptr = flux_subprocess_read (p, stream, -1, &lenp);
    ok (ptr != NULL
        && lenp == 0,
        "flux_subprocess_read on %s read EOF", stream);

    (*counter)++;
}

void test_basic_stdin_closed (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-E", "-n", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = stdin_closed_cb,
        .on_stderr = stdin_closed_cb
    };
    completion_cb_count = 0;
    stdin_closed_stdout_cb_count = 0;
    stdin_closed_stderr_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdin_closed_stdout_cb_count == 1, "stdout output callback called 1 time");
    ok (stdin_closed_stderr_cb_count == 1, "stderr output callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void output_read_line_until_eof_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stderr_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ptr = flux_subprocess_getline (p, stream, &lenp);
    if ((*counter) == 0) {
        ok (ptr != NULL,
            "flux_subprocess_getline on %s success", stream);
        ok (!strcmp (ptr, "foo\n"),
            "flux_subprocess_getline returned correct data");
        ok (lenp == 4,
            "flux_subprocess_getline returned correct data len");
    }
    else if ((*counter) == 1) {
        ok (ptr != NULL,
            "flux_subprocess_getline on %s success", stream);
        ok (!strcmp (ptr, "bar"),
            "flux_subprocess_getline returned correct data");
        ok (lenp == 3,
            "flux_subprocess_getline returned correct data len");
    }
    else {
        ok (ptr != NULL,
            "flux_subprocess_getline on %s success", stream);
        ok (lenp == 0,
            "flux_subprocess_getline returned EOF");
    }

    (*counter)++;
}

void test_basic_read_line_until_eof (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-E", "-n", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_read_line_until_eof_cb,
        .on_stderr = output_read_line_until_eof_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "stdin", "foo\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "stdin", "bar", 3) == 3,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 3, "stdout output callback called 3 times");
    ok (stderr_output_cb_count == 3, "stderr output callback called 3 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void output_read_line_until_eof_error_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        ptr = flux_subprocess_getline (p, stream, &lenp);
        ok (!ptr && errno == EPERM,
            "flux_subprocess_getline returns EPERM "
            "on non line-buffered stream");

        /* drain whatever is in the buffer, we don't care about
         * contents for this test */
        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL && lenp > 0,
            "flux_subprocess_read on %s success", stream);
    }
    else {
        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }
    (*counter)++;
}

void test_basic_read_line_until_eof_error (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (3, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_read_line_until_eof_error_cb,
        .on_stderr = NULL
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_write_after_close (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-E", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (3, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "stdin", "hi", 2) == 2,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    ok (flux_subprocess_write (p, "stdin", "hi", 2) < 0
        && errno == EPIPE,
        "flux_subprocess_write failed with EPIPE after a close");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

#if 0
/* disable test.  libtap has an issue with fallthrough
 * stdout/stderr in forked process */
void test_flag_stdio_fallthrough (flux_reactor_t *r)
{
    char *av[] = { "echo", "foo", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (2, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r,
                         FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH,
                         cmd,
                         &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}
#endif

void test_flag_setpgrp (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops);
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
    const char *ptr;
    int lenp = 0;

    ok (!strcasecmp (stream, "stdout"),
        "env_passed_cb called with correct stream");

    if (!env_passed_cb_count) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strncmp (ptr, "FOOBAR=foobaz", 13),
            "environment variable FOOBAR in subprocess");
        ok (lenp == 14,
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
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
}

void test_kill (flux_reactor_t *r)
{
    char *av[]  = { "sleep", "1000", NULL };
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
    const char *ptr;
    int lenp;

    if (output_processes_cb_count == 0
        || output_processes_cb_count == 1) {
        ptr = flux_subprocess_read_trimmed_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_trimmed_line read valid data");

        if (ptr && lenp) {
            if (output_processes_cb_count == 0)
                parent_pid = atoi (ptr);
            else
                child_pid = atoi (ptr);
        }

        if (output_processes_cb_count == 1) {
            flux_future_t *f = NULL;
            f = flux_subprocess_kill (p, SIGTERM);
            ok (f != NULL, "flux_subprocess_kill returns future_t");
            /* ignore response, we're not going to wait for it */
            flux_future_destroy (f);
        }
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
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
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops);
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

void eof_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_eof_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stderr_eof_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ok (flux_subprocess_read_stream_closed (p, stream) > 0,
        "flux_subprocess_read_stream_closed saw EOF on %s", stream);

    ptr = flux_subprocess_read (p, stream, -1, &lenp);
    ok (ptr != NULL
        && lenp == 0,
        "flux_subprocess_read on %s read EOF", stream);

    (*counter)++;
}

void test_kill_eofs (flux_reactor_t *r)
{
    char *av[]  = { "sleep", "1000", NULL };
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
    char *av[] = { "sleep", "30", NULL };
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
    flux_subprocess_ref (p);

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_unref (p);

    /* normally this should fail, but we've increased the refcount so
     * subprocess should not be destroyed */
    ok ((tmp = flux_subprocess_aux_get (p, "extra")) != NULL,
        "flux_subprocess_aux_get success");
    ok (tmp == extra,
        "flux_subprocess_aux_get returned correct pointer");

    flux_subprocess_unref (p);
    flux_cmd_destroy (cmd);
}

void channel_fd_env_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;

    ok (!strcasecmp (stream, "stdout"),
        "channel_fd_env_cb called with correct stream");

    if (!channel_fd_env_cb_count) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strncmp (ptr, "FOO=", 4),
            "environment variable FOO created in subprocess");
        /* no length check, can't predict channel FD value */
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    channel_fd_env_cb_count++;
}

void test_channel_fd_env (flux_reactor_t *r)
{
    char *av[] = { "/usr/bin/env", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "FOO") == 0,
        "flux_cmd_add_channel success adding channel FOO");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = channel_fd_env_cb
    };
    completion_cb_count = 0;
    channel_fd_env_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (channel_fd_env_cb_count == 2, "channel fd callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void channel_in_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;

    ok (!strcasecmp (stream, "stdout"),
        "channel_in_cb called with correct stream");

    if (!channel_in_cb_count) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp == 7,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (ptr, "foobar\n", 7),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    channel_in_cb_count++;
}

void test_channel_fd_in (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-c", "TEST_CHANNEL", "-O", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = NULL,
        .on_stdout = channel_in_cb,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    channel_in_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "TEST_CHANNEL", "foobar", 6) == 6,
        "flux_subprocess_write success");

    /* close after we get output */

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (channel_in_cb_count == 2, "channel in callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void channel_in_and_out_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;

    ok (!strcasecmp (stream, "TEST_CHANNEL"),
        "channel_in_and_out_cb called with correct stream");

    if (!channel_in_and_out_cb_count) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp == 7,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (ptr, "foobaz\n", 7),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        /* no check of flux_subprocess_read_stream_closed(), we aren't
         * closing channel in test below */

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    channel_in_and_out_cb_count++;
}

void test_channel_fd_in_and_out (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-c", "TEST_CHANNEL", "-C", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = channel_in_and_out_cb,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    channel_in_and_out_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "TEST_CHANNEL", "foobaz", 6) == 6,
        "flux_subprocess_write success");

    /* don't call flux_subprocess_close() here, we'll race with data
     * coming back, call in callback */

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (channel_in_and_out_cb_count == 2, "channel out callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void channel_multiple_lines_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;

    ok (!strcasecmp (stream, "TEST_CHANNEL"),
        "channel_multiple_lines_cb called with correct stream");

    if (multiple_lines_channel_cb_count == 0) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "bob\n"),
            "flux_subprocess_read_line returned correct data");
    }
    else if (multiple_lines_channel_cb_count == 1) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "dan\n"),
            "flux_subprocess_read_line returned correct data");
    }
    else if (multiple_lines_channel_cb_count == 2) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", stream);

        ok (!strcmp (ptr, "jo\n"),
            "flux_subprocess_read_line returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        /* no check of flux_subprocess_read_stream_closed(), we aren't
         * closing channel in test below */

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    multiple_lines_channel_cb_count++;
}

void test_channel_multiple_lines (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-c", "TEST_CHANNEL", "-C", "-n", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = channel_multiple_lines_cb,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    multiple_lines_channel_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "TEST_CHANNEL", "bob\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "TEST_CHANNEL", "dan\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "TEST_CHANNEL", "jo\n", 3) == 3,
        "flux_subprocess_write success");

    /* don't call flux_subprocess_close() here, we'll race with data
     * coming back, call in callback */

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (multiple_lines_channel_cb_count == 4, "channel output callback called 4 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void channel_nul_terminate_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;

    if (!channel_nul_terminate_cb_count) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL
            && lenp == 7,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (ptr, "foobaz\n\0", 8),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    channel_nul_terminate_cb_count++;
}

void test_bufsize (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    ok (flux_cmd_setopt (cmd, "stdin_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set stdin_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "stdout_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set stdout_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "stderr_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set stderr_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "TEST_CHANNEL_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set TEST_CHANNEL_BUFSIZE success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = flux_standard_output,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_bufsize_error (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    ok (flux_cmd_setopt (cmd, "TEST_CHANNEL_BUFSIZE", "ABCD") == 0,
        "flux_cmd_setopt set TEST_CHANNEL_BUFSIZE success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = flux_standard_output,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bad bufsize input");

    flux_cmd_destroy (cmd);
}

/* Line buffering tests are technically racy.  If the stdout in the
 * test_multi_echo command occurs fast enough, a single on_stdout
 * callback could occur.  But hopefully by repeating the word "hi" a
 * lot of times, the probability of that occuring is zero if line
 * buffering is not working.
 *
 * I pick 2200 to make sure we output enough to surpass 4096 bytes of
 * output (i.e. 2200 * 2 bytes > 4096 bytes).
 */

void line_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        ptr = flux_subprocess_read_line (p, stream, &lenp);
        ok (ptr != NULL && lenp == 4401,
            "flux_subprocess_read_line read line correctly");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream) > 0,
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    (*counter)++;
}

void test_line_buffer_default (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-c", "2200", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = line_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* == 2 times means we got a single line and EOF */
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_line_buffer_enable (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-c", "2200", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "true") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = line_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* == 2 times means we got a single line and EOF */
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void count_output_cb (flux_subprocess_t *p, const char *stream)
{
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stderr_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    (void)flux_subprocess_read_line (p, stream, NULL);
    (*counter)++;
}

void test_line_buffer_disable (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-c", "2200", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = count_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* we care about greater than two, that it's not a single line and EOF */
    ok (stdout_output_cb_count > 2, "stdout output callback got more than 2 calls: %d", stdout_output_cb_count);
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_line_buffer_error (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "ABCD") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = flux_standard_output,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bad line_buffer input");

    flux_cmd_destroy (cmd);
}

void test_stream_start_stop_basic (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb,
        .on_stderr = output_cb,
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_stream_status (p, "stdout") > 0,
        "flux_subprocess_stream_status says stdout is started");
    ok (flux_subprocess_stream_status (p, "stderr") > 0,
        "flux_subprocess_stream_status says stderr is started");

    ok (!flux_subprocess_stream_stop (p, "stdout"),
        "flux_subprocess_stream_stop on stdout success");
    ok (!flux_subprocess_stream_stop (p, "stderr"),
        "flux_subprocess_stream_stop on stderr success");

    ok (flux_subprocess_stream_status (p, "stdout") == 0,
        "flux_subprocess_stream_status says stdout is stopped");
    ok (flux_subprocess_stream_status (p, "stderr") == 0,
        "flux_subprocess_stream_status says stderr is stopped");

    ok (!flux_subprocess_stream_start (p, "stdout"),
        "flux_subprocess_stream_start on stdout success");
    ok (!flux_subprocess_stream_start (p, "stderr"),
        "flux_subprocess_stream_start on stderr success");

    ok (flux_subprocess_stream_status (p, "stdout") > 0,
        "flux_subprocess_stream_status says stdout is started");
    ok (flux_subprocess_stream_status (p, "stderr") > 0,
        "flux_subprocess_stream_status says stderr is started");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    ok (stderr_output_cb_count == 2, "stderr output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void start_stdout_after_stderr_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;
    int *len_counter;

    if (!strcasecmp (stream, "stdout")) {
        counter = &stdout_output_cb_count;
        len_counter = &stdout_output_cb_len_count;
    }
    else if (!strcasecmp (stream, "stderr")) {
        counter = &stderr_output_cb_count;
        len_counter = &stderr_output_cb_len_count;
    }
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ptr = flux_subprocess_read (p, stream, -1, &lenp);
    (*counter)++;
    (*len_counter)+= lenp;

    if (ptr && lenp && (*len_counter) == 10001) {
        if (!strcmp (stream, "stderr")) {
            ok (stdout_output_cb_count == 0
                && stdout_output_cb_len_count == 0,
                "received all stderr data and stdout output is still 0");
            ok (!flux_subprocess_stream_start (p, "stdout"),
                "flux_subprocess_stream_start on stdout success");
        }
    }
}

/* How this tests works is we output "hi" alot of times without line
 * buffering on both stdout and stderr.  After starting the
 * subprocess, we immediately disable the stdout stream.  The goal is
 * we get all the stderr via callback, then re-enable the stdout
 * stream, and get the rest of the stdout.
 *
 * This test is racy, as its always possible stderr just arrives
 * before stdout under normal circumstances, but the probability of
 * that occuring is low given how much we output.
 */
void test_stream_start_stop_initial_stop (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-E", "-c", "5000", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (6, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    ok (flux_cmd_setopt (cmd, "stderr_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stderr_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = start_stdout_after_stderr_cb,
        .on_stderr = start_stdout_after_stderr_cb,
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    stdout_output_cb_len_count = 0;
    stderr_output_cb_len_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (!flux_subprocess_stream_stop (p, "stdout"),
        "flux_subprocess_stream_stop on stdout success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* potential for == 2, b/c could all be buffered before stdout
     * callback is re-started */
    ok (stdout_output_cb_count >= 2, "stdout output callback called >= 2 times: %d",
        stdout_output_cb_count);
    /* we would hope stderr is called > 2 times, but there's
     * potentially racy behavior and its only called 2 times.  This
     * isn't seen in practice. */
    ok (stderr_output_cb_count > 2, "stderr output callback called > 2 times: %d",
        stderr_output_cb_count);
    ok (stdout_output_cb_len_count == 10001, "stdout_output_cb_len_count is 10001");
    ok (stderr_output_cb_len_count == 10001, "stderr_output_cb_len_count is 10001");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void mid_stop_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    flux_subprocess_t *p = arg;
    ok (stdout_output_cb_count == 1,
        "stdout callback has not been called since timer activated");
    ok (!flux_subprocess_stream_start (p, "stdout"),
        "flux_subprocess_stream_start on stdout success");
    timer_cb_count++;
    flux_watcher_stop (w);
}

void mid_stop_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp = 0;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ptr = flux_subprocess_read (p, stream, -1, &lenp);
    if (stdout_output_cb_count == 0) {
        flux_watcher_t *tw = NULL;
        ok (ptr && lenp > 0,
            "flux_subprocess_read read data on stdout: %d", lenp);
        ok (!flux_subprocess_stream_stop (p, "stdout"),
            "flux_subprocess_stream_stop on stdout success");
        ok ((tw = flux_subprocess_aux_get (p, "tw")) != NULL,
            "flux_subprocess_aux_get timer success");
        flux_watcher_start (tw);
    }
    else if (stdout_output_cb_count == 1) {
        ok (ptr && lenp > 0,
            "flux_subprocess_read read data on stdout: %d", lenp);
        ok (timer_cb_count == 1,
            "next stdout callback called after time callback called");
    }
    (*counter)++;
}

/* How this tests works is we output "hi" alot of times without line
 * buffering on stdout.  After the first callback, we stop the output
 * stream, and setup a timer.  For a bit of time, we should see no
 * more stdout, and after the timer expires, we'll re-eanble the
 * stdout stream.
 *
 * This test is racy, as its always possible stdout is just delayed,
 * but the probability of that occuring is low given how much we
 * output.
 */
void test_stream_start_stop_mid_stop (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-c", "5000", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    flux_watcher_t *tw = NULL;

    ok ((cmd = flux_cmd_create (5, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = mid_stop_cb,
        .on_stderr = NULL,
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    timer_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok ((tw = flux_timer_watcher_create (r, 2.0, 0.0,
                                         mid_stop_timer_cb, p)) != NULL,
        "flux_timer_watcher_create success");

    ok (!flux_subprocess_aux_set (p, "tw", tw, NULL),
        "flux_subprocess_aux_set timer success");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* could be == to 3 if output occurs fast enough, but chances are
     * it'll be > 3 */
    ok (stdout_output_cb_count >= 3, "stdout output callback called >= 3 times: %d",
        stdout_output_cb_count);
    ok (stderr_output_cb_count == 0, "stderr output callback called 0 times");
    ok (timer_cb_count == 1, "timer callback called 1 time");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    flux_watcher_destroy (tw);
}

/* How this tests works is we output "hi" alot of times without line
 * buffering on both stdout and stderr.  We initially disable stdout
 * callbacks via STREAM_STOP.  So the goal is we get all the
 * stderr via callback and no stdout via callback.  After we get all
 * the stderr, we can re-enable stdout and get all of that data.
 *
 * This test is racy, as its always possible stderr just arrives
 * before stdout under normal circumstances, but the probability of
 * that occuring is low given how much we output.
  */
void test_stream_stop_enable (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-E", "-c", "5000", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (6, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    ok (flux_cmd_setopt (cmd, "stderr_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stderr_LINE_BUFFER success");

    ok (flux_cmd_setopt (cmd, "stdout_STREAM_STOP", "true") == 0,
        "flux_cmd_setopt set stdout_STREAM_STOP success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = start_stdout_after_stderr_cb,
        .on_stderr = start_stdout_after_stderr_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    stdout_output_cb_len_count = 0;
    stderr_output_cb_len_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    /* potential for == 2, b/c could all be buffered before stdout
     * callback is started */
    ok (stdout_output_cb_count >= 2, "stdout output callback called >= 2 times: %d",
        stdout_output_cb_count);
    /* we would hope stderr is called > 2 times, but there's
     * potentially racy behavior and its only called 2 times.  This
     * isn't seen in practice. */
    ok (stderr_output_cb_count > 2, "stderr output callback called > 2 times: %d",
        stderr_output_cb_count);
    ok (stdout_output_cb_len_count == 10001, "stdout_output_cb_len_count is 10001");
    ok (stderr_output_cb_len_count == 10001, "stderr_output_cb_len_count is 10001");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

/* disabled the test should work like a normal test */
void test_stream_stop_disable (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_multi_echo", "-O", "-E", "-c", "5000", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (6, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stdout_LINE_BUFFER success");

    ok (flux_cmd_setopt (cmd, "stderr_LINE_BUFFER", "false") == 0,
        "flux_cmd_setopt set stderr_LINE_BUFFER success");

    ok (flux_cmd_setopt (cmd, "stdout_STREAM_STOP", "false") == 0,
        "flux_cmd_setopt set stdout_STREAM_STOP success");

    ok (flux_cmd_setopt (cmd, "stderr_STREAM_STOP", "false") == 0,
        "flux_cmd_setopt set stderr_STREAM_STOP success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = count_output_cb,
        .on_stderr = count_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count > 2, "stdout output callback called > 2 times: %d",
        stdout_output_cb_count);
    ok (stderr_output_cb_count > 2, "stderr output callback called > 2 times: %d",
        stderr_output_cb_count);
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_stream_stop_error (flux_reactor_t *r)
{
    char *av[] = { "true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_STREAM_STOP", "ABCD") == 0,
        "flux_cmd_setopt set stdout_STREAM_STOP success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = flux_standard_output,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bad stream_stop input");

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

int main (int argc, char *argv[])
{
    flux_reactor_t *r;
    int start_fdcount, end_fdcount;

    plan (NO_PLAN);

    // Create shared reactor for all tests
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "flux_reactor_create");

    start_fdcount = fdcount ();

    diag ("basic");
    test_basic (r);
    diag ("basic_fail");
    test_basic_fail (r);
    diag ("basic_errors");
    test_basic_errors (r);
    diag ("errors");
    test_errors (r);
    diag ("basic_stdout");
    test_basic_stdout (r);
    diag ("basic_stderr");
    test_basic_stderr (r);
    diag ("basic_stdout_and_stderr");
    test_basic_stdout_and_stderr (r);
    diag ("basic_default_output");
    test_basic_default_output (r);
    diag ("basic_stdin");
    test_basic_stdin (r);
    diag ("basic_no_newline");
    test_basic_no_newline (r);
    diag ("basic_trimmed_line");
    test_basic_trimmed_line (r);
    diag ("basic_multiple_lines");
    test_basic_multiple_lines (r);
    diag ("basic_stdin_closed");
    test_basic_stdin_closed (r);
    diag ("basic_read_line_until_eof");
    test_basic_read_line_until_eof (r);
    diag ("basic_read_line_until_eof_error");
    test_basic_read_line_until_eof_error (r);
    diag ("write_after_close");
    test_write_after_close (r);
    diag ("env_passed");
    test_env_passed (r);
#if 0
    diag ("flag_stdio_fallthrough");
    test_flag_stdio_fallthrough (r);
#endif
    diag ("flag_setpgrp");
    test_flag_setpgrp (r);
    diag ("flag_fork_exec");
    test_flag_fork_exec (r);
    diag ("kill");
    test_kill (r);
    diag ("kill_setpgrp");
    test_kill_setpgrp (r);
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
    diag ("channel_fd_env");
    test_channel_fd_env (r);
    diag ("channel_fd_in");
    test_channel_fd_in (r);
    diag ("channel_fd_in_and_out");
    test_channel_fd_in_and_out (r);
    diag ("channel_multiple_lines");
    test_channel_multiple_lines (r);
    diag ("bufsize");
    test_bufsize (r);
    diag ("bufsize_error");
    test_bufsize_error (r);
    diag ("line_buffer_default");
    test_line_buffer_default (r);
    diag ("line_buffer_enable");
    test_line_buffer_enable (r);
    diag ("line_buffer_disable");
    test_line_buffer_disable (r);
    diag ("line_buffer_error");
    test_line_buffer_error (r);
    diag ("stream_start_stop_basic");
    test_stream_start_stop_basic (r);
    diag ("stream_start_stop_initial_stop");
    test_stream_start_stop_initial_stop (r);
    diag ("stream_start_stop_mid_stop");
    test_stream_start_stop_mid_stop (r);
    diag ("stream_stop_enable");
    test_stream_stop_enable (r);
    diag ("stream_stop_disable");
    test_stream_stop_disable (r);
    diag ("stream_stop_error");
    test_stream_stop_error (r);
    diag ("pre_exec_hook");
    test_pre_exec_hook (r);
    diag ("post_fork_hook");
    test_post_fork_hook (r);
    diag ("test_destroy_in_completion");
    test_destroy_in_completion (r);

    end_fdcount = fdcount ();

    ok (start_fdcount == end_fdcount,
        "no file descriptors leaked");

    flux_reactor_destroy (r);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
