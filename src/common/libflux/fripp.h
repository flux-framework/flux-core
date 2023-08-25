/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_FRIPP_H
#define _FLUX_CORE_FRIPP_H

#include <stdarg.h>

#include "handle.h"

struct fripp_ctx;

struct fripp_ctx *fripp_ctx_create (flux_t *h);
void fripp_ctx_destroy (struct fripp_ctx *ctx);

/* Format and append a packet to the internal queue to be sent on the
 * next flush.
 */
int fripp_packet_appendf (struct fripp_ctx *ctx, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

/* Format and send a single packet immediately.
 */
int fripp_sendf (struct fripp_ctx *ctx, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

/* Update (or create) and store 'count' for 'name' to be sent on the
 * next flush.
 */
int fripp_count (struct fripp_ctx *ctx, const char *name, ssize_t count);

/* Update (or create) and store 'value' for 'name' to be sent on the
 * next flush. The'inc' indicates whether or not 'value' is some delta on
 * the previous value. If 'inc' is set and 'name' was not previously stored,
 * then the value is stored directly.
 */
int fripp_gauge (struct fripp_ctx *ctx, const char *name, ssize_t value, bool inc);

/* Update (or create) and store 'ms' for 'name' to be sent on the
 * next flush.
 */
int fripp_timing (struct fripp_ctx *ctx, const char *name, double ms);

/* Update the internal aggregation period over which metrics accumulate
 * before being set. A 'period' of '0' indicates the metrics should be
 * sent immediately.
 */
void fripp_set_agg_period (struct fripp_ctx *ctx, double period);

/* Check whether fripp collection is enabled.
 */
bool fripp_enabled (struct fripp_ctx *ctx);

/* Set the prefix to be prepended to all metrics sent from the handle.
 * The prefix has a max limit of 127 characters. The default prefix is
 * flux.{{rank}}.
 */
void fripp_set_prefix (struct fripp_ctx *ctx, const char *prefix);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
