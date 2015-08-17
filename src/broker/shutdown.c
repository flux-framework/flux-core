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
#include "src/common/libutil/shortjson.h"

#include "shutdown.h"

#define REASON_MAX 256

struct shutdown_struct {
    flux_t h;
    flux_timer_watcher_t *w;

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
        free (s);
    }
}

void shutdown_set_handle (shutdown_t *s, flux_t h)
{
    s->h = h;
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
    if (s->w) {
        flux_timer_watcher_stop (s->h, s->w);
        flux_timer_watcher_destroy (s->w);
        s->w = NULL;
    }
}

static void shutdown_cb (flux_t h, flux_timer_watcher_t *w,
                         int revents, void *arg)
{
    shutdown_t *s = arg;
    if (s->cb)
        s->cb (s, s->arg);
}

flux_msg_t *shutdown_vencode (double grace, int exitcode, int rank,
                              const char *fmt, va_list ap)
{
    flux_msg_t *msg;
    JSON out = Jnew ();
    char reason[REASON_MAX];

    vsnprintf (reason, sizeof (reason), fmt, ap);

    Jadd_str (out, "reason", reason);
    Jadd_double (out, "grace", grace);
    Jadd_int (out, "rank", rank);
    Jadd_int (out, "exitcode", exitcode);
    msg = flux_event_encode ("shutdown", Jtostr (out));
    Jput (out);
    return msg;
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
    const char *json_str, *s;
    JSON in = NULL;
    int rc = -1;

    if (flux_event_decode (msg, NULL, &json_str) < 0
                || !(in = Jfromstr (json_str))
                || !Jget_str (in, "reason", &s)
                || !Jget_double (in, "grace", grace)
                || !Jget_int (in, "rank", rank)
                || !Jget_int (in, "exitcode", exitcode)) {
        errno = EPROTO;
        goto done;
    }
    snprintf (reason, reason_len, "%s", s);
    rc = 0;
done:
    Jput (in);
    return rc;
}

int shutdown_recvmsg (shutdown_t *s, const flux_msg_t *msg)
{
    int rc = -1;

    if (!s->w) {
        if (shutdown_decode (msg, &s->grace, &s->rc, &s->rank,
                             s->reason, sizeof (s->reason)) < 0)
            goto done;
        if (!(s->w = flux_timer_watcher_create (s->grace, 0., shutdown_cb, s)))
            goto done;
        flux_timer_watcher_start (s->h, s->w);
        if (flux_rank (s->h) == 0)
            flux_log (s->h, LOG_INFO, "%d: shutdown in %.3fs: %s",
                      s->rank, s->grace, s->reason);
    }
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

    if (!s->w) {
        va_start (ap, fmt);
        msg = shutdown_vencode (grace, exitcode, flux_rank (s->h), fmt, ap);
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
