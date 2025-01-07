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

extern char **environ;

int completion_cb_count;
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
        "subprocess exit code is 0, got %d", flux_subprocess_exit_code (p));
    completion_cb_count++;
}

void channel_fd_env_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    ok (!strcasecmp (stream, "stdout"),
        "channel_fd_env_cb called with correct stream");

    if (!channel_fd_env_cb_count) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (strstarts (buf, "FOO="),
            "environment variable FOO created in subprocess");
        /* no length check, can't predict channel FD value */
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
    const char *buf = NULL;
    int len;

    ok (!strcasecmp (stream, "stdout"),
        "channel_in_cb called with correct stream");

    if (!channel_in_cb_count) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len == 7
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (buf, "foobar\n", 7),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
        .on_stderr = subprocess_standard_output
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
    const char *buf = NULL;
    int len;

    ok (!strcasecmp (stream, "TEST_CHANNEL"),
        "channel_in_and_out_cb called with correct stream");

    if (!channel_in_and_out_cb_count) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len == 7
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (buf, "foobaz\n", 7),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        /* no check of flux_subprocess_read_stream_closed(), we aren't
         * closing channel in test below */

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
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
    const char *buf = NULL;
    int len;

    ok (!strcasecmp (stream, "TEST_CHANNEL"),
        "channel_multiple_lines_cb called with correct stream");

    if (multiple_lines_channel_cb_count == 0) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "bob\n"),
            "flux_subprocess_read_line returned correct data");
    }
    else if (multiple_lines_channel_cb_count == 1) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "dan\n"),
            "flux_subprocess_read_line returned correct data");
    }
    else if (multiple_lines_channel_cb_count == 2) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "jo\n"),
            "flux_subprocess_read_line returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        /* no check of flux_subprocess_read_stream_closed(), we aren't
         * closing channel in test below */

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
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
    const char *buf = NULL;
    int len;

    if (!channel_nul_terminate_cb_count) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len == 7
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (!memcmp (buf, "foobaz\n\0", 8),
            "read on channel returned correct data");

        ok (flux_subprocess_close (p, "TEST_CHANNEL") == 0,
            "flux_subprocess_close success");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
        .on_channel_out = subprocess_standard_output,
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
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
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_channel_out = subprocess_standard_output,
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
    };

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    ok (flux_cmd_setopt (cmd, "TEST_CHANNEL_BUFSIZE", "ABCD") == 0,
        "flux_cmd_setopt set TEST_CHANNEL_BUFSIZE success");

    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bad bufsize input");

    flux_cmd_destroy (cmd);

    ok ((cmd = flux_cmd_create (1, av, NULL)) != NULL, "flux_cmd_create");

    ok (flux_cmd_add_channel (cmd, "TEST_CHANNEL") == 0,
        "flux_cmd_add_channel success adding channel TEST_CHANNEL");

    ok (flux_cmd_setopt (cmd, "TEST_CHANNEL_BUFSIZE", "0") == 0,
        "flux_cmd_setopt set TEST_CHANNEL_BUFSIZE success");

    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p == NULL
        && errno == EINVAL,
        "flux_local_exec fails with EINVAL due to bufsize zero");

    flux_cmd_destroy (cmd);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *r;
    int start_fdcount, end_fdcount;

    plan (NO_PLAN);

    start_fdcount = fdcount ();

    // Create shared reactor for all tests
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "flux_reactor_create");

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
