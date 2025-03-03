/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* heartbeat.c - publish regular heartbeat messages
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

static const double default_period = 2.0;

struct heartbeat {
    flux_t *h;
    uint32_t rank;
    double period;
    flux_watcher_t *timer;
    flux_msg_handler_t **handlers;
    flux_future_t *f;
};

static void heartbeat_stats_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct heartbeat *hb = arg;

    if (flux_respond_pack (h,
                           msg,
                           "{s:f}",
                           "period", hb->period) < 0)
        flux_log_error (h, "error responding to stats-get request");
}

static void publish_continuation (flux_future_t *f, void *arg)
{
    struct heartbeat *hb = arg;

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (hb->h, "error publishing heartbeat");

    flux_future_destroy (f);
    hb->f = NULL;
}

static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct heartbeat *hb = arg;

    flux_future_destroy (hb->f);

    if (!(hb->f = flux_event_publish (hb->h, "heartbeat.pulse", 0, NULL))) {
        flux_log_error (hb->h, "error sending publish request");
        return;
    }
    if (flux_future_then (hb->f, -1, publish_continuation, hb) < 0) {
        flux_log_error (hb->h, "error setting up continuation");
        flux_future_destroy (hb->f);
        hb->f = NULL;
    }
}

static int heartbeat_parse_args (struct heartbeat *hb,
                                 int argc,
                                 char **argv,
                                 flux_error_t *error)
{
    int i;

    for (i = 0; i < argc; i++) {
        if (strstarts (argv[i], "period=")) {
            if (fsd_parse_duration (argv[i] + 7, &hb->period) < 0) {
                errprintf (error,
                           "period: error parsing FSD: %s",
                           strerror (errno));
                return -1;
            }
        }
        else {
            errprintf (error, "%s: unknown option", argv[i]);
            goto inval;
        }
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

static int heartbeat_parse_config (struct heartbeat *hb,
                                   const flux_conf_t *conf,
                                   flux_error_t *error)
{
    flux_error_t conf_error;
    const char *period_fsd = NULL;
    double new_period = default_period;

    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?{s?s !}}",
                          "heartbeat",
                            "period", &period_fsd) < 0) {
        errprintf (error,
                   "error reading [heartbeat] config table: %s",
                   conf_error.text);
        return -1;
    }
    if (period_fsd) {
        if (fsd_parse_duration (period_fsd, &new_period) < 0) {
            errprintf (error, "error parsing heartbeat.period FSD value");
            return -1;
        }
        if (new_period <= 0.) {
            errprintf (error, "heartbeat.period must be a positive FSD value");
            errno = EINVAL;
            return -1;
        }
    }
    if (new_period != hb->period) {
        hb->period = new_period;
        if (hb->timer) {
            flux_timer_watcher_reset (hb->timer, 0., hb->period);
            flux_timer_watcher_again (hb->timer);
        }
    }
    return 0;
}

static void heartbeat_config_reload_cb (flux_t *h,
                                        flux_msg_handler_t *mh,
                                        const flux_msg_t *msg,
                                        void *arg)
{
    struct heartbeat *hb = arg;
    const flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0)
        goto error;
    if (heartbeat_parse_config (hb, conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (flux_set_conf (h, flux_conf_incref (conf)) < 0) {
        errstr = "error updating cached configuration";
        flux_conf_decref (conf);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "heartbeat.stats-get",
        heartbeat_stats_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "heartbeat.config-reload",
        heartbeat_config_reload_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void heartbeat_destroy (struct heartbeat *hb)
{
    if (hb) {
        int saved_errno = errno;
        flux_future_destroy (hb->f);
        flux_msg_handler_delvec (hb->handlers);
        flux_watcher_destroy (hb->timer);
        free (hb);
        errno = saved_errno;
    }
}

static struct heartbeat *heartbeat_create (flux_t *h)
{
    struct heartbeat *hb;

    if (!(hb = calloc (1, sizeof (*hb))))
        return NULL;
    hb->h = h;
    hb->period = default_period;
    if (flux_get_rank (h, &hb->rank) < 0
        || flux_msg_handler_addvec (hb->h, htab, hb, &hb->handlers) < 0)
        goto error;
    return hb;
error:
    heartbeat_destroy (hb);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct heartbeat *hb;
    flux_reactor_t *r = flux_get_reactor (h);
    flux_error_t error;

    if (!(hb = heartbeat_create (h)))
        return -1;
    if (heartbeat_parse_config (hb, flux_get_conf (h), &error) < 0
        || heartbeat_parse_args (hb, argc, argv, &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (hb->rank == 0) {
        if (!(hb->timer = flux_timer_watcher_create (r,
                                                     0.,
                                                     hb->period,
                                                     timer_cb,
                                                     hb)))
            goto error;
        flux_watcher_start (hb->timer);
    }
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    heartbeat_destroy (hb);
    return 0;
error:
    heartbeat_destroy (hb);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
