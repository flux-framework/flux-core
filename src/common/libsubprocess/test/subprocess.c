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

extern char **environ;

int completion_cb_count;
int completion_fail_cb_count;
int stdout_output_cb_count;
int stderr_output_cb_count;
int output_default_stream_cb_count;
int multiple_lines_stdout_output_cb_count;
int multiple_lines_stderr_output_cb_count;
int env_passed_cb_count;
int completion_sigterm_cb_count;
int output_processes_cb_count;
int parent_pid;
int child_pid;
int stdout_eof_cb_count;
int stderr_eof_cb_count;
int state_change_cb_count;
int channel_fd_env_cb_count;
int channel_in_cb_count;
int channel_in_and_out_cb_count;
int multiple_lines_channel_cb_count;
int channel_nul_terminate_cb_count;

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
        "subprocess exit code is 0");
    completion_cb_count++;
}

void test_basic (flux_reactor_t *r)
{
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd, *cmd2;
    flux_reactor_t *r2;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    char *av[] = { "/bin/false", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_fail_cb
    };
    completion_fail_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    flux_t *h_hack = (flux_t *)0x12345678;
    char *avgood[] = { "/bin/true", NULL };
    char *avbad[] = { NULL };
    flux_cmd_t *cmd;

    ok (!flux_subprocess_server_start (NULL, NULL, NULL, 0)
        && errno == EINVAL,
        "flux_subprocess_server_start fails with NULL pointer inputs");
    ok (flux_subprocess_server_terminate_by_uuid (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_subprocess_server_terminate_by_uuid fails with NULL pointer inputs");
    ok (flux_subprocess_server_subprocesses_kill (NULL, 0, 0.) < 0
        && errno == EINVAL,
        "flux_subprocess_server_subprocesses_kill fails with NULL pointer inputs");

    ok (flux_exec (NULL, 0, NULL, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_exec fails with NULL pointer inputs");
    ok (flux_local_exec (NULL, 0, NULL, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with NULL pointer inputs");
    ok (flux_local_exec (r, 1234, NULL, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_local_exec fails with invalid flag");
    ok (flux_rexec (NULL, 0, 0, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with NULL pointer inputs");
    ok (flux_rexec (h_hack, 0, 1, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with invalid flag");

    ok ((cmd = flux_cmd_create (0, avbad, NULL)) != NULL,
        "flux_cmd_create with 0 args works");
    ok (flux_rexec (h_hack, 0, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with zero args");
    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, avgood, NULL)) != NULL,
        "flux_cmd_create with 0 args works");
    ok (flux_rexec (h_hack, -10, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with invalid rank");
    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, avgood, NULL)) != NULL,
        "flux_cmd_create with 0 args works");
    ok (flux_rexec (h_hack, 0, 0, cmd, NULL) == NULL
        && errno == EINVAL,
        "flux_rexec fails with cmd with no cwd");
    flux_cmd_destroy (cmd);

    ok (flux_subprocess_write (NULL, "STDIN", "foo", 3) < 0
        && errno == EINVAL,
        "flux_subprocess_write fails with NULL pointer inputs");
    ok (flux_subprocess_close (NULL, "STDIN") < 0
        && errno == EINVAL,
        "flux_subprocess_close fails with NULL pointer inputs");
    ok (flux_subprocess_read (NULL, "STDOUT", -1, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read fails with NULL pointer inputs");
    ok (flux_subprocess_read_line (NULL, "STDOUT", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read_line fails with NULL pointer inputs");
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
}

void test_errors (flux_reactor_t *r)
{
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    ok (flux_subprocess_read (p, NULL, 0, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad input");
    ok (flux_subprocess_read (p, "foo", -1, NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad stream");
    ok (flux_subprocess_read_line (p, "foo", NULL) == NULL
        && errno == EINVAL,
        "flux_subprocess_read returns EINVAL on bad stream");
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

    ok (flux_subprocess_write (p, NULL, "foo", 3) < 0
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

    if (!strcasecmp (stream, "STDOUT"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "STDERR"))
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
        ptr = flux_subprocess_read_line (p, NULL, &lenp);
        ok (ptr != NULL
            && lenp > 0,
            "flux_subprocess_read_line on %s success", "STDOUT");

        sprintf (cmpbuf, "%s:hi\n", stream);

        ok (!strcmp (ptr, cmpbuf),
            "flux_subprocess_read_line returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (lenp == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ptr = flux_subprocess_read (p, NULL, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", "STDOUT");
    }

    output_default_stream_cb_count++;
}

void test_basic_stdout_default_stream (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_default_stream_cb
    };
    completion_cb_count = 0;
    output_default_stream_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (output_default_stream_cb_count == 2, "stdout output default stream callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic_stdin (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "STDIN", "hi", 2) == 2,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "STDIN") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 2, "stdout output callback called 2 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void test_basic_stdin_default_stream (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "-E", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, NULL, "hi", 2) == 2,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, NULL) == 0,
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

    if (!strcasecmp (stream, "STDOUT"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "STDERR"))
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
        ok (flux_subprocess_read_eof_reached (p, stream) > 0,
            "flux_subprocess_read_eof_reached saw EOF on %s", stream);

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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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

    if (!strcasecmp (stream, "STDOUT"))
        counter = &stdout_output_cb_count;
    else if (!strcasecmp (stream, "STDERR"))
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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

    if (!strcasecmp (stream, "STDOUT"))
        counter = &multiple_lines_stdout_output_cb_count;
    else if (!strcasecmp (stream, "STDERR"))
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "STDIN", "foo\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "STDIN", "bar\n", 4) == 4,
        "flux_subprocess_write success");

    ok (flux_subprocess_write (p, "STDIN", "bo\n", 3) == 3,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "STDIN") == 0,
        "flux_subprocess_close success");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (multiple_lines_stdout_output_cb_count == 4, "stdout output callback called 4 times");
    ok (multiple_lines_stderr_output_cb_count == 4, "stderr output callback called 4 times");
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    ok (flux_subprocess_write (p, "STDIN", "hi", 2) == 2,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "STDIN") == 0,
        "flux_subprocess_close success");

    ok (flux_subprocess_write (p, "STDIN", "hi", 2) < 0
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
                         &ops,
                         NULL);
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
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops, NULL);
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

    ok (!strcasecmp (stream, "STDOUT"),
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    char *av[]  = { "/bin/sleep", "1000", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_future_t *f = NULL;

    ok ((cmd = flux_cmd_create (2, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_sigterm_cb
    };
    completion_sigterm_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops, NULL);
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

    if (!strcasecmp (stream, "STDOUT"))
        counter = &stdout_eof_cb_count;
    else if (!strcasecmp (stream, "STDERR"))
        counter = &stderr_eof_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    ptr = flux_subprocess_read (p, stream, -1, &lenp);
    ok (ptr != NULL
        && lenp == 0,
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_change_cb
    };
    completion_cb_count = 0;
    state_change_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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

void test_state_strings (void)
{
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_INIT), "Init"),
        "flux_subprocess_state_string returns correct string");
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_RUNNING), "Running"),
        "flux_subprocess_state_string returns correct string");
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_EXITED), "Exited"),
        "flux_subprocess_state_string returns correct string");
    ok (!strcasecmp (flux_subprocess_state_string (FLUX_SUBPROCESS_EXEC_FAILED), "Exec Failed"),
        "flux_subprocess_state_string returns correct string");
    ok (!flux_subprocess_state_string (100),
        "flux_subprocess_state_string returns NULL on bad state");
}

void test_exec_fail (flux_reactor_t *r)
{
    char *av_eacces[]  = { "/", NULL };
    char *av_enoent[]  = { "/usr/bin/foobarbaz", NULL };
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av_eacces, NULL)) != NULL, "flux_cmd_create");

    p = flux_local_exec (r, 0, cmd, NULL, NULL);
    ok (p == NULL
        && errno == EACCES,
        "flux_local_exec failed with EACCES");

    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, av_enoent, NULL)) != NULL, "flux_cmd_create");

    p = flux_local_exec (r, 0, cmd, NULL, NULL);
    ok (p == NULL
        && errno == ENOENT,
        "flux_local_exec failed with ENOENT");

    flux_cmd_destroy (cmd);
}

void test_context (flux_reactor_t *r)
{
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    char *extra = "mydata";
    char *tmp;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    char *extra = "mydata";
    char *tmp;

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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

    ok (!strcasecmp (stream, "STDOUT"),
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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

    ok (!strcasecmp (stream, "STDOUT"),
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
        ptr = flux_subprocess_read (p, stream, -1, &lenp);
        ok (ptr != NULL
            && lenp == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }

    channel_nul_terminate_cb_count++;
}

void test_bufsize (flux_reactor_t *r)
{
    char *av[] = { "/bin/true", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (1, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    ok (flux_cmd_setopt (cmd, "STDIN_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set STDIN_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "STDOUT_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set STDOUT_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "STDERR_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set STDERR_BUFSIZE success");

    ok (flux_cmd_setopt (cmd, "TEST_CHANNEL_BUFSIZE", "1024") == 0,
        "flux_cmd_setopt set TEST_CHANNEL_BUFSIZE success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = flux_standard_output,
        .on_stdout = flux_standard_output,
        .on_stderr = flux_standard_output
    };
    completion_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
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
    char *av[] = { "/bin/true", NULL };
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
    p = flux_local_exec (r, 0, cmd, &ops, NULL);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bad bufsize input");

    flux_cmd_destroy (cmd);
}

void shmem_hook_cb (flux_subprocess_t *p, void *arg)
{
    int *shmem_count = arg;
    (*shmem_count)++;
}

void test_pre_exec_hook (flux_reactor_t *r)
{
    char *av[] = { "/bin/true", NULL };
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
    p = flux_local_exec (r, FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH, cmd, &ops, &hooks);
    ok (p != NULL, "flux_local_exec");

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
    char *av[] = { "/bin/true", NULL };
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
    p = flux_local_exec (r, 0, cmd, &ops, &hooks);
    ok (p != NULL, "flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (hook_count == 1, "post_fork hook cb called 1 time");
    flux_subprocess_destroy (p);
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
    diag ("basic_stdout_default_stream");
    test_basic_stdout_default_stream (r);
    diag ("basic_stdin");
    test_basic_stdin (r);
    diag ("basic_stdin_default_stream");
    test_basic_stdin_default_stream (r);
    diag ("basic_no_newline");
    test_basic_no_newline (r);
    diag ("basic_trimmed_line");
    test_basic_trimmed_line (r);
    diag ("basic_multiple_lines");
    test_basic_multiple_lines (r);
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
    diag ("kill");
    test_kill (r);
    diag ("kill_setpgrp");
    test_kill_setpgrp (r);
    diag ("kill_eofs");
    test_kill_eofs (r);
    diag ("state_change");
    test_state_change (r);
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
    diag ("pre_exec_hook");
    test_pre_exec_hook (r);
    diag ("post_fork_hook");
    test_post_fork_hook (r);

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
