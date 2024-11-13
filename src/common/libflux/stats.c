/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <flux/core.h>

#include "fripp.h"

#define FRIPP_AUX_TAG "flux::fripp"

static struct fripp_ctx *get_fripp_ctx (flux_t *h)
{
    struct fripp_ctx *ctx;

    if (!(ctx = flux_aux_get (h, FRIPP_AUX_TAG))) {
        if (!(ctx = fripp_ctx_create (h)))
            return NULL;
        if (flux_aux_set (h, FRIPP_AUX_TAG, ctx,
                          (flux_free_f)fripp_ctx_destroy) == -1) {
            fripp_ctx_destroy (ctx);
            return NULL;
        }
    }
    return fripp_enabled (ctx) ? ctx : NULL;
}

void flux_stats_count (flux_t *h, const char *name, ssize_t count)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    fripp_count (ctx, name, count);
}

void flux_stats_gauge_set (flux_t *h, const char *name, ssize_t value)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    fripp_gauge (ctx, name, value, false);
}

void flux_stats_gauge_inc (flux_t *h, const char *name, ssize_t inc)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    fripp_gauge (ctx, name, inc, true);
}

void flux_stats_timing (flux_t *h, const char *name, double ms)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    fripp_timing (ctx, name, ms);
}


void flux_stats_set_period (flux_t *h, double period)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    fripp_set_agg_period (ctx, period);
}

void flux_stats_set_prefix (flux_t *h, const char *fmt, ...)
{
    struct fripp_ctx *ctx;
    if (!(ctx = get_fripp_ctx (h)))
        return;

    va_list ap;
    va_start (ap, fmt);

    char prefix[128];
    if (vsnprintf (prefix, sizeof (prefix), fmt, ap) >= 128)
        goto done;

    fripp_set_prefix (ctx, prefix);

done:
    va_end (ap);
}

bool flux_stats_enabled (flux_t *h, const char *metric)
{
    return fripp_enabled (get_fripp_ctx (h));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
