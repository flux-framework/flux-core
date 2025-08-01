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
 *
 * Heartbeats are published on rank 0 (leader)
 * Heartbeats are subscribed to on rank > 0 (followers)
 *
 * By default, if a follower broker does not receive heartbeats within
 * a timeout window (5m), it forces an overlay parent disconnect.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <flux/core.h>
#include <jansson.h>
#include <math.h>

#include "src/broker/module.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

static const double default_period = 2.0;
static const double default_timeout = 300.;

static const int default_warn_thresh = 3; // number of heartbeat periods

struct heartbeat {
    flux_t *h;
    uint32_t rank;
    double period;
    double timeout;
    flux_watcher_t *timer;
    flux_msg_handler_t **handlers;
    flux_future_t *f;
    flux_future_t *sync;
    json_int_t count;
    double t_stamp;
    int warn_thresh;
    bool over_warn_thresh;
};

static void heartbeat_stats_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct heartbeat *hb = arg;

    if (flux_respond_pack (h,
                           msg,
                           "{s:f s:f s:I s:i}",
                           "period", hb->period,
                           "timeout", hb->timeout,
                           "count", hb->count,
                           "warn_thresh", hb->warn_thresh) < 0)
        flux_log_error (h, "error responding to stats-get request");
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct heartbeat *hb = arg;
    double now = flux_reactor_now (flux_get_reactor (hb->h));

    if (flux_future_get (f, NULL) < 0) {
        if (errno != ETIMEDOUT) {
            flux_log_error (hb->h, "unexpected sync error");
            goto done;
        }
        char buf[64] = "unknown period";
        char msg[128];
        (void)fsd_format_duration_ex (buf, sizeof (buf), now - hb->t_stamp, 2);
        snprintf (msg, sizeof (msg), "no heartbeat for %s", buf);

        flux_future_t *f2;
        if (!(f2 = flux_rpc_pack (hb->h,
                                  "overlay.disconnect-parent",
                                  FLUX_NODEID_ANY,
                                  FLUX_RPC_NORESPONSE,
                                  "{s:s}",
                                  "reason", msg)))
            flux_log_error (hb->h, "overlay.disconnect-parent");
        flux_future_destroy (f2);
    }
    else {
        hb->count++;
        hb->t_stamp = now;
    }
done:
    flux_future_reset (f);
}

static void publish_continuation (flux_future_t *f, void *arg)
{
    struct heartbeat *hb = arg;

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (hb->h, "error publishing heartbeat");

    flux_future_destroy (f);
    hb->f = NULL;
}

static void heartbeat_publish (struct heartbeat *hb)
{
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
    hb->count++;
}

static void heartbeat_warn (struct heartbeat *hb)
{
    double now = flux_reactor_now (flux_get_reactor (hb->h));
    bool over_thresh = false;

    if (now - hb->t_stamp > hb->period * hb->warn_thresh)
        over_thresh = true;

    if (over_thresh && !hb->over_warn_thresh) {
        flux_log (hb->h, LOG_WARNING, "heartbeat overdue");
        hb->over_warn_thresh = true;
    }
    else if (!over_thresh && hb->over_warn_thresh) {
        flux_log (hb->h, LOG_WARNING, "heartbeat received");
        hb->over_warn_thresh = false;
    }
}

static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct heartbeat *hb = arg;

    if (hb->rank == 0)
        heartbeat_publish (hb);
    else
        heartbeat_warn (hb);
}

static void heartbeat_period_adjust (struct heartbeat *hb, double period)
{
    if (hb->timer) {
        flux_timer_watcher_reset (hb->timer, 0., period);
        flux_timer_watcher_again (hb->timer);
    }
}

static int heartbeat_timeout_adjust (struct heartbeat *hb, double timeout)
{
    if (hb->sync) {
        flux_future_t *f;
        if (!(f = flux_sync_create (hb->h, 0.))
            || flux_future_then (f, timeout, sync_cb, hb) < 0)
            return -1;
        hb->sync = f;
    }
    return 0;
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
    const char *timeout_fsd = NULL;
    double new_period = default_period;
    double new_timeout = default_timeout;
    int new_warn_thresh = default_warn_thresh;

    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?{s?s s?s s?i !}}",
                          "heartbeat",
                            "period", &period_fsd,
                            "timeout", &timeout_fsd,
                            "warn_thresh", &new_warn_thresh) < 0) {
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
    if (timeout_fsd) {
        if (fsd_parse_duration (timeout_fsd, &new_timeout) < 0) {
            errprintf (error, "error parsing heartbeat.timeout FSD value");
            return -1;
        }
        if (new_timeout == 0 || new_timeout == INFINITY)
            new_timeout = -1;
    }
    if (new_timeout < new_period * 2 && new_timeout != -1) {
            errprintf (error,
                       "heartbeat.timeout must be >= 2*heartbeat.period,"
                       " infinity, or 0");
            errno = EINVAL;
            return -1;
    }
    if (new_period != hb->period) {
        heartbeat_period_adjust (hb, new_period);
        hb->period = new_period;
    }
    if (new_timeout != hb->timeout) {
        if (heartbeat_timeout_adjust (hb, new_timeout) < 0) {
            errprintf (error, "error adjusting timeout: %s", strerror (errno));
            return -1;
        }
        hb->timeout = new_timeout;
    }
    if (new_warn_thresh <= 0) {
        errprintf (error, "heartbeat.warn_thresh must be positive");
        errno = EINVAL;
        return -1;
    }
    hb->warn_thresh = new_warn_thresh;
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
        flux_future_destroy (hb->sync);
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
    hb->timeout = default_timeout;
    hb->t_stamp = flux_reactor_now (flux_get_reactor (h));
    hb->warn_thresh = default_warn_thresh;
    if (flux_get_rank (h, &hb->rank) < 0
        || flux_msg_handler_addvec (hb->h, htab, hb, &hb->handlers) < 0)
        goto error;
    return hb;
error:
    heartbeat_destroy (hb);
    return NULL;
}

static int mod_main (flux_t *h, int argc, char **argv)
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
    if (!(hb->timer = flux_timer_watcher_create (r,
                                                 0.,
                                                 hb->period,
                                                 timer_cb,
                                                 hb)))
        goto error;
    flux_watcher_start (hb->timer);
    if (hb->rank > 0) {
        if (!(hb->sync = flux_sync_create (h, 0))
            || flux_future_then (hb->sync, hb->timeout, sync_cb, hb) < 0)
            goto error;
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

struct module_builtin builtin_heartbeat = {
    .name = "heartbeat",
    .main = mod_main,
    .autoload = false,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
