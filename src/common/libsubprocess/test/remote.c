/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <unistd.h> // environ def
#include <signal.h>
#include <jansson.h>
#include <flux/core.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libsubprocess/server.h"
#include "src/common/libsubprocess/client.h"
#include "src/common/libsubprocess/command_private.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/stdlog.h"

#include "rcmdsrv.h"

#define SERVER_NAME "test-remote"

struct simple_scorecard {
    unsigned int completion:1;
    unsigned int exit_nonzero:1;
    unsigned int signaled:1;

    // states
    unsigned int init:1;
    unsigned int running:1;
    unsigned int failed:1;
    unsigned int exited:1;
    unsigned int stopped:1;

    // output
    unsigned int stdout_eof:1;
    unsigned int stderr_eof:1;
    unsigned int stdout_error:1;
    unsigned int stderr_error:1;
    int stdout_lines;
    int stderr_lines;
};

struct simple_ctx {
    flux_t *h;
    struct simple_scorecard scorecard;
};

extern char **environ;

static void simple_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line;
    int len;

    if ((len = flux_subprocess_read_line (p, stream, &line)) < 0)
        diag ("%s: %s", stream, strerror (errno));
    else if (len == 0)
        diag ("%s: EOF", stream);
    else
        diag ("%s: %d bytes", stream, len);

    if (streq (stream, "stdout")) {
        if (len < 0)
            ctx->scorecard.stdout_error = 1;
        else if (len == 0)
            ctx->scorecard.stdout_eof = 1;
        else
            ctx->scorecard.stdout_lines++;
    }
    else if (streq (stream, "stderr")) {
        if (len < 0)
            ctx->scorecard.stderr_error = 1;
        else if (len == 0)
            ctx->scorecard.stderr_eof = 1;
        else
            ctx->scorecard.stderr_lines++;
    }
}

static void simple_state_cb (flux_subprocess_t *p,
                             flux_subprocess_state_t state)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("state callback state=%s", flux_subprocess_state_string (state));

    switch (state) {
        case FLUX_SUBPROCESS_INIT:
            ctx->scorecard.init = 1;
            break;
        case FLUX_SUBPROCESS_RUNNING:
            ctx->scorecard.running= 1;
            break;
        case FLUX_SUBPROCESS_EXITED:
            ctx->scorecard.exited = 1;
            break;
        case FLUX_SUBPROCESS_FAILED:
            ctx->scorecard.failed = 1;
            diag ("stopping reactor");
            flux_reactor_stop (flux_get_reactor (ctx->h));
            break;
        case FLUX_SUBPROCESS_STOPPED:
            ctx->scorecard.stopped = 1;
            break;
    }
}

static void simple_completion_cb (flux_subprocess_t *p)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("completion callback");

    ctx->scorecard.completion = 1;
    if (flux_subprocess_exit_code (p) > 0)
        ctx->scorecard.exit_nonzero = 1;
    if (flux_subprocess_signaled (p) >= 0)
        ctx->scorecard.signaled = 1;

    diag ("stopping reactor");
    flux_reactor_stop (flux_get_reactor (ctx->h));
}

flux_subprocess_ops_t simple_ops = {
    .on_completion      = simple_completion_cb,
    .on_state_change    = simple_state_cb,
    .on_stdout          = simple_output_cb,
    .on_stderr          = simple_output_cb,
};


void simple_run_check (flux_t *h,
                       const char *prefix,
                       int ac,
                       char **av,
                       struct simple_scorecard *exp)
{
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    struct simple_ctx ctx;
    int rc;

    cmd = flux_cmd_create (ac, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       0,
                       cmd,
                       &simple_ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "%s: flux_rexec_ex returned a subprocess object", prefix);
    if (!p)
        BAIL_OUT ("flux_rexec_ex failed");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0,
        "%s: client reactor ran successfully", prefix);
    ok (ctx.scorecard.init == exp->init,
        "%s: subprocess state=INIT was %sreported",
        prefix, exp->init ? "" : "not ");
    ok (ctx.scorecard.running == exp->running,
        "%s: subprocess state=RUNNING was %sreported",
        prefix, exp->running ? "" : "not ");
    ok (ctx.scorecard.exited == exp->exited,
        "%s: subprocess state=EXITED was %sreported",
        prefix, exp->exited ? "" : "not ");
    ok (ctx.scorecard.failed == exp->failed,
        "%s: subprocess state=FAILED was %sreported",
        prefix, exp->failed ? "" : "not ");
    ok (ctx.scorecard.stopped == exp->stopped,
        "%s: subprocess state=STOPPED was %sreported",
        prefix, exp->stopped ? "" : "not ");
    ok (ctx.scorecard.completion == exp->completion,
        "%s: subprocess completion callback was %sinvoked",
        prefix, exp->completion ? "" : "not ");
    ok (ctx.scorecard.exit_nonzero == exp->exit_nonzero,
        "%s: subprocess did%s exit with nonzero exit code",
        prefix, exp->exit_nonzero ? "" : " not");
    ok (ctx.scorecard.signaled == exp->signaled,
        "%s: subprocess was%s signaled",
        prefix, exp->signaled ? "" : " not");
    ok (ctx.scorecard.stdout_lines == exp->stdout_lines,
        "%s: subprocess stdout got %d lines",
        prefix, exp->stdout_lines);
    ok (ctx.scorecard.stdout_eof == exp->stdout_eof,
        "%s: subprocess stdout %s EOF",
        prefix, exp->stdout_eof ? "got" : "did not get");
    ok (ctx.scorecard.stdout_error == exp->stdout_error,
        "%s: subprocess stdout %s error",
        prefix, exp->stdout_error ? "got" : "did not get");
    flux_cmd_destroy (cmd);
    flux_subprocess_destroy (p);
}

void simple_test (flux_t *h)
{
    struct simple_scorecard exp;

    char *true_av[] = { "true", NULL };
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    simple_run_check (h,
                      "true",
                      ARRAY_SIZE (true_av) - 1,
                      true_av,
                      &exp);

    char *false_av[] = { "false", NULL };
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.exit_nonzero = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    simple_run_check (h,
                      "false",
                      ARRAY_SIZE (false_av) - 1,
                      false_av,
                      &exp);
#if 0
    // This fails differently on el7 - need to investigate
    char *nocmd_av[] = { "/nocmd", NULL };
    memset (&exp, 0, sizeof (exp));
    exp.failed = 1;
    simple_run_check (h,
                      "/nocmd",
                      ARRAY_SIZE (nocmd_av) - 1,
                      nocmd_av,
                      &exp);
#endif

    char *echo_av[] = { "/bin/sh", "-c", "echo hello", NULL };
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.stdout_lines = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    simple_run_check (h,
                      "echo stdout",
                      ARRAY_SIZE (echo_av) - 1,
                      echo_av,
                      &exp);

    char *echo2_av[] = { "/bin/sh", "-c", "echo hello >&2", NULL };
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.stderr_lines = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    simple_run_check (h,
                      "echo stderr",
                      ARRAY_SIZE (echo2_av) - 1,
                      echo2_av,
                      &exp);
}

void simple_pre_running_write_close_output_cb (flux_subprocess_t *p,
                                               const char *stream)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line = NULL;
    char cmpbuf[1024];
    int len;

    if (!streq (stream, "stdout"))
        BAIL_OUT ("unexpected stream: %s", stream);

    if (ctx->scorecard.stdout_lines == 0) {
        len = flux_subprocess_read (p, stream, &line);
        ok (len > 0
            && line != NULL,
            "flux_subprocess_read success");

        /* 1 + 3 + 1 for ':', "foo", "\n" */
        ok (len == (strlen (stream) + 1 + 3 + 1),
            "flux_subprocess_read returned correct data len");

        sprintf (cmpbuf, "%s:foo\n", stream);
        ok (streq (line, cmpbuf),
            "flux_subprocess_read returned correct data");

        ctx->scorecard.stdout_lines++;
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &line);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
        ctx->scorecard.stdout_eof++;
    }
}

void simple_pre_running_write_close (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", NULL };
    flux_subprocess_ops_t ops = {
        .on_completion      = simple_completion_cb,
        .on_stdout          = simple_pre_running_write_close_output_cb,
    };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    struct simple_ctx ctx;
    int rc;

    cmd = flux_cmd_create (3, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       0,
                       cmd,
                       &ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "unbuf basic read: flux_rexec_ex returned a subprocess object");
    if (!p)
        BAIL_OUT ("flux_rexec_ex failed");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");

    /* write & close BEFORE flux_reactor_run() */
    ok (flux_subprocess_write (p, "stdin", "foo", 3) == 3,
        "flux_subprocess_write success");

    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0, "unbuf basic read: client reactor ran successfully");
    ok (ctx.scorecard.completion == 1, "completion callback called 1 time");
    ok (ctx.scorecard.stdout_lines == 1, "stdout lines valid");
    ok (ctx.scorecard.stdout_eof == 1, "stdout eof count valid");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void simple_pre_running_close_output_cb (flux_subprocess_t *p,
                                         const char *stream)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line = NULL;
    int len;

    if (!streq (stream, "stdout"))
        BAIL_OUT ("unexpected stream: %s", stream);

    ok (flux_subprocess_read_stream_closed (p, stream),
        "flux_subprocess_read_stream_closed saw EOF on %s", stream);

    len = flux_subprocess_read (p, stream, &line);
    ok (len == 0,
        "flux_subprocess_read on %s read EOF", stream);
    ctx->scorecard.stdout_eof++;
}

void simple_pre_running_close (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", NULL };
    flux_subprocess_ops_t ops = {
        .on_completion      = simple_completion_cb,
        .on_stdout          = simple_pre_running_close_output_cb,
    };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    struct simple_ctx ctx;
    int rc;

    cmd = flux_cmd_create (3, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       0,
                       cmd,
                       &ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "unbuf basic read: flux_rexec_ex returned a subprocess object");
    if (!p)
        BAIL_OUT ("flux_rexec_ex failed");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");

    /* close BEFORE flux_reactor_run() */
    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0, "unbuf basic read: client reactor ran successfully");
    ok (ctx.scorecard.completion == 1, "completion callback called 1 time");
    ok (ctx.scorecard.stdout_lines == 0, "stdout lines valid");
    ok (ctx.scorecard.stdout_eof == 1, "stdout eof count valid");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void local_unbuf_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line = NULL;
    char cmpbuf[1024];
    int len;

    if (!streq (stream, "stdout"))
        BAIL_OUT ("unexpected stream: %s", stream);

    if (ctx->scorecard.stdout_lines == 0) {
        errno = 0;
        len = flux_subprocess_read_line (p, stream, &line);
        ok (len < 0
            && errno == EPERM,
            "flux_subprocess_read_line fails w/ EPERM w/ LOCAL_UNBUF");

        errno = 0;
        len = flux_subprocess_read_trimmed_line (p, stream, &line);
        ok (len < 0
            && errno == EPERM,
            "flux_subprocess_read_trimmed_line fails w/ EPERM w/ LOCAL_UNBUF");

        errno = 0;
        len = flux_subprocess_getline (p, stream, &line);
        ok (len < 0
            && errno == EPERM,
            "flux_subprocess_getline fails w/ EPERM w/ LOCAL_UNBUF");

        len = flux_subprocess_read (p, stream, &line);
        ok (len > 0
            && line != NULL,
            "flux_subprocess_read success");

        /* 1 + 2 + 1 for ':', "hi", "\n" */
        ok (len == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read returned correct data len");

        /* N.B. not guarantee on NUL termination, use memcmp() not streq() */
        sprintf (cmpbuf, "%s:hi\n", stream);
        ok (memcmp (line, cmpbuf, len) == 0,
            "flux_subprocess_read returned correct data");

        ctx->scorecard.stdout_lines++;

        len = flux_subprocess_read (p, stream, &line);
        ok (len > 0
            && line != NULL,
            "flux_subprocess_read success on second call");

        ok (len == (strlen (stream) + 1 + 2 + 1),
            "flux_subprocess_read returned correct data len on second call");

        ok (memcmp (line, cmpbuf, len) == 0,
            "flux_subprocess_read returned correct data on second call");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &line);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
        ctx->scorecard.stdout_eof++;
    }
}

void local_unbuf_test (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-P", "-O", "hi", NULL };
    flux_subprocess_ops_t ops = {
        .on_completion      = simple_completion_cb,
        .on_stdout          = local_unbuf_output_cb,
    };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    struct simple_ctx ctx;
    int rc;

    cmd = flux_cmd_create (4, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF,
                       cmd,
                       &ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "unbuf basic read: flux_rexec_ex returned a subprocess object");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0, "unbuf basic read: client reactor ran successfully");
    ok (ctx.scorecard.completion == 1, "completion callback called 1 time");
    ok (ctx.scorecard.stdout_lines == 1, "stdout lines valid");
    ok (ctx.scorecard.stdout_eof == 1, "stdout eof count valid");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void local_unbuf_multiline_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct simple_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line = NULL;
    int len;

    if (!streq (stream, "stdout"))
        BAIL_OUT ("unexpected stream: %s", stream);

    if (ctx->scorecard.stdout_lines < 2) {
        len = flux_subprocess_read (p, stream, &line);
        ok (len > 0
            && line != NULL,
            "flux_subprocess_read success");

        /* 3 for "hi" and "\n" */
        ok (len == 3,
            "flux_subprocess_read returned correct data len");

        /* N.B. not guarantee on NUL termination, use memcmp() not streq() */
        ok (memcmp (line, "hi\n", len) == 0,
            "flux_subprocess_read returned correct data");

        ctx->scorecard.stdout_lines++;

        len = flux_subprocess_read (p, stream, &line);
        ok (len > 0
            && line != NULL,
            "flux_subprocess_read success on second call");

        ok (len == 3,
            "flux_subprocess_read returned correct data len on second call");

        ok (memcmp (line, "hi\n", len) == 0,
            "flux_subprocess_read returned correct data on second call");
    }
    else {
        ok (flux_subprocess_read_stream_closed (p, stream),
            "flux_subprocess_read_stream_closed saw EOF on %s", stream);

        len = flux_subprocess_read (p, stream, &line);
        ok (len == 0,
            "flux_subprocess_read on %s read EOF", stream);
        ctx->scorecard.stdout_eof++;
    }
}

void local_unbuf_multiline_test (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-O", "-n", NULL };
    flux_subprocess_ops_t ops = {
        .on_completion      = simple_completion_cb,
        .on_stdout          = local_unbuf_multiline_output_cb,
    };
    flux_cmd_t *cmd;
    flux_subprocess_t *p = NULL;
    struct simple_ctx ctx;
    int rc;

    cmd = flux_cmd_create (3, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF,
                       cmd,
                       &ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "unbuf basic read: flux_rexec_ex returned a subprocess object");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    ok (flux_subprocess_write (p, "stdin", "hi\nhi\n", 6) == 6,
        "flux_subprocess_write success");
    ok (flux_subprocess_close (p, "stdin") == 0,
        "flux_subprocess_close success");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0, "unbuf basic read: client reactor ran successfully");
    ok (ctx.scorecard.completion == 1, "completion callback called 1 time");
    ok (ctx.scorecard.stdout_lines == 2, "stdout lines valid");
    ok (ctx.scorecard.stdout_eof == 1, "stdout eof count valid");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

/* In SIGSTOP test, a 'cat' subprocess is sent SIGSTOP upon starting.
 * If remote SIGSTOP handling works, the state callback is called again
 * with state == STOPPED, which triggers closure of stdin and natural
 * termination of the process, which causes the reactor to exit.
 */

static void stop_state_cb (flux_subprocess_t *p,
                           flux_subprocess_state_t state)
{
    flux_reactor_t *r = flux_subprocess_aux_get (p, "reactor");

    diag ("state callback state=%s", flux_subprocess_state_string (state));
    if (state == FLUX_SUBPROCESS_RUNNING) {
        pid_t pid = flux_subprocess_pid (p);
        if (pid < 0 || kill (pid, SIGSTOP) < 0) {
            diag ("could not stop test proc: %s", strerror (errno));
            flux_reactor_stop_error (r);
        }
    }
    else if (state == FLUX_SUBPROCESS_STOPPED) {
        pid_t pid = flux_subprocess_pid (p);
        if (pid < 0 || kill (pid, SIGCONT) < 0) {
            diag ("could not continue test proc: %s", strerror (errno));
            flux_reactor_stop_error (r);
        }
        if (flux_subprocess_close (p, "stdin") < 0) {
            diag ("could not close remote stdin");
            flux_reactor_stop_error (r);
        }
    }
}

static void stop_output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *line;
    int len;

    if ((len = flux_subprocess_read_line (p, stream, &line)) < 0)
        diag ("%s: %s", stream, strerror (errno));
    else if (len == 0)
        diag ("%s: EOF", stream);
    else
        diag ("%s: %d bytes", stream, len);
}

flux_subprocess_ops_t stoptest_ops = {
    .on_state_change    = stop_state_cb,
    .on_stdout          = stop_output_cb,
    .on_stderr          = stop_output_cb,
};

void sigstop_test (flux_t *h)
{
    char *av[] = { "/bin/cat", NULL };
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    int rc;

    cmd = flux_cmd_create (ARRAY_SIZE (av) - 1, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       0,
                       cmd,
                       &stoptest_ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "stoptest: created subprocess");
    if (flux_subprocess_aux_set (p, "reactor", flux_get_reactor (h), NULL) < 0)
        BAIL_OUT ("could not stash reactor in subprocess aux container");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0,
        "stoptest: reactor ran successfully");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

/* Like sigstop_test, but verifies the on_sigchld callback is invoked
 * with FLUX_SUBPROCESS_SIGCHLD_STOPPED when the subprocess is stopped.
 */

static int sigchld_stopped_count;

static void sigchld_stop_cb (flux_subprocess_t *p,
                             flux_subprocess_sigchld_t sigchld)
{
    diag ("sigchld callback sigchld=%s",
          flux_subprocess_sigchld_string (sigchld));
    ok (sigchld == FLUX_SUBPROCESS_SIGCHLD_STOPPED,
        "sigchld callback sigchld == STOPPED");
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "sigchld returned when job was running");
    sigchld_stopped_count++;
}

flux_subprocess_ops_t sigchld_stoptest_ops = {
    .on_state_change    = stop_state_cb,
    .on_stdout          = stop_output_cb,
    .on_stderr          = stop_output_cb,
    .on_sigchld         = sigchld_stop_cb,
};

void sigchld_sigstop_test (flux_t *h)
{
    char *av[] = { "/bin/cat", NULL };
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    int rc;

    cmd = flux_cmd_create (ARRAY_SIZE (av) - 1, av, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");

    sigchld_stopped_count = 0;

    p = flux_rexec_ex (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       0,
                       cmd,
                       &sigchld_stoptest_ops,
                       tap_logger,
                       NULL);
    ok (p != NULL,
        "sigchld stoptest: created subprocess");
    if (flux_subprocess_aux_set (p, "reactor", flux_get_reactor (h), NULL) < 0)
        BAIL_OUT ("could not stash reactor in subprocess aux container");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0,
        "sigchld stoptest: reactor ran successfully");
    /* N.B. The subprocess is reported as stopped via both the legacy
     * FLUX_SUBPROCESS_STOPPED state and the new sigchld path (until a
     * later commit removes the former).  Both append the same
     * FLUX_SUBPROCESS_SIGCHLD_STOPPED flag, which coalesces into a
     * single pending sigchld, so on_sigchld is called exactly once.
     */
    ok (sigchld_stopped_count == 1,
        "sigchld stoptest: on_sigchld called once with STOPPED");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
}

void bg_kill (flux_t *h, const char *label)
{
    flux_future_t *f;
    char *topic;

    if (asprintf (&topic, "%s.kill", SERVER_NAME) < 0)
        BAIL_OUT ("failed to create kill topic string");
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       0,
                       "{s:i s:s s:i}",
                       "pid", -1,
                       "label", label,
                       "signum", 15);
    if (!f)
        BAIL_OUT ("failed to send %s RPC", topic);
    ok (flux_future_get (f, NULL) == 0,
        "%s: kill successful",
        label);
    flux_future_destroy (f);
    free (topic);
}

void bg_test (flux_t *h,
              const char *label,
              int argc,
              char **argv,
              int expected_status,
              bool kill)
{
    int status;
    int rc;
    flux_future_t *f;
    flux_cmd_t *cmd;
    bool wait_on_pid = false;
    int pid;

    if (!label) {
        /* Wait using pid */
        wait_on_pid = true;
        label = "wait-on-pid";
    }
    diag ("%s: background test argc=%d cmd=%s", label, argc, argv[0]);
    cmd = flux_cmd_create (argc, argv, environ);
    if (!cmd)
        BAIL_OUT ("flux_cmd_create failed");
    if (!wait_on_pid) {
        ok (flux_cmd_set_label (cmd, label) == 0,
            "%s: set cmd label",
            label);
    }
    f = flux_rexec_bg (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_WAITABLE,
                       cmd);
    flux_cmd_destroy (cmd);
    if (!f)
        BAIL_OUT ("%s: flux_rexec_bg failed", label);
    rc = flux_rpc_get_unpack (f, "{s:i}", "pid", &pid);
    flux_future_destroy (f);
    if (expected_status < 0) {
        ok (rc < 0 && errno == -expected_status,
            "%s: got rc=%d (expected -1) with errno=%d (expected %d)",
            label,
            rc,
            errno,
            -expected_status);
        return;
    }
    ok (rc == 0,
        "%s: flux_rexec_bg returned success",
        label);
    if (kill)
        bg_kill (h, label);
    f = flux_rexec_wait (h,
                         SERVER_NAME,
                         FLUX_NODEID_ANY,
                         wait_on_pid ? pid : -1,
                         wait_on_pid ? NULL : label);
    if (!f)
        BAIL_OUT ("%s: flux_rexec_wait failed", label);
    ok (flux_rpc_get_unpack (f, "{s:i}", "status", &status) == 0,
        "%s: flux_rpc_get_unpack returned successfully",
        label);
    ok (status == expected_status,
        "%s: got expected status (got 0x%04x, expected 0x%04x)",
        label,
        status,
        expected_status);
    flux_future_destroy (f);
}

/* Start a waitable background subprocess with the given label.  The stdout
 * and stderr forwarding flags are ignored in background mode (RFC 42);
 * forwarding is selected later by the attaching client's callbacks.
 */
static void attach_bg_start (flux_t *h,
                             const char *label,
                             int ac,
                             char **av)
{
    flux_future_t *f;
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (ac, av, environ))
        || flux_cmd_set_label (cmd, label) < 0)
        BAIL_OUT ("attach_bg_start: flux_cmd_create");
    f = flux_rexec_bg (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_WAITABLE,
                       cmd);
    if (!f)
        BAIL_OUT ("attach_bg_start: flux_rexec_bg");
    ok (flux_rpc_get (f, NULL) == 0,
        "%s: background exec started", label);
    flux_future_destroy (f);
    flux_cmd_destroy (cmd);
}

/* Attach to a background subprocess and run to completion, checking the
 * scorecard.  Uses the simple_ops callbacks.
 */
static void attach_run_check (flux_t *h,
                              const char *prefix,
                              const char *label,
                              struct simple_scorecard *exp)
{
    flux_subprocess_t *p;
    struct simple_ctx ctx;
    int rc;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_attach (h,
                           SERVER_NAME,
                           FLUX_NODEID_ANY,
                           0,
                           -1,
                           label,
                           &simple_ops);
    ok (p != NULL,
        "%s: flux_rexec_attach returned a subprocess object", prefix);
    if (!p)
        BAIL_OUT ("flux_rexec_attach failed");
    if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0,
        "%s: client reactor ran successfully", prefix);
    ok (ctx.scorecard.running == exp->running,
        "%s: subprocess state=RUNNING was %sreported",
        prefix, exp->running ? "" : "not ");
    ok (ctx.scorecard.exited == exp->exited,
        "%s: subprocess state=EXITED was %sreported",
        prefix, exp->exited ? "" : "not ");
    ok (ctx.scorecard.completion == exp->completion,
        "%s: subprocess completion callback was %sinvoked",
        prefix, exp->completion ? "" : "not ");
    ok (ctx.scorecard.exit_nonzero == exp->exit_nonzero,
        "%s: subprocess did%s exit with nonzero exit code",
        prefix, exp->exit_nonzero ? "" : " not");
    ok (ctx.scorecard.stdout_eof == exp->stdout_eof,
        "%s: subprocess stdout %s EOF",
        prefix, exp->stdout_eof ? "got" : "did not get");
    ok (ctx.scorecard.stderr_eof == exp->stderr_eof,
        "%s: subprocess stderr %s EOF",
        prefix, exp->stderr_eof ? "got" : "did not get");
    flux_subprocess_destroy (p);
}

void attach_test (flux_t *h)
{
    struct simple_scorecard exp;

    /* Attach to a running background process: it should report RUNNING,
     * stream its stdout/stderr EOFs when it exits, and complete with the
     * exit status.
     */
    char *run_av[] = { "/bin/sh", "-c", "sleep 0.2; exit 4", NULL };
    attach_bg_start (h, "att-run", ARRAY_SIZE (run_av) - 1, run_av);
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.exit_nonzero = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    attach_run_check (h, "attach running", "att-run", &exp);

    /* Attach to a background process that closed stdout *before* the attach.
     * The EOF consumed in background mode is not forwarded, so the server
     * synthesizes it on attach; without that EOF the client's per-stream
     * accounting would never balance and completion would not fire.
     */
    char *closed_av[] = {
        "/bin/sh", "-c", "echo hi; exec 1>&-; sleep 0.2; exit 9", NULL
    };
    attach_bg_start (h, "att-closed", ARRAY_SIZE (closed_av) - 1, closed_av);
    /* Pump the reactor so the server observes the stdout close while the
     * process is still in background mode.
     */
    flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    memset (&exp, 0, sizeof (exp));
    exp.running = 1;
    exp.exited = 1;
    exp.completion = 1;
    exp.exit_nonzero = 1;
    exp.stdout_eof = 1;
    exp.stderr_eof = 1;
    attach_run_check (h, "attach stdout-closed-in-bg", "att-closed", &exp);

    /* Attach to a nonexistent label -> the attach RPC fails with ENOENT,
     * surfaced to the client as the FAILED state.
     */
    struct simple_ctx ctx;
    flux_subprocess_t *p;
    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    p = flux_rexec_attach (h,
                           SERVER_NAME,
                           FLUX_NODEID_ANY,
                           0,
                           -1,
                           "att-nope",
                           &simple_ops);
    ok (p != NULL,
        "attach nonexistent: flux_rexec_attach returned a subprocess object");
    if (p) {
        if (flux_subprocess_aux_set (p, "ctx", &ctx, NULL) < 0)
            BAIL_OUT ("flux_subprocess_aux_set failed");
        flux_reactor_run (flux_get_reactor (h), 0);
        ok (ctx.scorecard.failed == 1,
            "attach nonexistent: subprocess reached FAILED state");
        ok (flux_subprocess_fail_errno (p) == ENOENT,
            "attach nonexistent: fail errno is ENOENT");
        flux_subprocess_destroy (p);
    }
}

void background_waitable_test (flux_t *h)
{
    char *cmd_noexist[] = { "/noexist", NULL };
    char *cmd_true[] = { "true", NULL };
    char *cmd_false[] = { "false", NULL };
    char *cmd_sleep[] = { "sleep", "30", NULL };
    /* cat reads stdin until EOF then exits 0.  A background subprocess's
     * stdin is at EOF (RFC 42), so cat exits 0 rather than blocking; if it
     * blocked, the wait below would hang.
     */
    char *cmd_cat[] = { "cat", NULL };
    bg_test (h, "noexist", 1, cmd_noexist, -ENOENT, false);
    bg_test (h, "success", 1, cmd_true, 0, false);
    bg_test (h, NULL, 1, cmd_true, 0, false);
    bg_test (h, "failure", 1, cmd_false, 256, false);
    bg_test (h, "sleep", 2, cmd_sleep, 15, true);
    bg_test (h, "stdin-eof", 1, cmd_cat, 0, false);
}

/* Concatenate the "data" of all retained io objects matching 'stream' in the
 * wait response "output" array into 'buf' (NUL terminated).  Returns the
 * number of matching io objects, or -1 if the output key is absent.
 */
static int collect_output (json_t *output, const char *stream, char *buf, size_t bufsize)
{
    size_t i;
    int count = 0;

    buf[0] = '\0';
    if (!output)
        return -1;
    for (i = 0; i < json_array_size (output); i++) {
        const char *s;
        char *data = NULL;
        int len = 0;
        if (iodecode (json_array_get (output, i), &s, NULL, &data, &len, NULL) < 0)
            continue;
        if (data && streq (s, stream)) {
            size_t used = strlen (buf);
            if (used + len < bufsize) {
                memcpy (buf + used, data, len);
                buf[used + len] = '\0';
            }
            count++;
        }
        free (data);
    }
    return count;
}

/* Start a waitable background subprocess that emits known output, wait for
 * it, and verify the retained output is returned in the wait response.
 */
void background_output_test (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-E", "foo", "bar", NULL };
    flux_cmd_t *cmd;
    flux_future_t *f;
    int status = -1;
    json_t *output = NULL;
    char errbuf[1024];
    int rc;

    if (!(cmd = flux_cmd_create (ARRAY_SIZE (av) - 1, av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    ok (flux_cmd_set_label (cmd, "output") == 0,
        "output: set cmd label");
    f = flux_rexec_bg (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_WAITABLE,
                       cmd);
    flux_cmd_destroy (cmd);
    if (!f)
        BAIL_OUT ("output: flux_rexec_bg failed");
    ok (flux_rpc_get (f, NULL) == 0,
        "output: flux_rexec_bg returned success");
    flux_future_destroy (f);

    f = flux_rexec_wait (h, SERVER_NAME, FLUX_NODEID_ANY, -1, "output");
    if (!f)
        BAIL_OUT ("output: flux_rexec_wait failed");
    ok (flux_rpc_get_unpack (f, "{s:i s?o}", "status", &status, "output", &output) == 0,
        "output: wait response unpacked");
    ok (status == 0,
        "output: exited 0 (got 0x%04x)", status);
    rc = collect_output (output, "stderr", errbuf, sizeof (errbuf));
    ok (rc > 0,
        "output: wait response contains stderr output (%d objects)", rc);
    ok (streq (errbuf, "foo\nbar\n"),
        "output: retained stderr is correct (got '%s')", errbuf);
    flux_future_destroy (f);
}

/* Emit far more output than RETAINED_OUTPUT_MAX and verify the wait response
 * retains only a bounded, most-recent tail: the last line survives, the first
 * is evicted, and the total retained data does not greatly exceed the cap.
 */
void background_output_cap_test (flux_t *h)
{
    /* NLINES args x ~LINELEN bytes each greatly exceeds the 8192 byte cap. */
#define NLINES 40
#define LINELEN 500
    char lines[NLINES][LINELEN + 1];
    char *av[NLINES + 3];
    flux_cmd_t *cmd;
    flux_future_t *f;
    json_t *output = NULL;
    char outbuf[65536];
    int status = -1;
    int i;

    av[0] = TEST_SUBPROCESS_DIR "test_echo";
    av[1] = "-O";
    for (i = 0; i < NLINES; i++) {
        /* Each line is "L<nnnnn>" followed by 'x' padding to LINELEN. */
        int n = snprintf (lines[i], sizeof (lines[i]), "L%05d", i);
        memset (lines[i] + n, 'x', LINELEN - n);
        lines[i][LINELEN] = '\0';
        av[i + 2] = lines[i];
    }
    av[NLINES + 2] = NULL;

    if (!(cmd = flux_cmd_create (NLINES + 2, av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    ok (flux_cmd_set_label (cmd, "cap") == 0,
        "cap: set cmd label");
    f = flux_rexec_bg (h,
                       SERVER_NAME,
                       FLUX_NODEID_ANY,
                       FLUX_SUBPROCESS_FLAGS_WAITABLE,
                       cmd);
    flux_cmd_destroy (cmd);
    if (!f)
        BAIL_OUT ("cap: flux_rexec_bg failed");
    ok (flux_rpc_get (f, NULL) == 0,
        "cap: flux_rexec_bg returned success");
    flux_future_destroy (f);

    f = flux_rexec_wait (h, SERVER_NAME, FLUX_NODEID_ANY, -1, "cap");
    if (!f)
        BAIL_OUT ("cap: flux_rexec_wait failed");
    ok (flux_rpc_get_unpack (f,
                             "{s:i s?o}",
                             "status", &status,
                             "output", &output) == 0,
        "cap: wait response unpacked");
    ok (status == 0,
        "cap: exited 0 (got 0x%04x)", status);
    (void)collect_output (output, "stdout", outbuf, sizeof (outbuf));
    ok (strstr (outbuf, "L00039") != NULL,
        "cap: most recent line retained");
    ok (strstr (outbuf, "L00000") == NULL,
        "cap: oldest line evicted");
    ok (strlen (outbuf) <= 8192 + LINELEN + 1,
        "cap: retained output bounded near cap (got %zu bytes)",
        strlen (outbuf));
    flux_future_destroy (f);
#undef NLINES
#undef LINELEN
}

/* Per RFC 42, the write-credit and stdio-fallthrough flags request input
 * handling and are not permitted in background mode.  Send raw exec RPCs to
 * verify each is rejected.
 */
void background_input_reject_test (flux_t *h)
{
    flux_cmd_t *cmd;
    json_t *cmd_obj = NULL;
    char *topic;
    flux_future_t *f;
    char *av[] = { "true", NULL };

    if (!(cmd = flux_cmd_create (1, av, environ))
        || !(cmd_obj = cmd_tojson (cmd)))
        BAIL_OUT ("background_input_reject_test: cmd setup");
    if (asprintf (&topic, "%s.exec", SERVER_NAME) < 0)
        BAIL_OUT ("background_input_reject_test: asprintf");

    /* Non-streaming (background) + write-credit flag -> EINVAL */
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       0,
                       "{s:O s:i s:i}",
                       "cmd", cmd_obj,
                       "flags", SUBPROCESS_REXEC_WRITE_CREDIT,
                       "local_flags", 0);
    ok (f != NULL,
        "sent background exec request");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EINVAL,
        "background exec with write-credit flag fails with EINVAL");
    flux_future_destroy (f);

    /* Non-streaming (background) + stdio-fallthrough local flag -> EINVAL */
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       0,
                       "{s:O s:i s:i}",
                       "cmd", cmd_obj,
                       "flags", 0,
                       "local_flags", FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH);
    ok (f != NULL,
        "sent background exec request");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EINVAL,
        "background exec with stdio-fallthrough flag fails with EINVAL");
    flux_future_destroy (f);

    /* Non-streaming (background) + a command with an auxiliary channel ->
     * EINVAL (implementation restriction; see server_exec_cb()).
     */
    flux_cmd_t *chan_cmd;
    json_t *chan_obj = NULL;
    if (!(chan_cmd = flux_cmd_create (1, av, environ))
        || flux_cmd_add_channel (chan_cmd, "TEST_CHANNEL") < 0
        || !(chan_obj = cmd_tojson (chan_cmd)))
        BAIL_OUT ("background_input_reject_test: channel cmd setup");
    f = flux_rpc_pack (h,
                       topic,
                       FLUX_NODEID_ANY,
                       0,
                       "{s:O s:i s:i}",
                       "cmd", chan_obj,
                       "flags", 0,
                       "local_flags", 0);
    ok (f != NULL,
        "sent background exec request with auxiliary channel");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EINVAL,
        "background exec with auxiliary channel fails with EINVAL");
    flux_future_destroy (f);
    json_decref (chan_obj);
    flux_cmd_destroy (chan_cmd);

    free (topic);
    json_decref (cmd_obj);
    flux_cmd_destroy (cmd);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    h = rcmdsrv_create (SERVER_NAME);

    diag ("simple_test");
    simple_test (h);
    diag ("simple_pre_running_write_close");
    simple_pre_running_write_close (h);
    diag ("simple_pre_running_close");
    simple_pre_running_close (h);
    diag ("local_unbuf_test");
    local_unbuf_test (h);
    diag ("local_unbuf_multiline_test");
    local_unbuf_multiline_test (h);
    diag ("sigstop_test");
    sigstop_test (h);
    diag ("sigchld_sigstop_test");
    sigchld_sigstop_test (h);
    diag ("background_test");
    background_waitable_test (h);
    diag ("background_output_test");
    background_output_test (h);
    diag ("background_output_cap_test");
    background_output_cap_test (h);
    diag ("background_input_reject_test");
    background_input_reject_test (h);
    diag ("attach_test");
    attach_test (h);

    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
