/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <json.h>

#include "tstat.h"

#include "log.h"
#include "jsonutil.h"


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

void util_json_object_add_tstat (json_object *o, const char *name,
                                 tstat_t *ts, double scale)
{
    json_object *to = util_json_object_new_object ();

    util_json_object_add_int (to, "count", tstat_count (ts));
    util_json_object_add_double (to, "min", tstat_min (ts)*scale);
    util_json_object_add_double (to, "mean", tstat_mean (ts)*scale);
    util_json_object_add_double (to, "stddev", tstat_stddev (ts)*scale);
    util_json_object_add_double (to, "max", tstat_max (ts)*scale);

    json_object_object_add (o, name, to);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
