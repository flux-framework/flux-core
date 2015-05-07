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
#include "request.h"
#include "message.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"


typedef struct {
    char *facility;
    bool redirect;
} logctx_t;

typedef struct {
    int level;
    const char *facility;
    int rank;
    const char *msg;
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

static JSON flog_encode (flog_t *f)
{
    JSON o = Jnew ();

    Jadd_str (o, "facility", f->facility);
    Jadd_int (o, "level", f->level);
    Jadd_int (o, "rank", f->rank);
    Jadd_int (o, "timestamp_usec", (int)f->tv.tv_usec);
    Jadd_int (o, "timestamp_sec", (int)f->tv.tv_sec);
    Jadd_str (o, "message", f->msg);
    return o;
}

static int flog_decode (JSON o, flog_t *f)
{
    int rc = -1;
    int sec, usec;
    if (!Jget_str (o, "facility", &f->facility))
        goto done;
    if (!Jget_int (o, "level", &f->level))
        goto done;
    if (!Jget_int (o, "rank", &f->rank))
        goto done;
    if (!Jget_int (o, "timestamp_usec", &usec))
        goto done;
    if (!Jget_int (o, "timestamp_sec", &sec))
        goto done;
    f->tv.tv_sec = sec;
    f->tv.tv_usec = usec;
    if (!Jget_str (o, "message", &f->msg))
        goto done;
    rc = 0;
done:
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
    JSON o = NULL;
    char *s = NULL;
    flog_t flog;
    int rc = -1;

    flog.facility = ctx->facility;
    flog.level = lev;
    flog.rank = flux_rank (h);
    if (gettimeofday (&flog.tv, NULL) < 0)
        err_exit ("gettimeofday");
    if (vasprintf (&s, fmt, ap) < 0)
        oom ();
    flog.msg = s;

    if (ctx->redirect) {
        flog_msg (&flog);
        rc = 0;
    } else {
        if (!(o = flog_encode (&flog)))
            goto done;
        rc = flux_json_request (h, FLUX_NODEID_ANY,
                                   FLUX_MATCHTAG_NONE, "cmb.log", o);
    }
done:
    if (s)
        free (s);
    Jput (o);
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
    JSON o = NULL;
    flog_t f;
    int rc = -1;

    if (flux_json_request_decode (zmsg, &o) < 0 || flog_decode (o, &f) < 0) {
        errno = EPROTO;
        goto done;
    }
    flog_msg (&f);
    rc = 0;
done:
    Jput (o);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
