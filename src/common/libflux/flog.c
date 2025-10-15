/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <flux/core.h>

#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/errno_safe.h"

#define FLUX_MAX_LOGBUF     2048

typedef struct {
    char appname[STDLOG_MAX_APPNAME + 1];
    char procid[STDLOG_MAX_PROCID + 1];
    char buf[FLUX_MAX_LOGBUF];
    flux_log_f cb;
    void *cb_arg;
    char hostname[STDLOG_MAX_HOSTNAME + 1];
} logctx_t;

static void freectx (void *arg)
{
    logctx_t *ctx = arg;
    if (ctx) {
        int saved_errno = errno;
        free (ctx);
        errno = saved_errno;
    }
}

static logctx_t *logctx_new (flux_t *h)
{
    logctx_t *ctx;
    extern char *__progname;
    // or glib-ism: program_invocation_short_name

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    snprintf (ctx->procid, sizeof (ctx->procid), "%d", getpid ());
    snprintf (ctx->appname, sizeof (ctx->appname), "%s", __progname);
    if (flux_aux_set (h, "flux::log", ctx, freectx) < 0)
        goto error;
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static logctx_t *getctx (flux_t *h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "flux::log");
    if (!ctx)
        ctx = logctx_new (h);
    return ctx;
}

void flux_log_set_appname (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    if (ctx)
        snprintf (ctx->appname, sizeof (ctx->appname), "%s", s);
}

void flux_log_set_procid (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    if (ctx)
        snprintf (ctx->procid, sizeof (ctx->procid), "%s", s);
}

void flux_log_set_hostname (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    if (ctx)
        snprintf (ctx->hostname, sizeof (ctx->hostname), "%s", s ? s : "");
}

void flux_log_set_redirect (flux_t *h, flux_log_f fun, void *arg)
{
    logctx_t *ctx = getctx (h);
    if (ctx) {
        ctx->cb = fun;
        ctx->cb_arg = arg;
    }
}


const char *flux_strerror (int errnum)
{
    return strerror (errnum);
}

static int log_rpc (flux_t *h, const char *buf, size_t len)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode_raw ("log.append", buf, len))
        || flux_send_new (h, &msg, 0) < 0) {
        flux_msg_destroy (msg);
        return -1;
    }
    return 0;
}

static int vlog_stderr (int level, const char *fmt, va_list ap)
{
    char buf[FLUX_MAX_LOGBUF];
    char *result = buf;
    char *mbuf = NULL;
    const char *lstr = stdlog_severity_to_string (LOG_PRI (level));
    int rc;
    va_list ap_copy;

    /* Copy va_list ap in case it reused on truncation below:
     */
    va_copy (ap_copy, ap);

    if (vsnprintf (result, sizeof (buf), fmt, ap) >= sizeof (buf)) {
        /* Message truncation occurred. Fall back to malloc. If that
         * fails, just print the truncated message:
         */
        if (vasprintf (&mbuf, fmt, ap_copy) == 0)
            result = mbuf;
    }
    rc = fprintf (stderr, "%s: %s\n", lstr, result);
    va_end (ap_copy);
    ERRNO_SAFE_WRAP (free, mbuf);
    return rc < 0 ? -1 : 0;
}

int flux_vlog (flux_t *h, int level, const char *fmt, va_list ap)
{
    logctx_t *ctx;
    int saved_errno = errno;
    uint32_t rank;
    int n;
    size_t len;
    char timestamp[WALLCLOCK_MAXLEN];
    struct stdlog_header hdr;
    char *xtra = NULL;
    char *mbuf = NULL;
    char *result;
    va_list ap_copy;

    if (!h)
        return vlog_stderr (level, fmt, ap);

    if (!(ctx = getctx (h))) {
        errno = ENOMEM;
        goto fatal;
    }

    stdlog_init (&hdr);
    hdr.pri = STDLOG_PRI (level, LOG_USER);
    if (wallclock_get_zulu (timestamp, sizeof (timestamp)) >= 0)
        hdr.timestamp = timestamp;
    if (ctx->hostname[0] == '\0' && flux_get_rank (h, &rank) == 0)
        snprintf (ctx->hostname, sizeof (ctx->hostname), "%" PRIu32, rank);
    hdr.hostname = ctx->hostname;
    hdr.appname = ctx->appname;
    hdr.procid = ctx->procid;

    /* Copy va_list ap in case it reused on truncation below:
     */
    va_copy (ap_copy, ap);

    n = stdlog_vencodef (ctx->buf,
                         sizeof (ctx->buf),
                         &hdr,
                         STDLOG_NILVALUE,
                         fmt,
                         ap);
    result = ctx->buf;
    len = n >= sizeof (ctx->buf) ? sizeof (ctx->buf) : n;

    /* Unlikely: On overflow, try again with a buffer allocated on the heap.
     * If this fails for any reason, fall back to truncated output.
     */
    if (n >= sizeof (ctx->buf)) {
       if ((mbuf = malloc (n + 1))) {
            /* Note: `n` returned from stdlog_vencodef() may be > actual
             * length because if there's truncation then this function cannot
             * drop trailing \r, \n from the result. Therefore, ignore return
             * of stdlog_vencodef() here (if malloc(3) succeeds) because it is
             * known the result will fit in the allocated buffer, but the
             * expected return value is not known.
             */
            (void) stdlog_vencodef (mbuf,
                                    n + 1,
                                    &hdr,
                                    STDLOG_NILVALUE,
                                    fmt,
                                    ap_copy);
            result = mbuf;
            len = n;
        }
    }

    va_end (ap_copy);

    /* If log message contains multiple lines, log the first
     * line and save the rest.
     */
    xtra = stdlog_split_message (result, &len, "\r\n");
    if (ctx->cb) {
        ctx->cb (result, len, ctx->cb_arg);
    } else {
        if (log_rpc (h, result, len) < 0)
            goto fatal;
    }
    /* If addition log lines were saved above, log them separately.
     */
    if (xtra)
        flux_log (h, level, "%s", xtra);
    free (xtra);
    free (mbuf);
    errno = saved_errno;
    return 0;
fatal:
    free (xtra);
    free (mbuf);
    return -1;
}

int flux_log (flux_t *h, int lev, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vlog (h, lev, fmt, ap);
    va_end (ap);
    return rc;
}

void flux_log_verror (flux_t *h, const char *fmt, va_list ap)
{
    int saved_errno = errno;
    char buf[FLUX_MAX_LOGBUF];

    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    flux_log (h, LOG_ERR, "%s: %s", buf, flux_strerror (saved_errno));
    errno = saved_errno;
}

void flux_log_error (flux_t *h, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    flux_log_verror (h, fmt, ap);
    va_end (ap);
}

void flux_llog (void *arg,
                const char *file,
                int line,
                const char *func,
                const char *subsys,
                int level,
                const char *fmt,
                va_list ap)
{
    flux_t *h = arg;
    // ignoring subsys, file, line
    flux_vlog (h, level, fmt, ap);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
