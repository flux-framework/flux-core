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
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <zmq.h>

#include "flog.h"
#include "info.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

typedef struct {
    char appname[STDLOG_MAX_APPNAME + 1];
    char procid[STDLOG_MAX_PROCID + 1];
    char buf[FLUX_MAX_LOGBUF + 1];
    flux_log_f cb;
    void *cb_arg;
} logctx_t;

static void freectx (void *arg)
{
    logctx_t *ctx = arg;
    free (ctx);
}

static logctx_t *getctx (flux_t *h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "flux::log");
    extern char *__progname;
    // or glib-ism: program_invocation_short_name

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        snprintf (ctx->procid, sizeof (ctx->procid), "%d", getpid ());
        snprintf (ctx->appname, sizeof (ctx->appname), "%s", __progname);
        flux_aux_set (h, "flux::log", ctx, freectx);
    }
    return ctx;
}

void flux_log_set_appname (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    snprintf (ctx->appname, sizeof (ctx->appname), "%s", s);
}

void flux_log_set_procid (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    snprintf (ctx->procid, sizeof (ctx->procid), "%s", s);
}


void flux_log_set_redirect (flux_t *h, flux_log_f fun, void *arg)
{
    logctx_t *ctx = getctx (h);
    ctx->cb = fun;
    ctx->cb_arg = arg;
}

const char *flux_strerror (int errnum)
{
    return zmq_strerror (errnum);
}

void flux_vlog (flux_t *h, int level, const char *fmt, va_list ap)
{
    logctx_t *ctx = getctx (h);
    int saved_errno = errno;
    uint32_t rank;
    flux_rpc_t *rpc = NULL;
    int n, len;
    char timestamp[WALLCLOCK_MAXLEN];
    char hostname[STDLOG_MAX_HOSTNAME + 1];
    struct stdlog_header hdr;

    stdlog_init (&hdr);
    hdr.pri = STDLOG_PRI (level, LOG_USER);
    if (wallclock_get_zulu (timestamp, sizeof (timestamp)) >= 0)
        hdr.timestamp = timestamp;
    if (flux_get_rank (h, &rank) == 0) {
        snprintf (hostname, sizeof (hostname), "%" PRIu32, rank);
        hdr.hostname = hostname;
    }
    hdr.appname = ctx->appname;
    hdr.procid = ctx->procid;

    len = stdlog_encode (ctx->buf, sizeof (ctx->buf), &hdr,
                         STDLOG_NILVALUE, "");
    assert (len < sizeof (ctx->buf));

    n = vsnprintf (ctx->buf + len, sizeof (ctx->buf) - len, fmt, ap);
    if (n > sizeof (ctx->buf) - len) /* ignore truncation of message */
        n = sizeof (ctx->buf) - len;
    len += n;

    if (ctx->cb) {
        ctx->cb (ctx->buf, len, ctx->cb_arg);
    } else {
        if (!(rpc = flux_rpc_raw (h, "log.append", ctx->buf, len,
                                  FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE)))
            goto done;
    }
done:
    flux_rpc_destroy (rpc);
    errno = saved_errno;
}

void flux_log (flux_t *h, int lev, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    flux_vlog (h, lev, fmt, ap);
    va_end (ap);
}

void flux_log_verror (flux_t *h, const char *fmt, va_list ap)
{
    int saved_errno = errno;
    char *s = xvasprintf (fmt, ap);

    flux_log (h, LOG_ERR, "%s: %s", s, flux_strerror (errno));
    free (s);
    errno = saved_errno;
}

void flux_log_error (flux_t *h, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    flux_log_verror (h, fmt, ap);
    va_end (ap);
}

static int dmesg_clear (flux_t *h, int seq)
{
    flux_rpc_t *rpc;
    int rc = -1;

    if (!(rpc = flux_rpcf (h, "log.clear", FLUX_NODEID_ANY, 0,
                           "{s:i}", "seq", seq)))
        goto done;
    if (flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

static flux_rpc_t *dmesg_rpc (flux_t *h, int seq, bool follow)
{
    return flux_rpcf (h, "log.dmesg", FLUX_NODEID_ANY, 0,
                      "{s:i s:b}", "seq", seq, "follow", follow);
}

static int dmesg_rpc_get (flux_rpc_t *rpc, int *seq, flux_log_f fun, void *arg)
{
    const char *buf;
    int rc = -1;

    if (flux_rpc_getf (rpc, "{s:i s:s}", "seq", seq, "buf", &buf) < 0)
        goto done;
    fun (buf, strlen (buf), arg);
    rc = 0;
done:
    return rc;
}

int flux_dmesg (flux_t *h, int flags, flux_log_f fun, void *arg)
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

void flux_log_fprint (const char *buf, int len, void *arg)
{
    FILE *f = arg;
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (f) {
        if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
            fprintf (f, "%.*s\n", len, buf);
        else {
            nodeid = strtoul (hdr.hostname, NULL, 10);
            severity = STDLOG_SEVERITY (hdr.pri);
            fprintf (f, "%s %s.%s[%" PRIu32 "]: %.*s\n",
                     hdr.timestamp,
                     hdr.appname,
                     stdlog_severity_to_string (severity),
                     nodeid,
                     msglen, msg);
        }
        fflush (f);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
