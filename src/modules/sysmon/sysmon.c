/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sysmon.c - simple system monitoring service
 *
 * On the heartbeat, the following system utilization stats are sampled:
 * - memory
 * - cpu (all)
 * - cpu (individual)
 *
 * Values are integer percentages.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "proc.h"

struct sysmon_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;

    /* local system stats */
    int nr_cpus;
    struct proc_cpu *cpu[2];
    struct proc_cpu tot_cpu[2];
    double mem_usage;
    int count;

    flux_future_t *f_sync;
    struct flux_msglist *requests;
};

/* Get cpu cpu usage from two samples, as an integer percentage.
 */
static int cpu_usage (struct proc_cpu *s1, struct proc_cpu *s2)
{
    return 100 * proc_stat_calc_cpu_usage (s1, s2);
}

static json_t *make_stats_object (struct sysmon_ctx *ctx, bool full)
{
    int mem = 100 * ctx->mem_usage;
    int totcpu = cpu_usage (&ctx->tot_cpu[1], &ctx->tot_cpu[0]);
    json_t *obj = NULL;

    if (!(obj = json_pack ("{s:i s:i}", "mem", mem, "cpu", totcpu)))
        goto error;
    if (full) {
        for (int i = 0; i < ctx->nr_cpus; i++) {
            int pct = cpu_usage (&ctx->cpu[1][i], &ctx->cpu[0][i]);
            json_t *o;
            if (!(o = json_integer (pct))
                || json_object_set_new (obj, ctx->cpu[0][i].name, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
            }
        }
    }
    return obj;
error:
    ERRNO_SAFE_WRAP (json_decref, obj);
    return NULL;
}

static void sysmon_stats_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct sysmon_ctx *ctx = arg;
    json_t *obj = NULL;
    const char *errmsg = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (ctx->count < 2) {
        errmsg = "stats are not populated yet, try again later";
        errno = EINVAL;
        goto error;
    }
    if (!(obj = make_stats_object (ctx, true)))
        goto error;
    if (flux_respond_pack (h, msg, "O", obj) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (obj);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (obj);
}

static int sysmon_monitor_respond (struct sysmon_ctx *ctx,
                                   const flux_msg_t *msg)
{
    json_t *obj;
    int full = 0;

    if (flux_request_unpack (msg, NULL, "{s?b}", "full", &full) < 0)
        return -1;
    if (ctx->count >= 2) {
        if (!(obj = make_stats_object (ctx, full)))
            return -1;
        if (flux_respond_pack (ctx->h, msg, "O", obj) < 0)
            flux_log_error (ctx->h, "error responding to monitor request");
        json_decref (obj);
    }
    return 0;
}

static void sysmon_monitor_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct sysmon_ctx *ctx = arg;

    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    if (sysmon_monitor_respond (ctx, msg) < 0
        || flux_msglist_append (ctx->requests, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to monitor request");
}

static void sysmon_disconnect_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct sysmon_ctx *ctx = arg;

    flux_msglist_disconnect (ctx->requests, msg);
}

static void sync_continuation (flux_future_t *f, void *arg)
{
    struct sysmon_ctx *ctx = arg;

    ctx->tot_cpu[1] = ctx->tot_cpu[0];
    if (proc_stat_get_tot_cpu (&ctx->tot_cpu[0]) < 0)
        flux_log (ctx->h, LOG_ERR, "error sampling overall cpu stats");

    struct proc_cpu *tmp = ctx->cpu[1];
    ctx->cpu[1] = ctx->cpu[0];
    ctx->cpu[0] = tmp;
    if (proc_stat_get_cpu (ctx->cpu[0], ctx->nr_cpus) < 0)
        flux_log (ctx->h, LOG_ERR, "error sampling individual cpu stats");

    if (proc_mem_usage (&ctx->mem_usage) < 0)
        flux_log (ctx->h, LOG_ERR, "error parsing /proc/meminfo");

    ctx->count++;

    flux_future_reset (f);

    if (ctx->count >= 2) {
        const flux_msg_t *msg;

        msg = flux_msglist_first (ctx->requests);
        while (msg) {
            sysmon_monitor_respond (ctx, msg);
            msg = flux_msglist_next (ctx->requests);
        }
    }
}

static struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "stats-get",
        sysmon_stats_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "monitor",
        sysmon_monitor_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "disconnect",
        sysmon_disconnect_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END
};

static void sysmon_ctx_destroy (struct sysmon_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx->cpu[0]);
        free (ctx->cpu[1]);
        flux_msglist_destroy (ctx->requests);
        flux_msg_handler_delvec (ctx->handlers);
        flux_future_destroy (ctx->f_sync);
        free (ctx);
        errno = saved_errno;
    }
}

static struct sysmon_ctx *sysmon_ctx_create (flux_t *h)
{
    struct sysmon_ctx *ctx;
    const char *modname = flux_aux_get (h, "flux::name");

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if ((ctx->nr_cpus = proc_stat_get_cpu (NULL, 0)) < 1) {
        flux_log (h, LOG_ERR, "error reading /proc/stat");
        errno = EINVAL;
        goto error;
    }
    if (!(ctx->cpu[0] = calloc (ctx->nr_cpus, sizeof (ctx->cpu[0][0])))
        || !(ctx->cpu[1] = calloc (ctx->nr_cpus, sizeof (ctx->cpu[0][0]))))
        goto error;
    if (!(ctx->f_sync = flux_sync_create (h, 0.))
        || flux_future_then (ctx->f_sync, -1, sync_continuation, ctx) < 0)
        goto error;
    if (flux_msg_handler_addvec_ex (h, modname, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->requests = flux_msglist_create ()))
        goto error;
    ctx->h = h;
    return ctx;
error:
    sysmon_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct sysmon_ctx *ctx;
    int rc = -1;

    if (!(ctx = sysmon_ctx_create (h)))
        goto error;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "reactor exited abnormally");
        goto error;
    }
    rc = 0;
error:
    sysmon_ctx_destroy (ctx);
    return rc;
}

// vi:ts=4 sw=4 expandtab
