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
#include <jansson.h>
#include <flux/core.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libsubprocess/server.h"
#include "src/common/libsubprocess/subprocess_private.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/stdlog.h"

#include "rcmdsrv.h"

struct iostress_ctx {
    flux_t *h;
    flux_subprocess_t *p;
    flux_watcher_t *source;
    flux_watcher_t *timer;
    pid_t pid;
    size_t linesize;
    int linerecv;
    int batchcount;
    int batchlines;
    int batchcursor;
    bool direct;
    const char *name;
};

static void iostress_timer_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    diag ("doomsday has arrived");
    flux_reactor_stop_error (r);
}

static void iostress_start_doomsday (struct iostress_ctx *ctx, double t)
{
    flux_timer_watcher_reset (ctx->timer, t, 0.);
    flux_watcher_start (ctx->timer);
}

static void iostress_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct iostress_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line;
    int len;

    if ((len = flux_subprocess_read_line (p, stream, &line)) < 0)
        diag ("%s: %s", stream, strerror (errno));
    else if (len == 0)
        diag ("%s: EOF", stream);
    else {
        //diag ("%s: %d bytes", stream, len);
        ctx->linerecv++;
    }
}

static void iostress_completion_cb (flux_subprocess_t *p)
{
    struct iostress_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("%s: completion callback", ctx->name);

    diag ("%s: stopping reactor", ctx->name);
    flux_reactor_stop (flux_get_reactor (ctx->h));
}

static void iostress_state_cb (flux_subprocess_t *p,
                               flux_subprocess_state_t state)
{
    struct iostress_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("%s state callback state=%s",
          ctx->name,
          flux_subprocess_state_string (state));

    switch (state) {
        case FLUX_SUBPROCESS_INIT:
        case FLUX_SUBPROCESS_RUNNING:
            ctx->pid = flux_subprocess_pid (p);
            flux_watcher_start (ctx->source); // start sourcing data
            break;
        case FLUX_SUBPROCESS_STOPPED:
            break;
        case FLUX_SUBPROCESS_EXITED: {
            int status = flux_subprocess_status (p);
            if (WIFEXITED (status))
                diag ("%s: exit %d", ctx->name, WEXITSTATUS (status));
            else if (WIFSIGNALED (status))
                diag ("%s: %s", ctx->name, strsignal (WTERMSIG (status)));
            // completion callback will exit the reactor, but just in case
            iostress_start_doomsday (ctx, 2.);
            break;
        }
        case FLUX_SUBPROCESS_FAILED:
            diag ("%s: %s",
                  ctx->name,
                  strerror (flux_subprocess_fail_errno (p)));
            diag ("%s: stopping reactor", ctx->name);
            flux_reactor_stop_error (flux_get_reactor (ctx->h));
            break;
    }
}

static int rexec_write (flux_t *h, uint32_t matchtag, const char *buf, int len)
{
    flux_future_t *f;
    json_t *io;
    bool eof = len > 0 ? false : true;

    if (!(io = ioencode ("stdin", "0", buf, len, eof)))
        return -1;
    if (!(f = flux_rpc_pack (h,
                             "rexec.write",
                             0,
                             FLUX_RPC_NORESPONSE,
                             "{s:i s:O}",
                             "matchtag", matchtag,
                             "io", io))) {
        json_decref (io);
        return -1;
    }
    flux_future_destroy (f);
    json_decref (io);
    return 0;
}

static void iostress_source_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    struct iostress_ctx *ctx = arg;
    char *buf;
    uint32_t matchtag;

    if (!(buf = malloc (ctx->linesize)))
        BAIL_OUT ("out of memory");
    memset (buf, 'F', ctx->linesize - 1);
    buf[ctx->linesize - 1] = '\n';

    matchtag = flux_rpc_get_matchtag (ctx->p->f);

    for (int i = 0; i < ctx->batchlines; i++) {
        if (ctx->direct) {
            if (rexec_write (ctx->h, matchtag, buf, ctx->linesize) < 0)
                BAIL_OUT ("rexec_write failed");
        }
        else {
            int len;
            len = flux_subprocess_write (ctx->p, "stdin", buf, ctx->linesize);
            if (len < 0) {
                diag ("%s: source: %s", ctx->name, strerror (errno));
                goto error;
            }
            if (len < ctx->linesize) {
                diag ("%s: source: short write", ctx->name);
                errno = ENOSPC;
                goto error;
            }
        }
    }
    free (buf);
    if (++ctx->batchcursor == ctx->batchcount) {
        if (flux_subprocess_close (ctx->p, "stdin") < 0) {
            diag ("%s: source: %s", ctx->name, strerror (errno));
            goto error;
        }
        flux_watcher_stop (w);
    }
    return;
error:
    free (buf);
    //flux_reactor_stop_error (r);
    iostress_start_doomsday (ctx, 2.);
}

flux_subprocess_ops_t iostress_ops = {
    .on_completion      = iostress_completion_cb,
    .on_state_change    = iostress_state_cb,
    .on_stdout          = iostress_output_cb,
};

bool iostress_run_check (flux_t *h,
                         const char *name,
                         bool direct,
                         int stdin_bufsize,
                         int stdout_bufsize,
                         int batchcount,
                         int batchlines,
                         size_t linesize)
{
    char *cat_av[] = { "cat", NULL };
    flux_cmd_t *cmd;
    struct iostress_ctx ctx;
    int rc;
    bool ret = true;
    char nbuf[16];

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    ctx.batchcount = batchcount;
    ctx.batchlines = batchlines;
    ctx.linesize = linesize;
    ctx.name = name;
    ctx.direct = direct;

    if (!(cmd = flux_cmd_create (ARRAY_SIZE (cat_av) - 1, cat_av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    if (stdin_bufsize > 0) {
        snprintf (nbuf, sizeof (nbuf), "%d", stdin_bufsize);
        if (flux_cmd_setopt (cmd, "stdin_BUFSIZE", nbuf) < 0)
            BAIL_OUT ("flux_cmd_setopt failed");
    }
    if (stdout_bufsize > 0) {
        snprintf (nbuf, sizeof (nbuf), "%d", stdout_bufsize);
        if (flux_cmd_setopt (cmd, "stdout_BUFSIZE", nbuf) < 0)
            BAIL_OUT ("flux_cmd_setopt failed");
    }
    if (!(ctx.p = flux_rexec_ex (h,
                                 "rexec",
                                 FLUX_NODEID_ANY,
                                 0,
                                 cmd,
                                 &iostress_ops,
                                 tap_logger,
                                 NULL)))
        BAIL_OUT ("flux_rexec failed");
    if (flux_subprocess_aux_set (ctx.p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");

    if (!(ctx.source = flux_prepare_watcher_create (flux_get_reactor (h),
                                                    iostress_source_cb,
                                                    &ctx)))
        BAIL_OUT ("could not create prepare watcher");
    if (!(ctx.timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                 0.,
                                                 0.,
                                                 iostress_timer_cb,
                                                 &ctx)))
        BAIL_OUT ("could not create timer watcher");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    if (rc < 0) {
        diag ("%s: flux_reactor_run: %s", name, strerror (errno));
        ret = false;
    }

    diag ("%s: processed %d of %d lines", name,
          ctx.linerecv, ctx.batchcount * ctx.batchlines);
    if (ctx.linerecv < ctx.batchcount * ctx.batchlines)
        ret = false;

    flux_watcher_destroy (ctx.source);
    flux_watcher_destroy (ctx.timer);
    diag ("%s: destroying subprocess", name);
    flux_subprocess_destroy (ctx.p);
    flux_cmd_destroy (cmd);

    return ret;
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    h = rcmdsrv_create ("rexec");

    ok (iostress_run_check (h, "balanced", false, 0, 0, 8, 8, 80),
        "balanced worked");

    // (remote?) stdout buffer is overrun
    // Needs further investigation as no errors are thrown and completion is
    // not called called after subprocess exit.  The doomsday timer stops
    // the test.
    ok (!iostress_run_check (h, "tinystdout", false, 0, 128, 1, 1, 256),
        "tinystdout failed as expected");

    // local stdin buffer is overrun (immediately)
    // remote stdin buffer is also overwritten
    ok (!iostress_run_check (h, "tinystdin", false, 128, 0, 1, 1, 4096),
        "tinystdin failed as expected");

    // remote stdin buffer is overwritten using direct RPC
    ok (!iostress_run_check (h, "tinystdin-direct", true, 128, 0, 1, 1, 4096),
        "tinystdin-direct failed as expected");

    test_server_stop (h);
    flux_close (h);
    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
