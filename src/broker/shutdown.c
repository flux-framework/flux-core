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

struct shutdown_struct {
    zloop_t *zloop;
    flux_t h;
    int tid;

    int rc;
    int rank;
    char reason[256];
    int grace;
};

shutdown_t shutdown_create (void)
{
    shutdown_t s = xzmalloc (sizeof (*s));
    s->tid = -1;
    return s;
}

void shutdown_destroy (shutdown_t s)
{
    if (s) {
        free (s);
    }
}

void shutdown_set_loop (shutdown_t s, zloop_t *zloop)
{
    s->zloop = zloop;
}

void shutdown_set_handle (shutdown_t s, flux_t h)
{
    s->h = h;
}

void shutdown_complete (shutdown_t s)
{
    if (s->tid != -1)
        zloop_timer_end (s->zloop, s->tid);
}

static int shutdown_cb (zloop_t *loop, int timer_id, void *arg)
{
    shutdown_t s = arg;
    exit (s->rc);
}

void shutdown_recvmsg (shutdown_t s, zmsg_t *zmsg)
{
    JSON in = NULL;
    const char *reason;
    int grace, rank, rc;

    if (flux_json_event_decode (zmsg, &in) < 0) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    if (!Jget_str (in, "reason", &reason) || !Jget_int (in, "grace", &grace)
                                          || !Jget_int (in, "rank", &rank)
                                          || !Jget_int (in, "exitcode", &rc)) {
        errn (EPROTO, "%s", __FUNCTION__);
        goto done;
    }
    if (s->tid == -1) {
        s->rc = rc;
        s->grace = grace;
        s->rank = rank;
        snprintf (s->reason, sizeof (s->reason), "%s", reason);
        s->tid = zloop_timer (s->zloop, grace * 1000, 1, shutdown_cb, s);
        if (s->tid == -1)
            err_exit ("zloop_timer");
        if (flux_rank (s->h) == 0)
            flux_log (s->h, LOG_INFO, "%d: shutdown in %ds: %s",
                      s->rank, s->grace, s->reason);
    }
done:
    Jput (in);
    return;
}

void shutdown_arm (shutdown_t s, int grace, int rc, const char *fmt, ...)
{
    va_list ap;
    char reason[256];

    if (s->tid != -1) {
        msg ("%s: shutdown already in progress", __FUNCTION__);
        return;
    }

    va_start (ap, fmt);
    vsnprintf (reason, sizeof (reason), fmt, ap);
    va_end (ap);

    JSON out = Jnew ();
    Jadd_str (out, "reason", reason);
    Jadd_int (out, "grace", grace);
    Jadd_int (out, "rank", flux_rank (s->h));
    Jadd_int (out, "exitcode", rc);
    if (flux_event_send (s->h, out, "shutdown") < 0)
        err_exit ("flux_event_send");
    Jput (out);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
