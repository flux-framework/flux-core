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

struct iochan_ctx {
    flux_t *h;
    flux_subprocess_t *p;
    flux_watcher_t *source;
    flux_watcher_t *timer;
    pid_t pid;
    int recvcount;
    int sendcount;
    int count;
    int refcount;
    const char *name;
};

extern char **environ;

const int linesize = 80;
const char *test_fdcopy = TEST_SUBPROCESS_DIR "test_fdcopy";

static void iochan_timer_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    diag ("doomsday has arrived");
    flux_reactor_stop_error (r);
}

static void iochan_start_doomsday (struct iochan_ctx *ctx, double t)
{
    flux_timer_watcher_reset (ctx->timer, t, 0.);
    flux_watcher_start (ctx->timer);
}

static void iochan_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct iochan_ctx *ctx = flux_subprocess_aux_get (p, "ctx");
    const char *line;
    int len;

    if ((len = flux_subprocess_read_line (p, stream, &line)) < 0)
        diag ("%s: %s", stream, strerror (errno));
    else if (len == 0)
        diag ("%s: EOF", stream);
    else if (streq (stream, "stderr") || streq (stream, "stdout"))
        diag ("%s: %s", stream, line);
    else if (streq (stream, "IOCHAN_OUT"))
        ctx->recvcount += len;
}

static void iochan_completion_cb (flux_subprocess_t *p)
{
    struct iochan_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

    diag ("%s: completion callback", ctx->name);

    diag ("%s: stopping reactor", ctx->name);
    flux_reactor_stop (flux_get_reactor (ctx->h));
}

static void iochan_state_cb (flux_subprocess_t *p,
                               flux_subprocess_state_t state)
{
    struct iochan_ctx *ctx = flux_subprocess_aux_get (p, "ctx");

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
            iochan_start_doomsday (ctx, 2.);
            // if testing refcnt, release stdout reference now
            if (streq (ctx->name, "refcnt")) {
                ctx->refcount--;
                flux_subprocess_channel_decref (ctx->p, "stdout");
            }
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

static void iochan_source_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct iochan_ctx *ctx = arg;
    char buf[linesize];
    int len;
    int n = linesize;

    if (n > ctx->count - ctx->sendcount)
        n = ctx->count - ctx->sendcount;

    memset (buf, 'F', n - 1);
    buf[n - 1] = '\n';

    len = flux_subprocess_write (ctx->p, "IOCHAN_IN", buf, n);
    if (len < 0) {
        diag ("%s: source: %s", ctx->name, strerror (errno));
        goto error;
    }
    if (len < n) {
        diag ("%s: source: short write", ctx->name);
        errno = ENOSPC;
        goto error;
    }
    ctx->sendcount += len;
    if (ctx->sendcount == ctx->count) {
        if (flux_subprocess_close (ctx->p, "IOCHAN_IN") < 0) {
            diag ("%s: source: %s", ctx->name, strerror (errno));
            goto error;
        }
        flux_watcher_stop (w);
    }
    return;
error:
    //flux_reactor_stop_error (r);
    iochan_start_doomsday (ctx, 2.);
}

flux_subprocess_ops_t iochan_ops = {
    .on_completion      = iochan_completion_cb,
    .on_state_change    = iochan_state_cb,
    .on_stdout          = iochan_output_cb,
    .on_stderr          = iochan_output_cb,
    .on_channel_out     = iochan_output_cb,
};

bool iochan_run_check (flux_t *h, const char *name, int count)
{
    char *cat_av[] = {
        (char *)test_fdcopy,
        "IOCHAN_IN",
        "IOCHAN_OUT",
        NULL,
    };
    flux_cmd_t *cmd;
    struct iochan_ctx ctx;
    int rc;
    bool ret = true;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    ctx.count = count;
    ctx.name = name;

    if (!(cmd = flux_cmd_create (ARRAY_SIZE (cat_av) - 1, cat_av, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    if (flux_cmd_add_channel (cmd, "IOCHAN_IN") < 0
        || flux_cmd_add_channel (cmd, "IOCHAN_OUT") < 0)
        BAIL_OUT ("flux_cmd_add_channel failed");
    if (!(ctx.p = flux_rexec_ex (h,
                                 "rexec",
                                 FLUX_NODEID_ANY,
                                 0,
                                 cmd,
                                 &iochan_ops,
                                 tap_logger,
                                 NULL)))
        BAIL_OUT ("flux_rexec_ex failed");
    if (streq (ctx.name, "refcnt")) {
        ctx.refcount++;
        flux_subprocess_channel_incref (ctx.p, "stdout");
    }
    if (flux_subprocess_aux_set (ctx.p, "ctx", &ctx, NULL) < 0)
        BAIL_OUT ("flux_subprocess_aux_set failed");

    if (!(ctx.source = flux_prepare_watcher_create (flux_get_reactor (h),
                                                    iochan_source_cb,
                                                    &ctx)))
        BAIL_OUT ("could not create prepare watcher");
    if (!(ctx.timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                 0.,
                                                 0.,
                                                 iochan_timer_cb,
                                                 &ctx)))
        BAIL_OUT ("could not create timer watcher");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    if (rc < 0) {
        diag ("%s: flux_reactor_run: %s", name, strerror (errno));
        ret = false;
    }

    diag ("%s: processed %d of %d bytes", name, ctx.recvcount, ctx.sendcount);
    if (ctx.recvcount < ctx.sendcount)
        ret = false;

    if (streq (ctx.name, "refcnt")) {
        diag ("%s: final refcount: %d", ctx.name, ctx.refcount);
        if (ctx.refcount != 0)
            ret = false;
    }

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

    ok (iochan_run_check (h, "simple", linesize * 100),
        "simple check worked");
    ok (iochan_run_check (h, "simple", linesize * 1000),
        "medium check worked");
    ok (iochan_run_check (h, "simple", linesize * 10000),
        "large check worked");
    ok (iochan_run_check (h, "refcnt", linesize * 10),
        "refcount check worked");
    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
