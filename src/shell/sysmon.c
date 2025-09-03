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
    char *path_memory_current;
    char *path_memory_peak;
    char *path_cpu_stat;
    struct sample first_cpu;
    struct sample prev_cpu;
};

/* Read the contents of 'path' into NULL terminated buffer.
 * Caller must free.
 */
static char *read_file (const char *path)
{
    int fd;
    char *buf;
    if ((fd = open (path, O_RDONLY)) < 0)
        return NULL;
    if (read_all (fd, (void **)&buf) < 0) {
        ERRNO_SAFE_WRAP (close, fd);
        return NULL;
    }
    close (fd);
    return buf;
}

/* Determine the cgroup v2 path to 'name' for for pid.
 * Check access(2) to the file according to 'mode' mask.
 * Caller must free.
 */
static char *get_cgroup_path (pid_t pid, const char *name, int mode)
{
    char tmp[1024];
    char *cg;
    char *path;

    snprintf (tmp, sizeof (tmp), "/proc/%d/cgroup", (int)pid);
    if (!(cg = read_file (tmp)))
        return NULL;
    if (!strstarts (cg, "0::")) // v2 always begins with 0::
        goto eproto;
    snprintf (tmp,
              sizeof (tmp),
              "/sys/fs/cgroup/%s/%s",
              strstrip (cg + 3),
              name);
    if (!(path = realpath (tmp, NULL)))
        goto error;
    if (access (path, mode) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        goto error;
    }
    free (cg);
    return path;
eproto:
    errno = EPROTO;
error:
    ERRNO_SAFE_WRAP (free, cg);
    return NULL;
}

static int get_cgroup_value (const char *path, char *buf, size_t len)
{
    char *s;
    int rc = -1;

    if (!(s = read_file (path)))
        return -1;
    if (snprintf (buf, len, "%s", strstrip (s)) >= len)
        goto out;
    rc = 0;
out:
    free (s);
    return rc;
}

static const char *get_cgroup_size (const char *path)
{
    uint64_t size;
    char rawbuf[32];

    if (get_cgroup_value (path, rawbuf, sizeof (rawbuf)) < 0
        || parse_size (rawbuf, &size) < 0)
        return "unknown";
    return encode_size (size);
}

static int parse_cpu_stat (const char *s,
                           const char *key,
                           unsigned long long *valp)
{
    char *argz = NULL;
    size_t argz_len = 0;
    int e;

    if ((e = argz_create_sep (s, '\n', &argz, &argz_len)) != 0) {
        errno = e;
        return -1;
    }
    char *entry = NULL;
    while ((entry = argz_next (argz, argz_len, entry))) {
        if (strstarts (entry, key) && isblank (entry[strlen (key)])) {
            unsigned long long val;
            char *endptr;
            errno = 0;
            val = strtoull (&entry[strlen (key) + 1], &endptr, 10);
            if (errno == 0 && *endptr == '\0') {
                *valp = val;
                free (argz);
                return 0;
            }
        }
    }
    free (argz);
    errno = EPROTO;
    return -1;
}

static int get_cpu_stat (struct shell_sysmon *ctx,
                         const char *key,
                         struct sample *sample)
{
    char rawbuf[1024];

    if (get_cgroup_value (ctx->path_cpu_stat, rawbuf, sizeof (rawbuf)) < 0
        || parse_cpu_stat (rawbuf, key, &sample->val) < 0)
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
    shell_trace ("memory.current=%s",
                 get_cgroup_size (ctx->path_memory_current));

    struct sample cur_cpu;
    if (get_cpu_stat (ctx, "usage_usec", &cur_cpu) == 0) {
        shell_trace ("loadavg=%.2f", cpu_load_avg (&ctx->prev_cpu, &cur_cpu));
        ctx->prev_cpu = cur_cpu;
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

    shell_log ("memory.peak=%s", get_cgroup_size (ctx->path_memory_peak));

    struct sample cur_cpu;
    if (get_cpu_stat (ctx, "usage_usec", &cur_cpu) == 0) {
        shell_log ("loadavg-overall=%.2f",
                   cpu_load_avg (&ctx->first_cpu, &cur_cpu));
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

    if (get_cpu_stat (ctx, "usage_usec", &ctx->first_cpu) < 0) {
        shell_log_error ("error sampling cpu.stat.usage_usec");
        return -1;
    }
    ctx->prev_cpu = ctx->first_cpu;

    if (ctx->shell->verbose >= 2) {
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
        free (ctx->path_memory_current);
        free (ctx->path_memory_peak);
        free (ctx->path_cpu_stat);
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
    pid_t mypid = getpid ();

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->shell = shell;
    ctx->period = -1;
    if (sysmon_parse_args (ctx, config) < 0)
        goto error;
    ctx->path_memory_current = get_cgroup_path (mypid, "memory.current", R_OK);
    ctx->path_memory_peak = get_cgroup_path (mypid, "memory.peak", R_OK);
    ctx->path_cpu_stat = get_cgroup_path (mypid, "cpu.stat", R_OK);
    if (!ctx->path_memory_current
        || !ctx->path_memory_peak
        || !ctx->path_cpu_stat) {
        shell_log_error ("error caching cgroup paths");
        goto error;
    }
    return ctx;
error:
    sysmon_destroy (ctx);
    return NULL;
}

static bool cgroup_check (void)
{
    char *path;
    if (!(path = get_cgroup_path (getpid (), "memory.current", R_OK)))
        return false;
    free (path);
    return true;
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
    if (!config || !cgroup_check())
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
