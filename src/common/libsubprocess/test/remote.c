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

    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
