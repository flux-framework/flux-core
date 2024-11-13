/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_STATS_H
#define _FLUX_CORE_STATS_H

/* Metric types:
 * Counter - An integer value that will, on the backend (brubeck) send the
 *           count and reset to 0 at each flush. It calculates the change
 *           from the value sent at the previous flush.
 *           An example of where to use a counter is the builtin msgcounters
 *           that are part of each flux_t handle. The counts will continually
 *           increase and brubeck will handle calculating the count sent in
 *           the interval.
 * Gauge   - An integer that takes an arbitrary value and maintains its
 *           value until it is set again. Gauges can also take incremental
 *           values in the form of a + or - in front of the value which
 *           increments the previously stored value.
 *           An example of where to use a gauge is to track the current
 *           size of the broker's content-cache. At each point, the cache's
 *           sizes are independent of each other.
 * Timing  - A double value which represents the time taken for a given
 *           task in ms.
 *           An example of where to use a timer is timing the length of
 *           asynchronous loads in the broker's content-cache. The cache
 *           entry can keep track of when the load was started and then
 *           calculate and send the time taken once the entry is loaded.
 */

/* Update (or create) and store 'count' for 'name' to be sent on the
 * next flush.
 */
void flux_stats_count (flux_t *h, const char *name, ssize_t count);

/* Update (or create) and store 'value' for 'name' to be sent on the
 * next flush.
 */
void flux_stats_gauge_set (flux_t *h, const char *name, ssize_t value);

/* Update (or create) and increment 'value' for 'name' to be sent on the
 * next flush. If'name' was not previously stored, then the value is stored
 * directly (i.e. assumed 0 previous value).
 */
void flux_stats_gauge_inc (flux_t *h, const char *name, ssize_t inc);


/* Update (or create) and store 'ms' for 'name' to be sent on the
 * next flush.
 */
void flux_stats_timing (flux_t *h, const char *name, double ms);

/* Update the internal aggregation period over which metrics accumulate
 * before being set. A 'period' of '0' indicates the metrics should be
 * sent immediately. The default aggregation period is 1s.
 */
void flux_stats_set_period (flux_t *h, double period);

/* Set the prefix to be prepended to all metrics sent from the handle.
 * The prefix has a max limit of 127 characters. The default prefix is
 * flux.{{rank}}.
 */
void flux_stats_set_prefix (flux_t *h, const char *fmt, ...);

/* Check whether stats collection is enabled on the flux handle.
 * If 'metric' is non-NULL check if it is currently being tracked.
 */
bool flux_stats_enabled (flux_t *h, const char *metric);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
