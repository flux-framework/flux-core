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
#include <czmq.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "shutdown.h"

#define REASON_MAX 256

struct shutdown_struct {
    flux_t *h;
    flux_watcher_t *timer;
    flux_msg_handler_t *shutdown;
    uint32_t myrank;

    int rc;
    int rank;
    char reason[REASON_MAX];
    double grace;

    shutdown_cb_f cb;
    void *arg;
};


shutdown_t *shutdown_create (void)
{
    shutdown_t *s = xzmalloc (sizeof (*s));
    return s;
}

void shutdown_destroy (shutdown_t *s)
{
    if (s) {
        if (s->shutdown)
            flux_msg_handler_destroy (s->shutdown);
        if (s->h)
            (void)flux_event_unsubscribe (s->h, "shutdown");
        free (s);
    }
}

/* When the timer expires, call the registered callback.
 */
static void timer_handler (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    shutdown_t *s = arg;
    if (s->shutdown)
        flux_msg_handler_stop (s->shutdown);
    if (s->cb)
        s->cb (s, true, s->arg);
    else
        exit (s->rc);
}

/* On receipt of the shutdown event message, begin the grace timer,
 * and log the "shutdown in..." message on rank 0.
 */
void shutdown_handler (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    shutdown_t *s = arg;

    if (!s->timer) {
        if (shutdown_decode (msg, &s->grace, &s->rc, &s->rank,
                             s->reason, sizeof (s->reason)) < 0) {
            flux_log_error (h, "shutdown event");
            return;
        }
        if (!(s->timer = flux_timer_watcher_create (flux_get_reactor (s->h),
                                                    s->grace, 0.,
                                                    timer_handler, s))) {
            flux_log_error (h, "shutdown timer creation");
            return;
        }
        flux_watcher_start (s->timer);
        if (s->myrank == 0)
            flux_log (s->h, LOG_INFO, "shutdown in %.3fs: %s",
                      s->grace, s->reason);
    }
}

void shutdown_set_handle (shutdown_t *s, flux_t *h)
{
    struct flux_match match = FLUX_MATCH_EVENT;

    s->h = h;

    match.topic_glob = "shutdown";
    if (!(s->shutdown = flux_msg_handler_create (s->h, match,
                                                 shutdown_handler, s)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (s->shutdown);
    if (flux_event_subscribe (s->h, "shutdown") < 0)
        log_err_exit ("flux_event_subscribe");

    if (flux_get_rank (s->h, &s->myrank) < 0)
        log_err_exit ("flux_get_rank");
}

void shutdown_set_callback (shutdown_t *s, shutdown_cb_f cb, void *arg)
{
    s->cb = cb;
    s->arg = arg;
}

int shutdown_get_rc (shutdown_t *s)
{
    return s->rc;
}

void shutdown_disarm (shutdown_t *s)
{
    if (s->timer) {
        flux_watcher_stop (s->timer);
        flux_watcher_destroy (s->timer);
        s->timer = NULL;
    }
}

flux_msg_t *shutdown_vencode (double grace, int exitcode, int rank,
                              const char *fmt, va_list ap)
{
    char reason[REASON_MAX];

    vsnprintf (reason, sizeof (reason), fmt, ap);

    return flux_event_pack ("shutdown", "{ s:s s:f s:i s:i }",
                            "reason", reason,
                            "grace", grace,
                            "rank", rank,
                            "exitcode", exitcode);
}

flux_msg_t *shutdown_encode (double grace, int exitcode, int rank,
                             const char *fmt, ...)
{
    va_list ap;
    flux_msg_t *msg;

    va_start (ap, fmt);
    msg = shutdown_vencode (grace, exitcode, rank, fmt, ap);
    va_end (ap);

    return msg;
}

int shutdown_decode (const flux_msg_t *msg, double *grace, int *exitcode,
                     int *rank, char *reason, int reason_len)
{
    const char *s;
    int rc = -1;

    if (flux_event_unpack (msg, NULL, "{ s:s s:F s:i s:i}",
                           "reason", &s,
                           "grace", grace,
                           "rank", rank,
                           "exitcode", exitcode) < 0)
        goto done;
    snprintf (reason, reason_len, "%s", s);
    rc = 0;
done:
    return rc;
}

int shutdown_arm (shutdown_t *s, double grace, int exitcode,
                  const char *fmt, ...)
{
    va_list ap;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!s->timer) {
        va_start (ap, fmt);
        msg = shutdown_vencode (grace, exitcode, s->myrank, fmt, ap);
        va_end (ap);
        if (!msg || flux_send (s->h, msg, 0) < 0)
            goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
