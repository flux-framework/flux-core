/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* cron interval type definition */

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <jansson.h>
#include <flux/core.h>

#include "entry.h"

struct cron_interval {
    flux_watcher_t *w;
    double after;   /* initial timeout */
    double seconds; /* repeat interval */
};

static void interval_handler (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    cron_entry_schedule_task ((cron_entry_t *)arg);
}

static void *cron_interval_create (flux_t *h, cron_entry_t *e, json_t *arg)
{
    struct cron_interval *iv;
    double i;
    double after = -1.;
    /*  Unpack 'interval' and 'after' arguments. If after was not specified,
     *   (and thus is still < 0.0), then it is set to interval by default.
     */
    if (json_unpack (arg, "{ s:F, s?F }", "interval", &i, "after", &after)
        < 0) {
        return NULL;
    }
    if (after < 0.0)
        after = i;

    if ((iv = calloc (1, sizeof (*iv))) == NULL) {
        flux_log_error (h, "cron interval");
        return NULL;
    }
    iv->seconds = i;
    iv->after = after;
    iv->w = flux_timer_watcher_create (flux_get_reactor (h),
                                       after,
                                       i,
                                       interval_handler,
                                       (void *)e);
    if (!iv->w) {
        flux_log_error (h, "cron_interval: flux_timer_watcher_create");
        free (iv);
        return (NULL);
    }

    return (iv);
}

static void cron_interval_destroy (void *arg)
{
    struct cron_interval *iv = arg;
    flux_watcher_destroy (iv->w);
    free (iv);
}

static void cron_interval_start (void *arg)
{
    flux_watcher_start (((struct cron_interval *)arg)->w);
}

static void cron_interval_stop (void *arg)
{
    flux_watcher_stop (((struct cron_interval *)arg)->w);
}

static json_t *cron_interval_to_json (void *arg)
{
    struct cron_interval *iv = arg;
    return json_pack ("{ s:f, s:f, s:f }",
                      "interval",
                      iv->seconds,
                      "after",
                      iv->after,
                      "next_wakeup",
                      flux_watcher_next_wakeup (iv->w));
}

struct cron_entry_ops cron_interval_operations = {
    .create = cron_interval_create,
    .destroy = cron_interval_destroy,
    .start = cron_interval_start,
    .stop = cron_interval_stop,
    .tojson = cron_interval_to_json,
};

/* vi: ts=4 sw=4 expandtab
 */
