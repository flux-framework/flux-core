/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/cronodate.h"
#include "src/common/libutil/errno_safe.h"

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
        if (!(dt->d = cronodate_create ())) {
            ERRNO_SAFE_WRAP (free, dt);
            return NULL;
        }
        /*  Fill cronodate set initially. The cronodate object will
         *  be refined when json arguments from user are processed
         */
        cronodate_fillset (dt->d);
    }
    return (dt);
}

static struct datetime_entry * datetime_entry_from_json (json_t *o)
{
    int i, rc = 0;
    struct datetime_entry *dt = datetime_entry_create ();

    if (!dt)
        return NULL;

    for (i = 0; i < TM_MAX_ITEM; i++) {
        json_t *val;
        /*  Time unit members of the json arguments are optional.
         *  If missing then the default of "*" is assumed.
         */
        if ((val = json_object_get (o, tm_unit_string (i)))) {
            /*  Value may either be a string range, or single integer.
             *  Allow either to be encoded in json.
             */
            if (json_is_string (val))
                rc = cronodate_set (dt->d, i, json_string_value (val));
            else if (json_is_integer (val))
                rc = cronodate_set_integer (dt->d, i, json_integer_value (val));
            else
                rc = -1;
        }
        if (rc < 0) {
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
                            "cron-%ju: Unable to get next wakeup. Stopping.",
                            (uintmax_t)e->id);
        }
        cron_entry_stop_safe (e);
        return now + 1.e19;
    }
    return (next);
}

static void *cron_datetime_create (flux_t *h, cron_entry_t *e, json_t *arg)
{
    struct datetime_entry *dt = datetime_entry_from_json (arg);
    if (dt == NULL)
        return (NULL);
    dt->h = h;
    dt->w = flux_periodic_watcher_create (flux_get_reactor (h),
                                          0.,
                                          0.,
                                          reschedule_cb,
                                          datetime_cb, (void *) e);
    if (dt->w == NULL) {
        flux_log_error (h, "periodic_watcher_create");
        datetime_entry_destroy (dt);
        return (NULL);
    }
    return (dt);
}

static json_t *cron_datetime_to_json (void *arg)
{
    int i;
    struct datetime_entry *dt = arg;
    json_t *o = json_object ();
    if (dt->w) {
        json_t *x = json_real (flux_watcher_next_wakeup (dt->w));
        if (x)
            json_object_set_new (o, "next_wakeup", x);
    }
    for (i = 0; i < TM_MAX_ITEM; i++) {
        json_t *x = json_string (cronodate_get (dt->d, i));
        if (x)
            json_object_set_new (o, tm_unit_string (i), x);
    }
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
