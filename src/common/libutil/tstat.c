/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <math.h>

#include "tstat.h"
#include "log.h"

void tstat_push (tstat_t *ts, double x)
{
    if (ts->n == 0 || x < ts->min)
        ts->min = x;
    if (ts->n == 0 || x > ts->max)
        ts->max = x;
    /* running variance
     * ref Knuth TAOCP vol 2, 3rd edition, page 232
     * and http://www.johndcook.com/standard_deviation.html
     */
    if (++ts->n == 1) {
        ts->M = ts->newM = x;
        ts->S = 0;
    } else {
        ts->newM = ts->M + (x - ts->M) / ts->n;
        ts->newS = ts->S + (x - ts->M) * (x - ts->newM);

        ts->M = ts->newM;
        ts->S = ts->newS;
    }
}
double tstat_mean (tstat_t *ts)
{
    return (ts->n > 0) ? ts->newM : 0;
}
double tstat_min (tstat_t *ts)
{
    return ts->min;
}
double tstat_max (tstat_t *ts)
{
    return ts->max;
}
double tstat_variance (tstat_t *ts)
{
    return (ts->n > 1) ? ts->newS / (ts->n - 1) : 0;
}
double tstat_stddev (tstat_t *ts)
{
    return sqrt (tstat_variance (ts));
}
int tstat_count (tstat_t *ts)
{
    return ts->n;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
