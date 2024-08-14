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
#include "entry.h"

/* event handler
 */
struct cron_event {
    flux_t *h;
    flux_msg_handler_t *mh;
    int paused;
    double min_interval;
    int nth;
    int after;
    int counter;
    char *event;
};

static void ev_timer_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    cron_entry_t *e = arg;
    struct cron_event *ev = cron_entry_type_data (e);
    cron_entry_schedule_task (e);
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
    ev->paused = 0;
}

static void event_handler (flux_t *h,
                           flux_msg_handler_t *w,
                           const flux_msg_t *msg,
                           void *arg)
{
    cron_entry_t *e = arg;
    struct cron_event *ev = cron_entry_type_data (e);

    ev->counter++;
    if (ev->paused)
        return;
    /*
     *  Determine if we should run the cron task on this event
     *   based on the current values for "after" and "nth".
     *   If ev->after is set, then only run for the first time
     *   after we've seen this many events. If ev->nth is set,
     *   only run every nth event starting with 'after'.
     */
    if (ev->counter < ev->after)
        return;
    if (ev->nth && ((ev->counter - ev->after) % ev->nth))
        return;
    if (ev->min_interval > 0.) {
        double now = get_timestamp ();
        double remaining = ev->min_interval - (now - e->stats.lastrun);
        if (remaining > 1e-5) {
            flux_reactor_t *r = flux_get_reactor (h);
            flux_watcher_t *w;
            if (!(w = flux_timer_watcher_create (r,
                                                 remaining,
                                                 0.,
                                                 ev_timer_cb,
                                                 (void *) e)))
                flux_log_error (h, "timer_watcher_create");
            else {
                /* Pause the event watcher. Continue to count events but
                 *  don't run anything until we unpause.
                 */
                ev->paused = 1;
                flux_watcher_start (w);
                flux_log (h,
                          LOG_DEBUG,
                          "cron-%ju: delaying %4.03fs due to min interval",
                          e->id,
                          remaining);
            }
            return;
        }
    }
    cron_entry_schedule_task (e);
}

static void cron_event_destroy (void *arg)
{
    struct cron_event *ev = arg;
    if (ev == NULL)
        return;

    if (ev->mh)
        flux_msg_handler_destroy (ev->mh);
    if (ev->h && ev->event)
        (void) flux_event_unsubscribe (ev->h, ev->event);
    free (ev->event);
    free (ev);
}

static void *cron_event_create (flux_t *h, cron_entry_t *e, json_t *arg)
{
    struct cron_event *ev;
    int nth = 0;
    int after = 0;
    double min_interval = 0.;
    const char *event;
    struct flux_match match = FLUX_MATCH_EVENT;

    if (json_unpack (arg,
                     "{s:s s?i s?i s?F}",
                     "topic", &event,
                     "nth",   &nth,
                     "after", &after,
                     "min_interval", &min_interval) < 0) {
        flux_log_error (h, "cron event: json_unpack");
        errno = EPROTO;
        return NULL;
    }

    if ((ev = calloc (1, sizeof (*ev))) == NULL) {
        flux_log_error (h, "cron event: calloc");
        return NULL;
    }

    /* Call subscribe per cron entry. Multiple event subscriptions are
     *  allowed and each cron_event entry will have a corresponding
     *  unsubscribe
     */
    if (flux_event_subscribe (h, event) < 0) {
        flux_log_error (h, "cron_event: subscribe");
        goto fail;
    }
    /* Save a copy of this handle for event unsubscribe at destroy */
    ev->h = h;
    ev->nth = nth;
    ev->after = after;
    ev->min_interval = min_interval;
    ev->counter = 0;

    if ((ev->event = strdup (event)) == NULL) {
        flux_log_error (h, "cron event: strdup");
        goto fail;
    }

    match.topic_glob = ev->event;
    ev->mh = flux_msg_handler_create (h, match, event_handler, (void *)e);
    if (!ev->mh) {
        flux_log_error (h, "cron_event: flux_msg_handler_create");
        goto fail;
    }

    return (ev);
fail:
    cron_event_destroy (ev);
    return (NULL);
}

static void cron_event_start (void *arg)
{
    struct cron_event *ev = arg;
    ev->counter = 0;
    flux_msg_handler_start (ev->mh);
}

static void cron_event_stop (void *arg)
{
    flux_msg_handler_stop (((struct cron_event *)arg)->mh);
}

static json_t *cron_event_to_json (void *arg)
{
    struct cron_event *ev = arg;
    return json_pack ("{ s:s, s:i, s:i, s:i, s:f }",
                      "topic", ev->event,
                      "nth", ev->nth,
                      "after", ev->after,
                      "counter", ev->counter,
                      "min_interval", ev->min_interval);
}

struct cron_entry_ops cron_event_operations = {
    .create =  cron_event_create,
    .destroy = cron_event_destroy,
    .start =   cron_event_start,
    .stop =    cron_event_stop,
    .tojson =  cron_event_to_json
};
