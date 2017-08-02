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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/cronodate.h"

#include "entry.h"

struct datetime_entry {
    flux_t *h;
    flux_watcher_t *w;
    cronodate_t *d;
};

void datetime_entry_destroy (struct datetime_entry *dt)
{
    dt->h = NULL;
    flux_watcher_destroy (dt->w);
    cronodate_destroy (dt->d);
    free (dt);
}

struct datetime_entry * datetime_entry_create ()
{
    struct datetime_entry *dt = calloc (1, sizeof (*dt));
    if (dt) {
        dt->d = cronodate_create ();
    }
    return (dt);
}

static struct datetime_entry * datetime_entry_from_json (json_object *o)
{
    int i;
    struct datetime_entry *dt = datetime_entry_create ();

    for (i = 0; i < TM_MAX_ITEM; i++) {
        const char *range;
        if (!Jget_str (o, tm_unit_string (i), &range))
            range = "*";
        if (cronodate_set (dt->d, i, range) < 0) {
            datetime_entry_destroy (dt);
            return (NULL);
        }
    }
    return (dt);
}

static void cron_datetime_start (void *arg)
{
    struct datetime_entry *dt = arg;
    flux_watcher_start (dt->w);
}

static void cron_datetime_stop (void *arg)
{
    struct datetime_entry *dt = arg;
    flux_watcher_stop (dt->w);
}

void datetime_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    cron_entry_t *e = arg;
    cron_entry_schedule_task (e);
}

static double reschedule_cb (flux_watcher_t *w, double now, void *arg)
{
    cron_entry_t *e = arg;
    struct datetime_entry *dt = cron_entry_type_data (e);
    double next = now + cronodate_remaining (dt->d, now);
    /* If we failed to get next timestamp, push timeout far into the future
     *  and stop the cron entry in an ev_prepare callback. See ev(7).
     */
    if (next < now) {
        /*  Only issue an error if this entry has more than one repeat:
         */
        if (e->repeat == 0 || e->repeat < e->stats.count + 1) {
            flux_log_error (dt->h,
                    "cron-%ju: Unable to get next wakeup. Stopping.", e->id);
        }
        cron_entry_stop_safe (e);
        return now + 1.e19;
    }
    return (next);
}

static void *cron_datetime_create (flux_t *h, cron_entry_t *e, json_object *arg)
{
    struct datetime_entry *dt = datetime_entry_from_json (arg);
    if (dt == NULL)
        return (NULL);
    dt->h = h;
    dt->w = flux_periodic_watcher_create (flux_get_reactor (h),
                                         0., 0.,
                                         reschedule_cb,
                                         datetime_cb, (void *) e);
    if (dt->w == NULL) {
        flux_log_error (h, "periodic_watcher_create");
        datetime_entry_destroy (dt);
        return (NULL);
    }
    return (dt);
}

static json_object *cron_datetime_to_json (void *arg)
{
    int i;
    struct datetime_entry *dt = arg;
    json_object *o = Jnew ();
    if (dt->w)
        Jadd_double (o, "next_wakeup", flux_watcher_next_wakeup (dt->w));
    for (i = 0; i < TM_MAX_ITEM; i++)
        Jadd_str (o, tm_unit_string (i), cronodate_get (dt->d, i));
    return (o);
}

static void cron_datetime_destroy (void *arg)
{
    datetime_entry_destroy (arg);
}

struct cron_entry_ops cron_datetime_operations = {
    .create =   cron_datetime_create,
    .destroy =  cron_datetime_destroy,
    .start =    cron_datetime_start,
    .stop =     cron_datetime_stop,
    .tojson =   cron_datetime_to_json
};

 /* vi:tabstop=4 shiftwidth=4 expandtab
 */
