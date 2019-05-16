/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
#include "src/common/libutil/log.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libjob/job.h"
#include "src/common/libutil/read_all.h"

int cmd_submitbench (optparse_t *p, int argc, char **argv);

const char *usage_msg = "[OPTIONS] jobspec";
static struct optparse_option opts[] =  {
    { .name = "repeat", .key = 'r', .has_arg = 1, .arginfo = "N",
      .usage = "Run N instances of jobspec",
    },
    { .name = "fanout", .key = 'f', .has_arg = 1, .arginfo = "N",
      .usage = "Run at most N RPCs in parallel",
    },
    { .name = "priority", .key = 'p', .has_arg = 1, .arginfo = "N",
      .usage = "Set job priority (0-31, default=16)",
    },
    { .name = "flags", .key = 'F', .has_arg = 3,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set comma-separated flags (e.g. debug)",
    },
#if HAVE_FLUX_SECURITY
    { .name = "reuse-signature", .key = 'R', .has_arg = 0,
      .usage = "Sign jobspec once and reuse the result for multiple RPCs",
    },
    { .name = "security-config", .key = 'c', .has_arg = 1, .arginfo = "pattern",
      .usage = "Use non-default security config glob",
    },
    { .name = "sign-type", .key = 's', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Use non-default mechanism type to sign J",
    },
#endif
    OPTPARSE_TABLE_END
};

int main (int argc, char *argv[])
{
    optparse_t *p;
    int exitval;

    log_init ("submitbench");

    p = optparse_create ("submitbench");

    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");
    if (optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");
    if (optparse_parse_args (p, argc, argv) < 0)
        log_msg_exit ("optprase_parse_args");

    exitval = cmd_submitbench (p, argc, argv);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

struct submitbench_ctx {
    flux_t *h;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec;
    const char *sign_type;
#endif
    int flags;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    int txcount;
    int rxcount;
    int totcount;
    int max_queue_depth;
    optparse_t *p;
    void *jobspec;
    int jobspecsz;
    const char *J;
    int priority;
};

/* Read entire file 'name' ("-" for stdin).  Exit program on error.
 */
size_t read_jobspec (const char *name, void **bufp)
{
    int fd;
    ssize_t size;
    void *buf;

    if (!strcmp (name, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (name, O_RDONLY)) < 0)
            log_err_exit ("%s", name);
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err_exit ("%s", name);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    *bufp = buf;
    return size;
}

/* handle RPC response
 * Once all responses are received, stop prep/check watchers
 * so reactor will stop.
 */
void submitbench_continuation (flux_future_t *f, void *arg)
{
    struct submitbench_ctx *ctx = arg;
    flux_jobid_t id;

    if (flux_job_submit_get_id (f, &id) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("submit: job-ingest module is not loaded");
        else
            log_msg_exit ("submit: %s", future_strerror (f, errno));
    }
    printf ("%llu\n", (unsigned long long)id);
    flux_future_destroy (f);

    ctx->rxcount++;
}

/* prep - called before event loop would block
 * Prevent loop from blocking if 'check' could send RPCs.
 * Stop the prep/check watchers if RPCs have all been sent,
 * so that, once responses are received, the reactor will exit naturally.
 */
void submitbench_prep (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct submitbench_ctx *ctx = arg;

    if (ctx->txcount == ctx->totcount) {
        flux_watcher_stop (ctx->prep);
        flux_watcher_stop (ctx->check);
    }
    else if ((ctx->txcount - ctx->rxcount) < ctx->max_queue_depth)
        flux_watcher_start (ctx->idle); // keeps loop from blocking
}

/* check - called after event loop unblocks
 * If there are RPCs to send, send one.
 */
void submitbench_check (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct submitbench_ctx *ctx = arg;
    int flags = ctx->flags;

    flux_watcher_stop (ctx->idle);
    if (ctx->txcount < ctx->totcount
                    && (ctx->txcount - ctx->rxcount) < ctx->max_queue_depth) {
        flux_future_t *f;
#if HAVE_FLUX_SECURITY
        if (ctx->sec) {
            if (!ctx->J || !optparse_hasopt (ctx->p, "reuse-signature")) {
                if (!(ctx->J = flux_sign_wrap (ctx->sec, ctx->jobspec,
                                               ctx->jobspecsz,
                                               ctx->sign_type, 0)))
                    log_err_exit ("flux_sign_wrap: %s",
                                  flux_security_last_error (ctx->sec));
            }
            flags |= FLUX_JOB_PRE_SIGNED;
        }
#endif
        if (!(f = flux_job_submit (ctx->h, ctx->J ? ctx->J : ctx->jobspec,
                                   ctx->priority, flags)))
            log_err_exit ("flux_job_submit");
        if (flux_future_then (f, -1., submitbench_continuation, ctx) < 0)
            log_err_exit ("flux_future_then");
        ctx->txcount++;
    }
}

int cmd_submitbench (optparse_t *p, int argc, char **argv)
{
    flux_reactor_t *r;
    int optindex = optparse_option_index (p);
    struct submitbench_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));

    if (optindex != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "flags")) {
        const char *name;
        while ((name = optparse_getopt_next (p, "flags"))) {
            if (!strcmp (name, "debug"))
                ctx.flags |= FLUX_JOB_DEBUG;
            else
                log_msg_exit ("unknown flag: %s", name);
        }
    }
#if HAVE_FLUX_SECURITY
    /* If any non-default security options are specified, create security
     * context so jobspec can be pre-signed before submission.
     */
    if (optparse_hasopt (p, "security-config")
                            || optparse_hasopt (p, "reuse-signature")
                            || optparse_hasopt (p, "sign-type")) {
        const char *sec_config = optparse_get_str (p, "security-config", NULL);
        if (!(ctx.sec = flux_security_create (0)))
            log_err_exit ("security");
        if (flux_security_configure (ctx.sec, sec_config) < 0)
            log_err_exit ("security config %s", flux_security_last_error (ctx.sec));
        ctx.sign_type = optparse_get_str (p, "sign-type", NULL);
    }
#endif
    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    r = flux_get_reactor (ctx.h);
    ctx.p = p;
    ctx.max_queue_depth = optparse_get_int (p, "fanout", 256);
    ctx.totcount = optparse_get_int (p, "repeat", 1);
    ctx.jobspecsz = read_jobspec (argv[optindex++], &ctx.jobspec);
    ctx.priority = optparse_get_int (p, "priority", FLUX_JOB_PRIORITY_DEFAULT);

    /* Prep/check/idle watchers perform flow control, keeping
     * at most ctx.max_queue_depth RPCs outstanding.
     */
    ctx.prep = flux_prepare_watcher_create (r, submitbench_prep, &ctx);
    ctx.check = flux_check_watcher_create (r, submitbench_check, &ctx);
    ctx.idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!ctx.prep || !ctx.check || !ctx.idle)
        log_err_exit ("flux_watcher_create");
    flux_watcher_start (ctx.prep);
    flux_watcher_start (ctx.check);

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");
#if HAVE_FLUX_SECURITY
    flux_security_destroy (ctx.sec); // invalidates ctx.J
#endif
    flux_close (ctx.h);
    free (ctx.jobspec);
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
