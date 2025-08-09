/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sysmon.c - log dramatic changes in memory/cpu usage
 */
#define FLUX_SHELL_PLUGIN_NAME "sysmon"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/fsd.h"
#include "ccan/str/str.h"

#include "builtins.h"
#include "internal.h"
#include "task.h"

struct shell_sysmon {
    flux_shell_t *shell;
    struct idset *cores;
    flux_future_t *f_mon;
    int full;
    int cpu_alert;
    int mem_alert;
    double settle;
    double t_start;
    json_t *cur;
    json_t *alert;
};

static const int default_mem_alert = 20;
static const int default_cpu_alert = 20;
static const double default_settle = 10.;

static int sysmon_alert (struct shell_sysmon *ctx)
{
    /* If the settling time (if any) has not passed, suppress any alerts.
     */
    double now = flux_reactor_now (flux_get_reactor (ctx->shell->h));
    if (ctx->settle > 0 && now - ctx->t_start < ctx->settle)
        return 0;
    /* If there is no alert history, start with the current sample.
     */
    if (!ctx->alert) {
        ctx->alert = json_incref (ctx->cur);
        return 0;
    }
    /* Generate alerts for keys that deviate substantially from the values
     * in ctx->alert, then update ctx->alert to the logged value.
     */
    const char *key;
    json_t *value;

    json_object_foreach (ctx->cur, key, value) {
        int v2 = json_integer_value (value);
        int v1 = json_integer_value (json_object_get (ctx->alert, key));
        int alert_pct;

        if (streq (key, "mem"))
            alert_pct = ctx->mem_alert;
        else
            alert_pct = ctx->cpu_alert;

        if (abs (v2 - v1) > alert_pct) {
            shell_log ("%s: %i%% (%+i%%)", key, v2, v2 - v1);

            if (json_object_set (ctx->alert, key, value) < 0)
                return -1;
        }
    }
    return 0;
}

/* Filter the sysmon sample to reflect the job's allocation.
 */
static int sysmon_filter (struct shell_sysmon *ctx, json_t *obj)
{
    void *tmp;
    const char *key;
    json_t *value;
    int tot = 0;
    int count = 0;

    json_object_foreach_safe (obj, tmp, key, value) {
        int cpu;
        bool mine = false;
        if (sscanf (key, "cpu%d", &cpu) == 1) {
            if (idset_test (ctx->cores, cpu)) {
                tot += json_integer_value (value);
                count++;
                mine = true;
            }
            if (!mine || !ctx->full)
                json_object_del (obj, key);
        }
    }
    json_t *o;
    if (!(o = json_integer (count > 0 ? tot / count : 0))
        || json_object_set_new (obj, "cpu", o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

static void sysmon_continuation (flux_future_t *f, void *arg)
{
    struct shell_sysmon *ctx = arg;
    json_t *obj;

    if (flux_rpc_get_unpack (f, "o", &obj) < 0) {
        shell_log_error ("error parsing sysmon response");
        return;
    }

    sysmon_filter (ctx, obj);

    // run with -o verbose=1 to get raw dump of each sample
    char *s = json_dumps (obj, JSON_COMPACT);
    if (s) {
        shell_debug ("%s", s);
        free (s);
    }

    json_decref (ctx->cur);
    ctx->cur = json_incref (obj);

    if (sysmon_alert (ctx) < 0)
        shell_log_error ("alert error");

    flux_future_reset (f);
}

static int sysmon_parse_args (struct shell_sysmon *ctx, json_t *config)
{
    json_error_t jerror;
    char *settle = NULL
        ;
    if (json_is_object (config)) {
        if (json_unpack_ex (config,
                            &jerror,
                            0,
                            "{s?i s?i s?i s?s !}",
                            "full", &ctx->full,
                            "mem-alert", &ctx->mem_alert,
                            "cpu-alert", &ctx->cpu_alert,
                            "settle", &settle) < 0) {
            shell_log_error ("option parse error: %s", jerror.text);
            return -1;
        }
        if (settle) {
            if (fsd_parse_duration (settle, &ctx->settle) < 0) {
                shell_log_error ("sysmon.settle is not a valid FSD");
                return -1;
            }
        }
    }
    return 0;
}

static void sysmon_destroy (struct shell_sysmon *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        json_decref (ctx->cur);
        json_decref (ctx->alert);
        idset_destroy (ctx->cores);
        flux_future_destroy (ctx->f_mon);
        free (ctx);
        errno = saved_errno;
    }
}

static struct shell_sysmon *sysmon_create (flux_shell_t *shell, json_t *config)
{
    struct shell_sysmon *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->shell = shell;
    ctx->settle = default_settle;
    ctx->mem_alert = default_mem_alert;
    ctx->cpu_alert = default_cpu_alert;
    if (sysmon_parse_args (ctx, config) < 0)
        goto error;
    if (!(ctx->cores = idset_decode (ctx->shell->info->rankinfo.cores)))
        goto error;
    return ctx;
error:
    sysmon_destroy (ctx);
    return NULL;
}

/* Start monitoring after the local tasks have been started.
 */
static int sysmon_start (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *arg,
                         void *data)
{
    struct shell_sysmon *ctx = data;

    ctx->t_start = flux_reactor_now (flux_get_reactor (ctx->shell->h));

    if (!(ctx->f_mon = flux_rpc_pack (ctx->shell->h,
                                      "sysmon.monitor",
                                      FLUX_NODEID_ANY,
                                      FLUX_RPC_STREAMING,
                                      "{s:b}",
                                      "full", 1))
        || flux_future_then (ctx->f_mon, -1, sysmon_continuation, ctx) < 0)
        shell_log_error ("error sending sysmon.monitor request");

    return 0;
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
    if (flux_plugin_aux_set (p,
                             "sysmon",
                             ctx,
                             (flux_free_f)sysmon_destroy) < 0) {
        sysmon_destroy (ctx);
        return -1;
    }
    if (flux_plugin_add_handler (p, "shell.start", sysmon_start, ctx) < 0)
        return shell_log_errno ("failed to add shell.start handler");

    shell_debug ("sysmon is active");
    return 0;
}

struct shell_builtin builtin_sysmon = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = sysmon_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
