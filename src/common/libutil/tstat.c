/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
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
        ts->newM = ts->M + (x - ts->M)/ts->n;
        ts->newS = ts->S + (x - ts->M)*(x - ts->newM);

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
    return (ts->n > 1) ? ts->newS/(ts->n - 1) : 0;
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
