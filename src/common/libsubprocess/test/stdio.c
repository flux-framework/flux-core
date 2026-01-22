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
int timer_cb_count;
int credit_cb_count;
char inputbuf[1024];
int inputbuf_index;
int inputbuf_len;
char outputbuf[1024];
int outputbuf_len;

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

void output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    char cmpbuf[1024];
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
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        sprintf (cmpbuf, "%s:hi\n", stream);

        ok (streq (buf, cmpbuf),
            "flux_subprocess_read_line returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (len == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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

void output_no_readline_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    char cmpbuf[1024];
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

    len = flux_subprocess_read (p, stream, &buf);
    ok (len >= 0,
        "flux_subprocess_read on %s success", stream);

    if (len > 0) {
        memcpy (outputbuf + outputbuf_len, buf, len);
        outputbuf_len += len;
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        sprintf (cmpbuf, "%s:hi\n", stream);
        ok (streq (outputbuf, cmpbuf),
            "flux_subprocess_read returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (outputbuf_len == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read returned correct amount of data");
    }

    (*counter)++;
}

/* use flux_subprocess_read() instead of flux_subprocess_read_line() */
void test_basic_stdout_no_readline (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "hi", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (4, av, environ)) != NULL, "flux_cmd_create");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_no_readline_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    stderr_output_cb_count = 0;
    memset (outputbuf, '\0', sizeof (outputbuf));
    outputbuf_len = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count >= 2, "stdout output callback called atleast 2 times");
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

void output_default_stream_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    char cmpbuf[1024];
    int len;

    if (output_default_stream_cb_count == 0) {
        len = flux_subprocess_read_line (p, "stdout", &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", "stdout");

        sprintf (cmpbuf, "%s:hi\n", stream);

        ok (streq (buf, cmpbuf),
            "flux_subprocess_read_line returned correct data");
        /* 1 + 2 + 1 for ':', "hi", '\n' */
        ok (len == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", "stdout");

        len = flux_subprocess_read (p, "stdout", &buf);
        ok (len == 0,
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
    const char *buf = NULL;
    char cmpbuf[1024];
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
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len == 0,
            "flux_subprocess_read_line on %s read 0 lines", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read on %s read success", stream);

        sprintf (cmpbuf, "%s:hi", stream);

        ok (streq (buf, cmpbuf),
            "flux_subprocess_read returned correct data");
        /* 1 + 2 for ':', "hi" */
        ok (len == (strlen (stream) + 1 + 2),
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
    const char *buf = NULL;
    char cmpbuf[1024];
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
        len = flux_subprocess_read_trimmed_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_trimmed_line on %s success", stream);

        sprintf (cmpbuf, "%s:hi", stream);

        ok (streq (buf, cmpbuf),
            "flux_subprocess_read_trimmed_line returned correct data");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
    const char *buf = NULL;
    int len;
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
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "foo\n"),
            "flux_subprocess_read_line returned correct data");
        ok (len == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else if ((*counter) == 1) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "bar\n"),
            "flux_subprocess_read_line returned correct data");
        ok (len == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else if ((*counter) == 2) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "bo\n"),
            "flux_subprocess_read_line returned correct data");
        ok (len == 3,
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdin_closed_stdout_cb_count;
    else if (!strcasecmp (stream, "stderr"))
        counter = &stdin_closed_stderr_cb_count;
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

    len = flux_subprocess_getline (p, stream, &buf);
    if ((*counter) == 0) {
        ok (len > 0,
            "flux_subprocess_getline on %s success", stream);
        ok (streq (buf, "foo\n"),
            "flux_subprocess_getline returned correct data");
        ok (len == 4,
            "flux_subprocess_getline returned correct data len");
    }
    else if ((*counter) == 1) {
        ok (len > 0,
            "flux_subprocess_getline on %s success", stream);
        ok (streq (buf, "bar"),
            "flux_subprocess_getline returned correct data");
        ok (len == 3,
            "flux_subprocess_getline returned correct data len");
    }
    else {
        ok (len == 0,
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
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        len = flux_subprocess_getline (p, stream, &buf);
        ok (len < 0 && errno == EPERM,
            "flux_subprocess_getline returns EPERM "
            "on non line-buffered stream");

        /* drain whatever is in the buffer, we don't care about
         * contents for this test */
        len = flux_subprocess_read (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read on %s success", stream);
    }
    else {
        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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

void test_write_enospc (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-E", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (3, av, environ)) != NULL, "flux_cmd_create");
    ok (flux_cmd_setopt (cmd, "stdin_BUFSIZE", "5") == 0,
        "set stdin buffer size to 5 bytes");

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

    ok (flux_subprocess_write (p, "stdin", "hi\n", 3) == 3,
        "flux_subprocess_write success");
    ok (flux_subprocess_write (p, "stdin", "hello\n", 6) < 0
        && errno == ENOSPC,
        "flux_subprocess_write returns ENOSPC if buffer exceeded");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

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

/* Line buffering tests are technically racy.  If the stdout in the
 * test_multi_echo command occurs fast enough, a single on_stdout
 * callback could occur.  But hopefully by repeating the word "hi" a
 * lot of times, the probability of that occurring is zero if line
 * buffering is not working.
 *
 * I pick 2200 to make sure we output enough to surpass 4096 bytes of
 * output (i.e. 2200 * 2 bytes > 4096 bytes).
 */

void line_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    if ((*counter) == 0) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len == 4401
            && buf != NULL,
            "flux_subprocess_read_line read line correctly");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
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
        .on_channel_out = subprocess_standard_output,
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
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
    const char *buf = NULL;
    int len;
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

    len = flux_subprocess_read (p, stream, &buf);
    (*counter)++;
    (*len_counter)+= len;

    if (len > 0 && buf != NULL && (*len_counter) == 10001) {
        if (streq (stream, "stderr")) {
            ok (stdout_output_cb_count == 0
                && stdout_output_cb_len_count == 0,
                "received all stderr data and stdout output is still 0");
            flux_subprocess_stream_start (p, "stdout");
            diag ("flux_subprocess_stream_start on stdout");
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
 * that occurring is low given how much we output.
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

    flux_subprocess_stream_stop (p, "stdout");
    diag ("flux_subprocess_stream_stop on stdout");

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
    flux_subprocess_stream_start (p, "stdout");
    diag ("flux_subprocess_stream_start on stdout");
    timer_cb_count++;
    flux_watcher_stop (w);
}

void mid_stop_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;
    int *counter;

    if (!strcasecmp (stream, "stdout"))
        counter = &stdout_output_cb_count;
    else {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    len = flux_subprocess_read (p, stream, &buf);
    if (stdout_output_cb_count == 0) {
        flux_watcher_t *tw = NULL;
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read read data on stdout: %d", len);
        flux_subprocess_stream_stop (p, "stdout");
        diag ("flux_subprocess_stream_stop on stdout");
        ok ((tw = flux_subprocess_aux_get (p, "tw")) != NULL,
            "flux_subprocess_aux_get timer success");
        flux_watcher_start (tw);
    }
    else if (stdout_output_cb_count == 1) {
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read read data on stdout: %d", len);
        ok (timer_cb_count == 1,
            "next stdout callback called after time callback called");
    }
    (*counter)++;
}

/* How this tests works is we output "hi" alot of times without line
 * buffering on stdout.  After the first callback, we stop the output
 * stream, and setup a timer.  For a bit of time, we should see no
 * more stdout, and after the timer expires, we'll re-enable the
 * stdout stream.
 *
 * This test is racy, as its always possible stdout is just delayed,
 * but the probability of that occurring is low given how much we
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

void overflow_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    if (strcasecmp (stream, "stdout") != 0) {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    /* first callback should return "0123" for 4 byte buffer.
     * second callback should return "456\n" in 4 byte buffer
     */
    if (stdout_output_cb_count == 0) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "0123"),
            "flux_subprocess_read_line returned correct data");
        ok (len == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else if (stdout_output_cb_count == 1) {
        len = flux_subprocess_read_line (p, stream, &buf);
        ok (len > 0
            && buf != NULL,
            "flux_subprocess_read_line on %s success", stream);

        ok (streq (buf, "456\n"),
            "flux_subprocess_read_line returned correct data");
        ok (len == 4,
            "flux_subprocess_read_line returned correct data len");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &buf);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
    }
    stdout_output_cb_count++;
}

/* Set buffer size to 4 and have 7 bytes of output (8 including newline) */
void test_long_line (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "0123456", NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;

    ok ((cmd = flux_cmd_create (3, av, environ)) != NULL, "flux_cmd_create");

    ok (flux_cmd_setopt (cmd, "stdout_BUFSIZE", "4") == 0,
        "flux_cmd_setopt set stdout_BUFSIZE success");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = overflow_output_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count == 3, "stdout output callback called 3 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void credit_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *buf = NULL;
    int len;

    if (strcasecmp (stream, "stdout")) {
        ok (false, "unexpected stream %s", stream);
        return;
    }

    len = flux_subprocess_read (p, stream, &buf);
    ok (len >= 0,
        "flux_subprocess_read on %s success", stream);

    if (len > 0) {
        memcpy (outputbuf + outputbuf_len, buf, len);
        outputbuf_len += len;
    }
    else {
        char cmpbuf[1024];

        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        sprintf (cmpbuf, "abcdefghijklmnopqrstuvwxyz0123456789\n");
        ok (streq (outputbuf, cmpbuf),
            "flux_subprocess_read returned correct data: %s", outputbuf);
        /* 26 (ABCs) + 10 (1-10) + 1 for `\n' */
        ok (outputbuf_len == (26 + 10 + 1),
            "flux_subprocess_read returned correct amount of data: %d",
            outputbuf_len);
    }
    stdout_output_cb_count++;
}

void credit_cb (flux_subprocess_t *p, const char *stream, int bytes)
{
    int *credits = flux_subprocess_aux_get (p, "credits");
    int len;
    int ret;

    assert (credits);

    diag ("on_credit: credit of %d bytes", bytes);

    (*credits) += bytes;

    if ((inputbuf_len - inputbuf_index) > 0) {
        /* If we "borrowed" credits, credits may not be > 0 */
        if ((*credits) <= 0)
            goto out;

        if ((inputbuf_len - inputbuf_index) > (*credits))
            len = (*credits);
        else
            len = (inputbuf_len - inputbuf_index);

        ret = flux_subprocess_write (p, "stdin", &inputbuf[inputbuf_index], len);
        ok (ret == len,
            "flux_subprocess_write success");

        (*credits) -= ret;
        inputbuf_index += ret;
    }
    else {
        ok (flux_subprocess_close (p, "stdin") == 0,
            "flux_subprocess_close success");
    }

out:
    credit_cb_count++;
}

void test_on_credit (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O",  NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    int credits = 0;
    int ret;

    ok ((cmd = flux_cmd_create (2, av, environ)) != NULL, "flux_cmd_create");
    ok (flux_cmd_setopt (cmd, "stdin_BUFSIZE", "8") == 0,
        "set stdin buffer size to 8 bytes");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = credit_output_cb,
        .on_credit = credit_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    credit_cb_count = 0;
    sprintf (inputbuf, "abcdefghijklmnopqrstuvwxyz0123456789");
    inputbuf_index = 0;
    inputbuf_len = (26 + 10);
    memset (outputbuf, '\0', sizeof (outputbuf));
    outputbuf_len = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");
    ret = flux_subprocess_aux_set (p, "credits", &credits, NULL);
    ok (ret == 0, "flux_subprocess_aux_set works");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    errno = 0;
    ret = flux_subprocess_write (p, "stdin", &inputbuf[inputbuf_index], 10);
    ok (ret < 0 && errno == ENOSPC,
        "flux_subprocess_write fails with too much data");

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count >= 2, "stdout output callback called >= 2 times");
    ok (credit_cb_count == 6, "credit callback called 6 times");
    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

/* very similar to above test but we send initial write by "borrowing"
 * credits
 */
void test_on_credit_borrow_credits (flux_reactor_t *r)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O",  NULL };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    int credits = 0;
    int ret;

    ok ((cmd = flux_cmd_create (2, av, environ)) != NULL, "flux_cmd_create");
    ok (flux_cmd_setopt (cmd, "stdin_BUFSIZE", "8") == 0,
        "set stdin buffer size to 8 bytes");

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = credit_output_cb,
        .on_credit = credit_cb
    };
    completion_cb_count = 0;
    stdout_output_cb_count = 0;
    credit_cb_count = 0;
    sprintf (inputbuf, "abcdefghijklmnopqrstuvwxyz0123456789");
    inputbuf_index = 0;
    inputbuf_len = (26 + 10);
    memset (outputbuf, '\0', sizeof (outputbuf));
    outputbuf_len = 0;
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL, "flux_local_exec");
    ret = flux_subprocess_aux_set (p, "credits", &credits, NULL);
    ok (ret == 0, "flux_subprocess_aux_set works");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state == RUNNING after flux_local_exec");

    errno = 0;
    ret = flux_subprocess_write (p, "stdin", &inputbuf[inputbuf_index], 8);
    ok (ret == 8,
        "flux_subprocess_write first 8 bytes");
    credits -= ret;
    inputbuf_index += ret;

    int rc = flux_reactor_run (r, 0);
    ok (rc == 0, "flux_reactor_run returned zero status");
    ok (completion_cb_count == 1, "completion callback called 1 time");
    ok (stdout_output_cb_count >= 2, "stdout output callback called >= 2 times");
    ok (credit_cb_count == 6, "credit callback called 6 times");
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

    diag ("basic_stdout");
    test_basic_stdout (r);
    diag ("basic_stdout_no_readline");
    test_basic_stdout_no_readline (r);
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
    diag ("write_enospc");
    test_write_enospc (r);
#if 0
    diag ("flag_stdio_fallthrough");
    test_flag_stdio_fallthrough (r);
#endif
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
    diag ("long_line");
    test_long_line (r);
    diag ("on_credit");
    test_on_credit (r);
    diag ("on_credit_borrow_credits");
    test_on_credit_borrow_credits (r);

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
