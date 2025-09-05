/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sysmon.c - monitor resource utilization of the shell cgroup
 */
#define FLUX_SHELL_PLUGIN_NAME "sysmon"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/strstrip.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libutil/cgroup.h"
#include "ccan/str/str.h"

#include "builtins.h"
#include "internal.h"
#include "task.h"

struct sample {
    double t;
    unsigned long long val;
};

struct shell_sysmon {
    flux_shell_t *shell;
    flux_future_t *f_sync;
    flux_watcher_t *timer;
    double period; // -1 = unset (use heartbeat)
    struct cgroup_info cgroup;
    struct sample first_cpu;
    struct sample prev_cpu;
    bool periodic_enable;
    bool memory_disable;
    bool cpu_disable;
};

static const char *get_memory_size (struct shell_sysmon *ctx,
                                    const char *name)
{
    unsigned long long val;

    if (cgroup_scanf (&ctx->cgroup, name, "%llu", &val) != 1)
        return "unknown";
    return encode_size (val);
}

static int get_cpu_stat (struct shell_sysmon *ctx,
                         struct sample *sample)
{
    if (cgroup_key_scanf (&ctx->cgroup,
                          "cpu.stat",
                          "usage_usec",
                          "%llu",
                          &sample->val) != 1)
        return -1;
    sample->t = flux_reactor_now (ctx->shell->r);
    return 0;
}

static double cpu_load_avg (struct sample *s1, struct sample *s2)
{
    double total_cpusec = s2->t - s1->t;
    double used_cpusec = 1E-6 * (s2->val - s1->val); // convert from usec
    double load_avg = 0;

    if (total_cpusec > 0)
        load_avg = used_cpusec / total_cpusec;
    return load_avg;
}

static void sysmon_poll (struct shell_sysmon *ctx)
{
    if (!ctx->memory_disable) {
        shell_trace ("memory.current=%s",
                     get_memory_size (ctx, "memory.current"));
    }

    if (!ctx->cpu_disable) {
        struct sample cur_cpu;
        if (get_cpu_stat (ctx, &cur_cpu) == 0) {
            shell_trace ("loadavg=%.2f", cpu_load_avg (&ctx->prev_cpu, &cur_cpu));
            ctx->prev_cpu = cur_cpu;
        }
    }
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct shell_sysmon *ctx = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ETIMEDOUT) {
        shell_log_error ("sync error: %s", future_strerror (f, errno));
        return;
    }
    sysmon_poll (ctx);
    flux_future_reset (f);
}

static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct shell_sysmon *ctx = arg;
    sysmon_poll (ctx);
}

/* Report after the local tasks have exited.
 */
static int sysmon_exit (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *arg,
                        void *data)
{
    struct shell_sysmon *ctx = data;

    if (!ctx->memory_disable) {
        shell_log ("memory.peak=%s",
                   get_memory_size (ctx, "memory.peak"));
    }

    if (!ctx->cpu_disable) {
        struct sample cur_cpu;
        if (get_cpu_stat (ctx, &cur_cpu) == 0) {
            shell_log ("loadavg-overall=%.2f",
                       cpu_load_avg (&ctx->first_cpu, &cur_cpu));
        }
    }
    return 0;
}

/* Start monitoring after the local tasks have been started.
 */
static int sysmon_start (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *arg,
                         void *data)
{
    struct shell_sysmon *ctx = data;

    if (!ctx->cpu_disable) {
        if (get_cpu_stat (ctx, &ctx->first_cpu) < 0) {
            shell_log_error ("error sampling cpu.stat");
            return -1;
        }
        ctx->prev_cpu = ctx->first_cpu;
    }

    if (ctx->periodic_enable) {
        if (ctx->period < 0) {
            if (!(ctx->f_sync = flux_sync_create (ctx->shell->h, 0))
                || flux_future_then (ctx->f_sync, -1, sync_cb, ctx) < 0) {
                shell_log_error ("error setting up sync callback");
                return -1;
            }
        }
        else {
            if (!(ctx->timer = flux_timer_watcher_create (ctx->shell->r,
                                                          ctx->period,
                                                          ctx->period,
                                                          timer_cb,
                                                          ctx))) {
                shell_log_error ("error setting up sync timer");
                return -1;
            }
            flux_watcher_start (ctx->timer);
        }
    }

    return 0;
}

static void sysmon_destroy (struct shell_sysmon *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_future_destroy (ctx->f_sync);
        flux_watcher_destroy (ctx->timer);
        free (ctx);
        errno = saved_errno;
    }
}

/* -o sysmon.period=FSD
 */
static int sysmon_parse_args (struct shell_sysmon *ctx, json_t *config)
{
    json_error_t jerror;
    json_t *period = NULL;

    if (json_is_object (config)) {
        if (json_unpack_ex (config,
                            &jerror,
                            0,
                            "{s?o !}",
                            "period", &period) < 0) {
            shell_log_error ("option parse error: %s", jerror.text);
            return -1;
        }
        if (period) {
            if (json_is_integer (period)) {
                int i = json_integer_value (period);
                if (i < 0)
                    goto error_period;
                ctx->period = i;
            }
            else if (json_is_string (period)) {
                const char *s = json_string_value (period);
                if (fsd_parse_duration (s, &ctx->period) < 0)
                    goto error_period;
            }
            else
                goto error_period;
        }
    }
    return 0;
error_period:
    shell_log_error ("sysmon.period is not a valid FSD");
    return -1;
}

static struct shell_sysmon *sysmon_create (flux_shell_t *shell, json_t *config)
{
    struct shell_sysmon *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->shell = shell;
    ctx->period = -1;
    if (sysmon_parse_args (ctx, config) < 0)
        goto error;
    if (ctx->shell->verbose >= 2)
        ctx->periodic_enable = true;
    if (cgroup_info_init (&ctx->cgroup) < 0) {
        shell_warn ("incompatible cgroup configuration (disabled)");
        ctx->memory_disable = true;
        ctx->cpu_disable = true;
        goto done;
    }
    if (cgroup_access (&ctx->cgroup, "cpu.stat", R_OK) < 0) {
        shell_warn ("no cpu.stat (disabled)");
        ctx->cpu_disable = true;
    }
    if (cgroup_access (&ctx->cgroup, "memory.peak", R_OK) < 0
        || cgroup_access (&ctx->cgroup, "memory.current", R_OK) < 0) {
        shell_warn ("no memory.peak/memory.current (disabled)");
        ctx->memory_disable = true;
    }
done:
    return ctx;
error:
    sysmon_destroy (ctx);
    return NULL;
}

static int sysmon_init (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *arg,
                        void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_sysmon *ctx;
    json_t *config = NULL;

    if (flux_shell_getopt_unpack (shell, "sysmon", "o", &config) < 0)
        return -1;
    if (!config)
        return 0;
    if (!(ctx = sysmon_create (shell, config)))
        return -1;
    if (ctx->memory_disable && ctx->cpu_disable)
        return 0;
    if (flux_plugin_aux_set (p,
                             "sysmon",
                             ctx,
                             (flux_free_f)sysmon_destroy) < 0) {
        sysmon_destroy (ctx);
        return -1;
    }
    if (flux_plugin_add_handler (p, "shell.start", sysmon_start, ctx) < 0
        || flux_plugin_add_handler (p, "shell.exit", sysmon_exit, ctx) < 0)
        return shell_log_errno ("failed to add shell.start handler");

    shell_debug ("sysmon is enabled");
    return 0;
}

struct shell_builtin builtin_sysmon = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = sysmon_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
