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
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>

#include "flog.h"
#include "info.h"
#include "message.h"
#include "request.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"


typedef struct {
    char *facility;
    bool redirect;
} logctx_t;

typedef struct {
    int level;
    char facility[64];
    int rank;
    char msg[1024];
    struct timeval tv;
} flog_t;


static void freectx (logctx_t *ctx)
{
    if (ctx->facility)
        free (ctx->facility);
    free (ctx);
}

static logctx_t *getctx (flux_t h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "log");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->facility = xstrdup ("unknown");
        flux_aux_set (h, "log", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

static zmsg_t *flog_encode (flog_t *f)
{
    JSON o = Jnew ();
    zmsg_t *zmsg;

    Jadd_str (o, "facility", f->facility);
    Jadd_int (o, "level", f->level);
    Jadd_int (o, "rank", f->rank);
    Jadd_int (o, "timestamp_usec", (int)f->tv.tv_usec);
    Jadd_int (o, "timestamp_sec", (int)f->tv.tv_sec);
    Jadd_str (o, "message", f->msg);
    zmsg = flux_request_encode ("cmb.log", Jtostr (o));
    Jput (o);
    return zmsg;
}

static int flog_decode (zmsg_t *zmsg, flog_t *f)
{
    const char *json_str;
    JSON o = NULL;
    int sec, usec;
    const char *s;
    const char *facility;
    int rc = -1;

    if (flux_request_decode (zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))
            || !Jget_str (o, "facility", &facility)
            || !Jget_int (o, "level", &f->level)
            || !Jget_int (o, "rank", &f->rank)
            || !Jget_int (o, "timestamp_usec", &usec)
            || !Jget_int (o, "timestamp_sec", &sec)
            || !Jget_str (o, "message", &s)) {
        errno = EPROTO;
        goto done;
    }
    snprintf (f->facility, sizeof (f->facility), "%s", facility);
    snprintf (f->msg, sizeof (f->msg), "%s", s);
    f->tv.tv_sec = sec;
    f->tv.tv_usec = usec;
    rc = 0;
done:
    Jput (o);
    return rc;
}

static void flog_msg (flog_t *flog)
{
    const char *levstr = log_leveltostr (flog->level);
    if (!levstr)
        levstr = "unknown";
    msg ("[%-.6lu.%-.6lu] %s.%s[%d] %s",
         flog->tv.tv_sec, flog->tv.tv_usec, flog->facility, levstr,
         flog->rank, flog->msg);
}

void flux_log_set_facility (flux_t h, const char *facility)
{
    logctx_t *ctx = getctx (h);

    if (ctx->facility)
        free (ctx->facility);
    ctx->facility = xstrdup (facility);
}

void flux_log_set_redirect (flux_t h, bool flag)
{
    logctx_t *ctx = getctx (h);
    ctx->redirect = flag;
}

int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap)
{
    logctx_t *ctx = getctx (h);
    flog_t flog;
    zmsg_t *zmsg = NULL;
    int rc = -1;

    snprintf (flog.facility, sizeof (flog.facility), "%s", ctx->facility);
    flog.level = lev;
    flog.rank = flux_rank (h);
    if (gettimeofday (&flog.tv, NULL) < 0)
        err_exit ("gettimeofday");
    (void)vsnprintf (flog.msg, sizeof (flog.msg), fmt, ap);

    if (ctx->redirect) {
        flog_msg (&flog);
    } else if (!(zmsg = flog_encode (&flog))
                     || flux_request_send (h, FLUX_MATCHTAG_NONE, &zmsg) < 0) {
        goto done;
    }
    rc = 0;
done:
    zmsg_destroy (&zmsg);
    return rc;
}

int flux_log (flux_t h, int lev, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vlog (h, lev, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_log_zmsg (zmsg_t *zmsg)
{
    flog_t f;
    int rc = -1;

    if (flog_decode (zmsg, &f) < 0)
        goto done;
    flog_msg (&f);
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
