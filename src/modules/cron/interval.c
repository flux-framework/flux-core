/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

/* cron interval type definition */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "entry.h"

struct cron_interval {
    flux_watcher_t *w;
    double          after;   /* initial timeout */
    double          seconds; /* repeat interval */
};


static void interval_handler (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    cron_entry_schedule_task ((cron_entry_t *)arg);
}

static void *cron_interval_create (flux_t *h, cron_entry_t *e, json_object *arg)
{
    struct cron_interval *iv;
    double i;
    double after;

    if (!(Jget_double (arg, "interval", &i)))
        return NULL;
    if (!(Jget_double (arg, "after", &after)))
        after = i;

    if ((iv = calloc (1, sizeof (*iv))) == NULL) {
        flux_log_error (h, "cron interval");
        return NULL;
    }
    iv->seconds = i;
    iv->after = after;
    iv->w = flux_timer_watcher_create (flux_get_reactor (h),
                                       after, i, interval_handler,
                                       (void *) e);
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

static json_object *cron_interval_to_json (void *arg)
{
    struct cron_interval *iv = arg;
    json_object *o = Jnew ();
    Jadd_double (o, "interval", iv->seconds);
    Jadd_double (o, "after",    iv->after);
    Jadd_double (o, "next_wakeup",
                 flux_watcher_next_wakeup (iv->w));
    return (o);
}

struct cron_entry_ops cron_interval_operations = {
    .create  = cron_interval_create,
    .destroy = cron_interval_destroy,
    .start =   cron_interval_start,
    .stop =    cron_interval_stop,
    .tojson =  cron_interval_to_json,
};

/* vi: ts=4 sw=4 expandtab
 */
