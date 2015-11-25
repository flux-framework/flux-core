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
#include <syslog.h>
#include <sys/time.h>
#include <zmq.h>

#include "flog.h"
#include "info.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

typedef struct {
    char *facility;
    flux_log_f cb;
    void *cb_arg;
} logctx_t;

static void freectx (void *arg)
{
    logctx_t *ctx = arg;
    if (ctx->facility)
        free (ctx->facility);
    free (ctx);
}

static logctx_t *getctx (flux_t h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "flux::log");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->facility = xstrdup ("unknown");
        flux_aux_set (h, "flux::log", ctx, freectx);
    }
    return ctx;
}

void flux_log_set_facility (flux_t h, const char *facility)
{
    logctx_t *ctx = getctx (h);

    if (ctx->facility)
        free (ctx->facility);
    ctx->facility = xstrdup (facility);
}

void flux_log_set_redirect (flux_t h, flux_log_f fun, void *arg)
{
    logctx_t *ctx = getctx (h);
    ctx->cb = fun;
    ctx->cb_arg = arg;
}

int flux_vlog (flux_t h, int level, const char *fmt, va_list ap)
{
    logctx_t *ctx = getctx (h);
    char *message = xvasprintf (fmt, ap);
    struct timeval tv = { 0, 0 };
    JSON o = NULL;
    uint32_t rank = FLUX_NODEID_ANY;
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    (void)gettimeofday (&tv, NULL);
    (void)flux_get_rank (h, &rank);
    if (ctx->cb) {
        ctx->cb (ctx->facility, level, rank, tv, message, ctx->cb_arg);
    } else {
        o = Jnew ();
        Jadd_str (o, "facility", ctx->facility);
        Jadd_int (o, "level", level);
        Jadd_int (o, "rank", rank);
        Jadd_int (o, "timestamp_usec", tv.tv_usec);
        Jadd_int (o, "timestamp_sec", tv.tv_sec);
        Jadd_str (o, "message", message);
        if (!(rpc = flux_rpc (h, "cmb.log", Jtostr (o), FLUX_NODEID_ANY,
                                                        FLUX_RPC_NORESPONSE)))
            goto done;
    }
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    Jput (o);
    free (message);
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

int flux_log_verror (flux_t h, const char *fmt, va_list ap)
{
    char *s = xvasprintf (fmt, ap);
    int rc;

    rc = flux_log (h, LOG_ERR, "%s: %s", s, zmq_strerror (errno));
    free (s);

    return rc;
}

int flux_log_error (flux_t h, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_log_verror (h, fmt, ap);
    va_end (ap);

    return rc;
}

// Instantiate inline flux_log_check functions, this is *necessary* for
// linking to work under C99 rules
int flux_log_check_int (flux_t h,
               int res,
               const char *fmt,
               ...  )
{
    if (res < 0) {
        va_list ap;
        va_start (ap, fmt);
        flux_log_verror (h, fmt, ap);
        va_end (ap);
        exit (errno);
    }
    return res;
}

void *flux_log_check_ptr (flux_t h,
                 void *res,
                 const char *fmt,
                 ... )
{
    if (res == NULL) {
        va_list ap;
        va_start (ap, fmt);
        flux_log_verror (h, fmt, ap);
        va_end (ap);
        exit (errno);
    }
    return res;
}

static int dmesg_clear (flux_t h, int seq)
{
    flux_rpc_t *rpc;
    JSON o = Jnew ();
    int rc = -1;

    Jadd_int (o, "seq", seq);
    if (!(rpc = flux_rpc (h, "cmb.dmesg.clear", Jtostr (o),
                          FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    Jput (o);
    return rc;
}

static flux_rpc_t *dmesg_rpc (flux_t h, int seq, bool follow)
{
    flux_rpc_t *rpc;
    JSON o = Jnew ();
    Jadd_int (o, "seq", seq);
    Jadd_bool (o, "follow", follow);
    rpc = flux_rpc (h, "cmb.dmesg", Jtostr (o), FLUX_NODEID_ANY, 0);
    Jput (o);
    return rpc;
}

static int dmesg_rpc_get (flux_rpc_t *rpc, int *seq, flux_log_f fun, void *arg)
{
    const char *json_str;
    const char *facility, *message;
    JSON o = NULL;
    int level, rank, tv_usec, tv_sec;
    struct timeval tv;
    int rc = -1;

    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str)) || !Jget_str (o, "facility", &facility)
                                   || !Jget_int (o, "level", &level)
                                   || !Jget_int (o, "rank", &rank)
                                   || !Jget_int (o, "timestamp_usec", &tv_usec)
                                   || !Jget_int (o, "timestamp_sec", &tv_sec)
                                   || !Jget_str (o, "message", &message)
                                   || !Jget_int (o, "seq", seq)) {
        errno = EPROTO;
        goto done;
    }
    tv.tv_usec = tv_usec;
    tv.tv_sec = tv_sec;
    fun (facility, level, rank, tv, message, arg);
    rc = 0;
done:
    Jput (o);
    return rc;
}

int flux_dmesg (flux_t h, int flags, flux_log_f fun, void *arg)
{
    int rc = -1;
    int seq = -1;
    bool eof = false;
    bool follow = false;

    if (flags & FLUX_DMESG_FOLLOW)
        follow = true;
    if (fun) {
        while (!eof) {
            flux_rpc_t *rpc;
            if (!(rpc = dmesg_rpc (h, seq, follow)))
                goto done;
            if (dmesg_rpc_get (rpc, &seq, fun, arg) < 0) {
                if (errno != ENOENT) {
                    flux_rpc_destroy (rpc);
                    goto done;
                }
                eof = true;
            }
            flux_rpc_destroy (rpc);
        }
    }
    if ((flags & FLUX_DMESG_CLEAR)) {
        if (dmesg_clear (h, seq) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

void flux_log_fprint (const char *facility, int level, uint32_t rank,
                      struct timeval tv, const char *message, void *arg)
{
    FILE *f = arg;
    if (f) {
        const char *levelstr = log_leveltostr (level);
        if (!levelstr)
            levelstr = "unknown";
        fprintf (f, "[%ld.%06ld] %s.%s[%" PRIu32 "]: %s\n",
                 tv.tv_sec, tv.tv_usec, facility, levelstr, rank, message);
        fflush (f);
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
